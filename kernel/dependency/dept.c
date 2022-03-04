// SPDX-License-Identifier: GPL-2.0
/*
 * DEPT(DEPendency Tracker) - Runtime dependency tracker
 *
 * Started by Byungchul Park <max.byungchul.park@gmail.com>:
 *
 *  Copyright (c) 2020 LG Electronics, Inc., Byungchul Park
 *
 * DEPT provides a general way to detect deadlock possibility in runtime
 * and the interest is not limited to typical lock but to every
 * syncronization primitives.
 *
 * The following ideas were borrowed from LOCKDEP:
 *
 *    1) Use a graph to track relationship between classes.
 *    2) Prevent performance regression using hash.
 *
 * The following items were enhanced from LOCKDEP:
 *
 *    1) Cover more deadlock cases.
 *    2) Allow muliple reports.
 *
 * TODO: Both LOCKDEP and DEPT should co-exist until DEPT is considered
 * stable. Then the dependency check routine should be replaced with
 * DEPT after. It should finally look like:
 *
 *
 *
 * As is:
 *
 *    LOCKDEP
 *    +-----------------------------------------+
 *    | Lock usage correctness check            | <-> locks
 *    |                                         |
 *    |                                         |
 *    | +-------------------------------------+ |
 *    | | Dependency check                    | |
 *    | | (by tracking lock acquisition order)| |
 *    | +-------------------------------------+ |
 *    |                                         |
 *    +-----------------------------------------+
 *
 *    DEPT
 *    +-----------------------------------------+
 *    | Dependency check                        | <-> waits/events
 *    | (by tracking wait and event context)    |
 *    +-----------------------------------------+
 *
 *
 *
 * To be:
 *
 *    LOCKDEP
 *    +-----------------------------------------+
 *    | Lock usage correctness check            | <-> locks
 *    |                                         |
 *    |                                         |
 *    |       (Request dependency check)        |
 *    |                    T                    |
 *    +--------------------|--------------------+
 *                         |
 *    DEPT                 V
 *    +-----------------------------------------+
 *    | Dependency check                        | <-> waits/events
 *    | (by tracking wait and event context)    |
 *    +-----------------------------------------+
 */

#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/hash.h>
#include <linux/dept.h>
#include <linux/utsname.h>

static int dept_stop;
static int dept_per_cpu_ready;

#define DEPT_READY_WARN (!oops_in_progress)

/*
 * Make all operations using DEPT_WARN_ON() fail on oops_in_progress and
 * prevent warning message.
 */
#define DEPT_WARN_ON_ONCE(c)						\
	({								\
		int __ret = 0;						\
									\
		if (likely(DEPT_READY_WARN))				\
			__ret = WARN_ONCE(c, "DEPT_WARN_ON_ONCE: " #c);	\
		__ret;							\
	})

#define DEPT_WARN_ONCE(s...)						\
	({								\
		if (likely(DEPT_READY_WARN))				\
			WARN_ONCE(1, "DEPT_WARN_ONCE: " s);		\
	})

#define DEPT_WARN_ON(c)							\
	({								\
		int __ret = 0;						\
									\
		if (likely(DEPT_READY_WARN))				\
			__ret = WARN(c, "DEPT_WARN_ON: " #c);		\
		__ret;							\
	})

#define DEPT_WARN(s...)							\
	({								\
		if (likely(DEPT_READY_WARN))				\
			WARN(1, "DEPT_WARN: " s);			\
	})

#define DEPT_STOP(s...)							\
	({								\
		WRITE_ONCE(dept_stop, 1);				\
		if (likely(DEPT_READY_WARN))				\
			WARN(1, "DEPT_STOP: " s);			\
	})

static arch_spinlock_t dept_spin = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;

/*
 * DEPT internal engine should be careful in using outside functions
 * e.g. printk at reporting since that kind of usage might cause
 * untrackable deadlock.
 */
static atomic_t dept_outworld = ATOMIC_INIT(0);

static inline void dept_outworld_enter(void)
{
	atomic_inc(&dept_outworld);
}

static inline void dept_outworld_exit(void)
{
	atomic_dec(&dept_outworld);
}

static inline bool dept_outworld_entered(void)
{
	return atomic_read(&dept_outworld);
}

static inline bool dept_lock(void)
{
	while (!arch_spin_trylock(&dept_spin))
		if (unlikely(dept_outworld_entered()))
			return false;
	return true;
}

static inline void dept_unlock(void)
{
	arch_spin_unlock(&dept_spin);
}

/*
 * whether to stack-trace on every wait or every ecxt
 */
static bool rich_stack = true;

enum bfs_ret {
	BFS_CONTINUE,
	BFS_CONTINUE_REV,
	BFS_DONE,
	BFS_SKIP,
};

static inline bool before(unsigned int a, unsigned int b)
{
	return (int)(a - b) < 0;
}

static inline bool valid_stack(struct dept_stack *s)
{
	return s && s->nr > 0;
}

static inline bool valid_class(struct dept_class *c)
{
	return c->key;
}

static inline void inval_class(struct dept_class *c)
{
	c->key = 0UL;
}

static inline struct dept_ecxt *dep_e(struct dept_dep *d)
{
	return d->ecxt;
}

static inline struct dept_wait *dep_w(struct dept_dep *d)
{
	return d->wait;
}

static inline struct dept_class *dep_fc(struct dept_dep *d)
{
	return dep_e(d)->class;
}

static inline struct dept_class *dep_tc(struct dept_dep *d)
{
	return dep_w(d)->class;
}

static inline const char *irq_str(int irq)
{
	if (irq == DEPT_SIRQ)
		return "softirq";
	if (irq == DEPT_HIRQ)
		return "hardirq";
	return "(unknown)";
}

static inline struct dept_task *dept_task(void)
{
	return &current->dept_task;
}

/*
 * Pool
 * =====================================================================
 * DEPT maintains pools to provide objects in a safe way.
 *
 *    1) Static pool is used at the beginning of booting time.
 *    2) Local pool is tried first before the static pool. Objects that
 *       have been freed will be placed.
 */

enum object_t {
#define OBJECT(id, nr) OBJECT_##id,
	#include "dept_object.h"
#undef  OBJECT
	OBJECT_NR,
};

#define OBJECT(id, nr)							\
static struct dept_##id spool_##id[nr];					\
static DEFINE_PER_CPU(struct llist_head, lpool_##id);
	#include "dept_object.h"
#undef  OBJECT

static struct dept_pool pool[OBJECT_NR] = {
#define OBJECT(id, nr) {						\
	.name = #id,							\
	.obj_sz = sizeof(struct dept_##id),				\
	.obj_nr = ATOMIC_INIT(nr),					\
	.node_off = offsetof(struct dept_##id, pool_node),		\
	.spool = spool_##id,						\
	.lpool = &lpool_##id, },
	#include "dept_object.h"
#undef  OBJECT
};

/*
 * Can use llist no matter whether CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG is
 * enabled or not because NMI and other contexts in the same CPU never
 * run inside of DEPT concurrently by preventing reentrance.
 */
static void *from_pool(enum object_t t)
{
	struct dept_pool *p;
	struct llist_head *h;
	struct llist_node *n;

	/*
	 * llist_del_first() doesn't allow concurrent access e.g.
	 * between process and IRQ context.
	 */
	if (DEPT_WARN_ON(!irqs_disabled()))
		return NULL;

	p = &pool[t];

	/*
	 * Try local pool first.
	 */
	if (likely(dept_per_cpu_ready))
		h = this_cpu_ptr(p->lpool);
	else
		h = &p->boot_pool;

	n = llist_del_first(h);
	if (n)
		return (void *)n - p->node_off;

	/*
	 * Try static pool.
	 */
	if (atomic_read(&p->obj_nr) > 0) {
		int idx = atomic_dec_return(&p->obj_nr);

		if (idx >= 0)
			return p->spool + (idx * p->obj_sz);
	}

	DEPT_WARN_ONCE("Pool(%s) is empty.\n", p->name);
	return NULL;
}

static void to_pool(void *o, enum object_t t)
{
	struct dept_pool *p = &pool[t];
	struct llist_head *h;

	preempt_disable();
	if (likely(dept_per_cpu_ready))
		h = this_cpu_ptr(p->lpool);
	else
		h = &p->boot_pool;

	llist_add(o + p->node_off, h);
	preempt_enable();
}

#define OBJECT(id, nr)							\
static void (*ctor_##id)(struct dept_##id *a);				\
static void (*dtor_##id)(struct dept_##id *a);				\
static inline struct dept_##id *new_##id(void)				\
{									\
	struct dept_##id *a;						\
									\
	a = (struct dept_##id *)from_pool(OBJECT_##id);			\
	if (unlikely(!a))						\
		return NULL;						\
									\
	atomic_set(&a->ref, 1);						\
									\
	if (ctor_##id)							\
		ctor_##id(a);						\
									\
	return a;							\
}									\
									\
static inline struct dept_##id *get_##id(struct dept_##id *a)		\
{									\
	atomic_inc(&a->ref);						\
	return a;							\
}									\
									\
static inline void put_##id(struct dept_##id *a)			\
{									\
	if (!atomic_dec_return(&a->ref)) {				\
		if (dtor_##id)						\
			dtor_##id(a);					\
		to_pool(a, OBJECT_##id);				\
	}								\
}									\
									\
static inline void del_##id(struct dept_##id *a)			\
{									\
	put_##id(a);							\
}									\
									\
static inline bool id##_consumed(struct dept_##id *a)			\
{									\
	return a && atomic_read(&a->ref) > 1;				\
}
#include "dept_object.h"
#undef  OBJECT

#define SET_CONSTRUCTOR(id, f) \
static void (*ctor_##id)(struct dept_##id *a) = f

static void initialize_dep(struct dept_dep *d)
{
	INIT_LIST_HEAD(&d->bfs_node);
	INIT_LIST_HEAD(&d->dep_node);
	INIT_LIST_HEAD(&d->dep_rev_node);
}
SET_CONSTRUCTOR(dep, initialize_dep);

static void initialize_class(struct dept_class *c)
{
	int i;

	for (i = 0; i < DEPT_IRQS_NR; i++) {
		struct dept_iecxt *ie = &c->iecxt[i];
		struct dept_iwait *iw = &c->iwait[i];

		ie->ecxt = NULL;
		ie->enirq = i;
		ie->staled = false;

		iw->wait = NULL;
		iw->irq = i;
		iw->staled = false;
		iw->touched = false;
	}
	c->bfs_gen = 0U;

	INIT_LIST_HEAD(&c->all_node);
	INIT_LIST_HEAD(&c->dep_head);
	INIT_LIST_HEAD(&c->dep_rev_head);
}
SET_CONSTRUCTOR(class, initialize_class);

static void initialize_ecxt(struct dept_ecxt *e)
{
	int i;

	for (i = 0; i < DEPT_IRQS_NR; i++) {
		e->enirq_stack[i] = NULL;
		e->enirq_ip[i] = 0UL;
	}
	e->ecxt_ip = 0UL;
	e->ecxt_stack = NULL;
	e->enirqf = 0UL;
	e->event_stack = NULL;
}
SET_CONSTRUCTOR(ecxt, initialize_ecxt);

static void initialize_wait(struct dept_wait *w)
{
	int i;

	for (i = 0; i < DEPT_IRQS_NR; i++) {
		w->irq_stack[i] = NULL;
		w->irq_ip[i] = 0UL;
	}
	w->wait_ip = 0UL;
	w->wait_stack = NULL;
	w->irqf = 0UL;
}
SET_CONSTRUCTOR(wait, initialize_wait);

static void initialize_stack(struct dept_stack *s)
{
	s->nr = 0;
}
SET_CONSTRUCTOR(stack, initialize_stack);

#define OBJECT(id, nr) \
static void (*ctor_##id)(struct dept_##id *a);
	#include "dept_object.h"
#undef  OBJECT

#undef  SET_CONSTRUCTOR

#define SET_DESTRUCTOR(id, f) \
static void (*dtor_##id)(struct dept_##id *a) = f

static void destroy_dep(struct dept_dep *d)
{
	if (dep_e(d))
		put_ecxt(dep_e(d));
	if (dep_w(d))
		put_wait(dep_w(d));
}
SET_DESTRUCTOR(dep, destroy_dep);

static void destroy_ecxt(struct dept_ecxt *e)
{
	int i;

	for (i = 0; i < DEPT_IRQS_NR; i++)
		if (e->enirq_stack[i])
			put_stack(e->enirq_stack[i]);
	if (e->class)
		put_class(e->class);
	if (e->ecxt_stack)
		put_stack(e->ecxt_stack);
	if (e->event_stack)
		put_stack(e->event_stack);
}
SET_DESTRUCTOR(ecxt, destroy_ecxt);

static void destroy_wait(struct dept_wait *w)
{
	int i;

	for (i = 0; i < DEPT_IRQS_NR; i++)
		if (w->irq_stack[i])
			put_stack(w->irq_stack[i]);
	if (w->class)
		put_class(w->class);
	if (w->wait_stack)
		put_stack(w->wait_stack);
}
SET_DESTRUCTOR(wait, destroy_wait);

#define OBJECT(id, nr) \
static void (*dtor_##id)(struct dept_##id *a);
	#include "dept_object.h"
#undef  OBJECT

#undef  SET_DESTRUCTOR

/*
 * Caching and hashing
 * =====================================================================
 * DEPT makes use of caching and hashing to improve performance. Each
 * object can be obtained in O(1) with its key.
 *
 * NOTE: Currently we assume all the objects in the hashs will never be
 * removed. Implement it when needed.
 */

/*
 * Some information might be lost but it's only for hashing key.
 */
static inline unsigned long mix(unsigned long a, unsigned long b)
{
	int halfbits = sizeof(unsigned long) * 8 / 2;
	unsigned long halfmask = (1UL << halfbits) - 1UL;

	return (a << halfbits) | (b & halfmask);
}

static bool cmp_dep(struct dept_dep *d1, struct dept_dep *d2)
{
	return dep_fc(d1)->key == dep_fc(d2)->key &&
	       dep_tc(d1)->key == dep_tc(d2)->key;
}

static unsigned long key_dep(struct dept_dep *d)
{
	return mix(dep_fc(d)->key, dep_tc(d)->key);
}

static bool cmp_class(struct dept_class *c1, struct dept_class *c2)
{
	return c1->key == c2->key;
}

static unsigned long key_class(struct dept_class *c)
{
	return c->key;
}

#define HASH(id, bits)							\
static struct hlist_head table_##id[1UL << bits];			\
									\
static inline struct hlist_head *head_##id(struct dept_##id *a)		\
{									\
	return table_##id + hash_long(key_##id(a), bits);		\
}									\
									\
static inline struct dept_##id *hash_lookup_##id(struct dept_##id *a)	\
{									\
	struct dept_##id *b;						\
									\
	hlist_for_each_entry_rcu(b, head_##id(a), hash_node)		\
		if (cmp_##id(a, b))					\
			return b;					\
	return NULL;							\
}									\
									\
static inline void hash_add_##id(struct dept_##id *a)			\
{									\
	hlist_add_head_rcu(&a->hash_node, head_##id(a));		\
}									\
									\
static inline void hash_del_##id(struct dept_##id *a)			\
{									\
	hlist_del_rcu(&a->hash_node);					\
}
#include "dept_hash.h"
#undef  HASH

static inline struct dept_dep *lookup_dep(struct dept_class *fc,
					  struct dept_class *tc)
{
	struct dept_ecxt onetime_e = { .class = fc };
	struct dept_wait onetime_w = { .class = tc };
	struct dept_dep  onetime_d = { .ecxt = &onetime_e,
				       .wait = &onetime_w };
	return hash_lookup_dep(&onetime_d);
}

static inline struct dept_class *lookup_class(unsigned long key)
{
	struct dept_class onetime_c = { .key = key };

	return hash_lookup_class(&onetime_c);
}

/*
 * Report
 * =====================================================================
 * DEPT prints useful information to help debuging on detection of
 * problematic dependency.
 */

static inline void print_ip_stack(unsigned long ip, struct dept_stack *s)
{
	if (ip)
		print_ip_sym(KERN_WARNING, ip);

	if (valid_stack(s)) {
		pr_warn("stacktrace:\n");
		stack_trace_print(s->raw, s->nr, 5);
	}

	if (!ip && !valid_stack(s))
		pr_warn("(N/A)\n");
}

#define print_spc(spc, fmt, ...)					\
	pr_warn("%*c" fmt, (spc) * 4, ' ', ##__VA_ARGS__)

static void print_diagram(struct dept_dep *d)
{
	struct dept_ecxt *e = dep_e(d);
	struct dept_wait *w = dep_w(d);
	struct dept_class *fc = dep_fc(d);
	struct dept_class *tc = dep_tc(d);
	unsigned long irqf;
	int irq;
	bool firstline = true;
	int spc = 1;
	const char *w_fn = w->wait_fn  ?: "(unknown)";
	const char *e_fn = e->event_fn ?: "(unknown)";
	const char *c_fn = e->ecxt_fn ?: "(unknown)";

	irqf = e->enirqf & w->irqf;
	for_each_set_bit(irq, &irqf, DEPT_IRQS_NR) {
		if (!firstline)
			pr_warn("\nor\n\n");
		firstline = false;

		print_spc(spc, "[S] %s(%s:%d)\n", c_fn, fc->name, fc->sub);
		print_spc(spc, "    <%s interrupt>\n", irq_str(irq));
		print_spc(spc + 1, "[W] %s(%s:%d)\n", w_fn, tc->name, tc->sub);
		print_spc(spc, "[E] %s(%s:%d)\n", e_fn, fc->name, fc->sub);
	}

	if (!irqf) {
		print_spc(spc, "[S] %s(%s:%d)\n", c_fn, fc->name, fc->sub);
		print_spc(spc, "[W] %s(%s:%d)\n", w_fn, tc->name, tc->sub);
		print_spc(spc, "[E] %s(%s:%d)\n", e_fn, fc->name, fc->sub);
	}
}

static void print_dep(struct dept_dep *d)
{
	struct dept_ecxt *e = dep_e(d);
	struct dept_wait *w = dep_w(d);
	struct dept_class *fc = dep_fc(d);
	struct dept_class *tc = dep_tc(d);
	unsigned long irqf;
	int irq;
	const char *w_fn = w->wait_fn  ?: "(unknown)";
	const char *e_fn = e->event_fn ?: "(unknown)";
	const char *c_fn = e->ecxt_fn ?: "(unknown)";

	irqf = e->enirqf & w->irqf;
	for_each_set_bit(irq, &irqf, DEPT_IRQS_NR) {
		pr_warn("%s has been enabled:\n", irq_str(irq));
		print_ip_stack(e->enirq_ip[irq], e->enirq_stack[irq]);
		pr_warn("\n");

		pr_warn("[S] %s(%s:%d):\n", c_fn, fc->name, fc->sub);
		print_ip_stack(e->ecxt_ip, e->ecxt_stack);
		pr_warn("\n");

		pr_warn("[W] %s(%s:%d) in %s context:\n",
		       w_fn, tc->name, tc->sub, irq_str(irq));
		print_ip_stack(w->irq_ip[irq], w->irq_stack[irq]);
		pr_warn("\n");

		pr_warn("[E] %s(%s:%d):\n", e_fn, fc->name, fc->sub);
		print_ip_stack(e->event_ip, e->event_stack);
	}

	if (!irqf) {
		pr_warn("[S] %s(%s:%d):\n", c_fn, fc->name, fc->sub);
		print_ip_stack(e->ecxt_ip, e->ecxt_stack);
		pr_warn("\n");

		pr_warn("[W] %s(%s:%d):\n", w_fn, tc->name, tc->sub);
		print_ip_stack(w->wait_ip, w->wait_stack);
		pr_warn("\n");

		pr_warn("[E] %s(%s:%d):\n", e_fn, fc->name, fc->sub);
		print_ip_stack(e->event_ip, e->event_stack);
	}
}

static void save_current_stack(int skip);

/*
 * Print all classes in a circle.
 */
static void print_circle(struct dept_class *c)
{
	struct dept_class *fc = c->bfs_parent;
	struct dept_class *tc = c;
	int i;

	dept_outworld_enter();
	save_current_stack(6);

	pr_warn("===================================================\n");
	pr_warn("DEPT: Circular dependency has been detected.\n");
	pr_warn("%s %.*s %s\n", init_utsname()->release,
		(int)strcspn(init_utsname()->version, " "),
		init_utsname()->version,
		print_tainted());
	pr_warn("---------------------------------------------------\n");
	pr_warn("summary\n");
	pr_warn("---------------------------------------------------\n");

	if (fc == tc)
		pr_warn("*** AA DEADLOCK ***\n\n");
	else
		pr_warn("*** DEADLOCK ***\n\n");

	i = 0;
	do {
		struct dept_dep *d = lookup_dep(fc, tc);

		pr_warn("context %c\n", 'A' + (i++));
		print_diagram(d);
		if (fc != c)
			pr_warn("\n");

		tc = fc;
		fc = fc->bfs_parent;
	} while (tc != c);

	pr_warn("\n");
	pr_warn("[S]: start of the event context\n");
	pr_warn("[W]: the wait blocked\n");
	pr_warn("[E]: the event not reachable\n");

	i = 0;
	do {
		struct dept_dep *d = lookup_dep(fc, tc);

		pr_warn("---------------------------------------------------\n");
		pr_warn("context %c's detail\n", 'A' + i);
		pr_warn("---------------------------------------------------\n");
		pr_warn("context %c\n", 'A' + (i++));
		print_diagram(d);
		pr_warn("\n");
		print_dep(d);

		tc = fc;
		fc = fc->bfs_parent;
	} while (tc != c);

	pr_warn("---------------------------------------------------\n");
	pr_warn("information that might be helpful\n");
	pr_warn("---------------------------------------------------\n");
	dump_stack();

	dept_outworld_exit();
}

/*
 * BFS(Breadth First Search)
 * =====================================================================
 * Whenever a new dependency is added into the graph, search the graph
 * for a new circular dependency.
 */

static inline void enqueue(struct list_head *h, struct dept_dep *d)
{
	list_add_tail(&d->bfs_node, h);
}

static inline struct dept_dep *dequeue(struct list_head *h)
{
	struct dept_dep *d;

	d = list_first_entry(h, struct dept_dep, bfs_node);
	list_del(&d->bfs_node);
	return d;
}

static inline bool empty(struct list_head *h)
{
	return list_empty(h);
}

static void extend_queue(struct list_head *h, struct dept_class *cur)
{
	struct dept_dep *d;

	list_for_each_entry(d, &cur->dep_head, dep_node) {
		struct dept_class *next = dep_tc(d);

		if (cur->bfs_gen == next->bfs_gen)
			continue;
		next->bfs_gen = cur->bfs_gen;
		next->bfs_dist = cur->bfs_dist + 1;
		next->bfs_parent = cur;
		enqueue(h, d);
	}
}

static void extend_queue_rev(struct list_head *h, struct dept_class *cur)
{
	struct dept_dep *d;

	list_for_each_entry(d, &cur->dep_rev_head, dep_rev_node) {
		struct dept_class *next = dep_fc(d);

		if (cur->bfs_gen == next->bfs_gen)
			continue;
		next->bfs_gen = cur->bfs_gen;
		next->bfs_dist = cur->bfs_dist + 1;
		next->bfs_parent = cur;
		enqueue(h, d);
	}
}

typedef enum bfs_ret bfs_f(struct dept_dep *d, void *in, void **out);
static unsigned int bfs_gen;

/*
 * NOTE: Must be called with dept_lock held.
 */
static void bfs(struct dept_class *c, bfs_f *cb, void *in, void **out)
{
	LIST_HEAD(q);
	enum bfs_ret ret;

	if (DEPT_WARN_ON(!cb))
		return;

	/*
	 * Avoid zero bfs_gen.
	 */
	bfs_gen = bfs_gen + 1 ?: 1;

	c->bfs_gen = bfs_gen;
	c->bfs_dist = 0;
	c->bfs_parent = c;

	ret = cb(NULL, in, out);
	if (ret == BFS_DONE)
		return;
	if (ret == BFS_SKIP)
		return;
	if (ret == BFS_CONTINUE)
		extend_queue(&q, c);
	if (ret == BFS_CONTINUE_REV)
		extend_queue_rev(&q, c);

	while (!empty(&q)) {
		struct dept_dep *d = dequeue(&q);

		ret = cb(d, in, out);
		if (ret == BFS_DONE)
			break;
		if (ret == BFS_SKIP)
			continue;
		if (ret == BFS_CONTINUE)
			extend_queue(&q, dep_tc(d));
		if (ret == BFS_CONTINUE_REV)
			extend_queue_rev(&q, dep_fc(d));
	}

	while (!empty(&q))
		dequeue(&q);
}

/*
 * Main operations
 * =====================================================================
 * Add dependencies - Each new dependency is added into the graph and
 * checked if it forms a circular dependency.
 *
 * Track waits - Waits are queued into the ring buffer for later use to
 * generate appropriate dependencies with cross-event.
 *
 * Track event contexts(ecxt) - Event contexts are pushed into local
 * stack for later use to generate appropriate dependencies with waits.
 */

static inline unsigned long cur_enirqf(void);
static inline int cur_irq(void);
static inline unsigned int cur_ctxt_id(void);

static inline struct dept_iecxt *iecxt(struct dept_class *c, int irq)
{
	return &c->iecxt[irq];
}

static inline struct dept_iwait *iwait(struct dept_class *c, int irq)
{
	return &c->iwait[irq];
}

static inline void stale_iecxt(struct dept_iecxt *ie)
{
	if (ie->ecxt)
		put_ecxt(ie->ecxt);

	WRITE_ONCE(ie->ecxt, NULL);
	WRITE_ONCE(ie->staled, true);
}

static inline void set_iecxt(struct dept_iecxt *ie, struct dept_ecxt *e)
{
	/*
	 * ->ecxt will never be updated once getting set until the class
	 * gets removed.
	 */
	if (ie->ecxt)
		DEPT_WARN_ON(1);
	else
		WRITE_ONCE(ie->ecxt, get_ecxt(e));
}

static inline void stale_iwait(struct dept_iwait *iw)
{
	if (iw->wait)
		put_wait(iw->wait);

	WRITE_ONCE(iw->wait, NULL);
	WRITE_ONCE(iw->staled, true);
}

static inline void set_iwait(struct dept_iwait *iw, struct dept_wait *w)
{
	/*
	 * ->wait will never be updated once getting set until the class
	 * gets removed.
	 */
	if (iw->wait)
		DEPT_WARN_ON(1);
	else
		WRITE_ONCE(iw->wait, get_wait(w));

	iw->touched = true;
}

static inline void touch_iwait(struct dept_iwait *iw)
{
	iw->touched = true;
}

static inline void untouch_iwait(struct dept_iwait *iw)
{
	iw->touched = false;
}

static inline struct dept_stack *get_current_stack(void)
{
	struct dept_stack *s = dept_task()->stack;

	return s ? get_stack(s) : NULL;
}

static inline void prepare_current_stack(void)
{
	struct dept_stack *s = dept_task()->stack;

	/*
	 * The dept_stack is already ready.
	 */
	if (s && !stack_consumed(s)) {
		s->nr = 0;
		return;
	}

	if (s)
		put_stack(s);

	s = dept_task()->stack = new_stack();
	if (!s)
		return;

	get_stack(s);
	del_stack(s);
}

static void save_current_stack(int skip)
{
	struct dept_stack *s = dept_task()->stack;

	if (!s)
		return;
	if (valid_stack(s))
		return;

	s->nr = stack_trace_save(s->raw, DEPT_MAX_STACK_ENTRY, skip);
}

static void finish_current_stack(void)
{
	struct dept_stack *s = dept_task()->stack;

	if (stack_consumed(s))
		save_current_stack(2);
}

/*
 * FIXME: For now, disable LOCKDEP while DEPT is working.
 *
 * Both LOCKDEP and DEPT report it on a deadlock detection using
 * printk taking the risk of another deadlock that might be caused by
 * locks of console or printk between inside and outside of them.
 *
 * For DEPT, it's no problem since multiple reports are allowed. But it
 * would be a bad idea for LOCKDEP since it will stop even on a singe
 * report. So we need to prevent LOCKDEP from its reporting the risk
 * DEPT would take when reporting something.
 */
#include <linux/lockdep.h>

void dept_off(void)
{
	dept_task()->recursive++;
	lockdep_off();
}

void dept_on(void)
{
	dept_task()->recursive--;
	lockdep_on();
}

static inline unsigned long dept_enter(void)
{
	unsigned long flags;

	raw_local_irq_save(flags);
	dept_off();
	prepare_current_stack();
	return flags;
}

static inline void dept_exit(unsigned long flags)
{
	finish_current_stack();
	dept_on();
	raw_local_irq_restore(flags);
}

/*
 * NOTE: Must be called with dept_lock held.
 */
static struct dept_dep *__add_dep(struct dept_ecxt *e,
				  struct dept_wait *w)
{
	struct dept_dep *d;

	if (!valid_class(e->class) || !valid_class(w->class))
		return NULL;

	if (lookup_dep(e->class, w->class))
		return NULL;

	d = new_dep();
	if (unlikely(!d))
		return NULL;

	d->ecxt = get_ecxt(e);
	d->wait = get_wait(w);

	/*
	 * Add the dependency into hash and graph.
	 */
	hash_add_dep(d);
	list_add(&d->dep_node, &dep_fc(d)->dep_head);
	list_add(&d->dep_rev_node, &dep_tc(d)->dep_rev_head);
	return d;
}

static enum bfs_ret cb_check_dl(struct dept_dep *d,
				void *in, void **out)
{
	struct dept_dep *new = (struct dept_dep *)in;

	/*
	 * initial condition for this BFS search
	 */
	if (!d) {
		dep_tc(new)->bfs_parent = dep_fc(new);

		if (dep_tc(new) != dep_fc(new))
			return BFS_CONTINUE;

		/*
		 * AA circle does not make additional deadlock. We don't
		 * have to continue this BFS search.
		 */
		print_circle(dep_tc(new));
		return BFS_DONE;
	}

	/*
	 * Allow multiple reports.
	 */
	if (dep_tc(d) == dep_fc(new))
		print_circle(dep_tc(new));

	return BFS_CONTINUE;
}

/*
 * This function is actually in charge of reporting.
 */
static inline void check_dl_bfs(struct dept_dep *d)
{
	bfs(dep_tc(d), cb_check_dl, (void *)d, NULL);
}

static enum bfs_ret cb_find_iw(struct dept_dep *d, void *in, void **out)
{
	int irq = *(int *)in;
	struct dept_class *fc;
	struct dept_iwait *iw;

	if (DEPT_WARN_ON(!out))
		return BFS_DONE;

	/*
	 * initial condition for this BFS search
	 */
	if (!d)
		return BFS_CONTINUE_REV;

	fc = dep_fc(d);
	iw = iwait(fc, irq);

	/*
	 * If any parent's ->wait was set, then the children would've
	 * been touched.
	 */
	if (!iw->touched)
		return BFS_SKIP;

	if (!iw->wait)
		return BFS_CONTINUE_REV;

	*out = iw;
	return BFS_DONE;
}

static struct dept_iwait *find_iw_bfs(struct dept_class *c, int irq)
{
	struct dept_iwait *iw = iwait(c, irq);
	struct dept_iwait *found = NULL;

	if (iw->wait)
		return iw;

	/*
	 * '->touched == false' guarantees there's no parent that has
	 * been set ->wait.
	 */
	if (!iw->touched)
		return NULL;

	bfs(c, cb_find_iw, (void *)&irq, (void **)&found);

	if (found)
		return found;

	untouch_iwait(iw);
	return NULL;
}

static enum bfs_ret cb_touch_iw_find_ie(struct dept_dep *d, void *in,
					void **out)
{
	int irq = *(int *)in;
	struct dept_class *tc;
	struct dept_iecxt *ie;
	struct dept_iwait *iw;

	if (DEPT_WARN_ON(!out))
		return BFS_DONE;

	/*
	 * initial condition for this BFS search
	 */
	if (!d)
		return BFS_CONTINUE;

	tc = dep_tc(d);
	ie = iecxt(tc, irq);
	iw = iwait(tc, irq);

	touch_iwait(iw);

	if (!ie->ecxt)
		return BFS_CONTINUE;

	if (!*out)
		*out = ie;

	return BFS_CONTINUE;
}

static struct dept_iecxt *touch_iw_find_ie_bfs(struct dept_class *c,
					       int irq)
{
	struct dept_iecxt *ie = iecxt(c, irq);
	struct dept_iwait *iw = iwait(c, irq);
	struct dept_iecxt *found = ie->ecxt ? ie : NULL;

	touch_iwait(iw);
	bfs(c, cb_touch_iw_find_ie, (void *)&irq, (void **)&found);
	return found;
}

/*
 * Should be called with dept_lock held.
 */
static void __add_idep(struct dept_iecxt *ie, struct dept_iwait *iw)
{
	struct dept_dep *new;

	/*
	 * There's nothing to do.
	 */
	if (!ie || !iw || !ie->ecxt || !iw->wait)
		return;

	new = __add_dep(ie->ecxt, iw->wait);

	/*
	 * Deadlock detected. Let check_dl_bfs() report it.
	 */
	if (new) {
		check_dl_bfs(new);
		stale_iecxt(ie);
		stale_iwait(iw);
	}

	/*
	 * If !new, it would be the case of lack of object resource.
	 * Just let it go and get checked by other chances. Retrying is
	 * meaningless in that case.
	 */
}

static void set_check_iecxt(struct dept_class *c, int irq,
			    struct dept_ecxt *e)
{
	struct dept_iecxt *ie = iecxt(c, irq);

	set_iecxt(ie, e);
	__add_idep(ie, find_iw_bfs(c, irq));
}

static void set_check_iwait(struct dept_class *c, int irq,
			    struct dept_wait *w)
{
	struct dept_iwait *iw = iwait(c, irq);

	set_iwait(iw, w);
	__add_idep(touch_iw_find_ie_bfs(c, irq), iw);
}

static void add_iecxt(struct dept_class *c, int irq, struct dept_ecxt *e,
		      bool stack)
{
	/*
	 * This access is safe since we ensure e->class has set locally.
	 */
	struct dept_task *dt = dept_task();
	struct dept_iecxt *ie = iecxt(c, irq);

	if (unlikely(READ_ONCE(ie->staled)))
		return;

	/*
	 * Skip add_iecxt() if ie->ecxt has ever been set at least once.
	 * Which means it has a valid ->ecxt or been staled.
	 */
	if (READ_ONCE(ie->ecxt))
		return;

	if (unlikely(!dept_lock()))
		return;

	if (unlikely(ie->staled))
		goto unlock;
	if (ie->ecxt)
		goto unlock;

	e->enirqf |= (1UL << irq);

	/*
	 * Should be NULL since it's the first time that these
	 * enirq_{ip,stack}[irq] have ever set.
	 */
	DEPT_WARN_ON(e->enirq_ip[irq]);
	DEPT_WARN_ON(e->enirq_stack[irq]);

	e->enirq_ip[irq] = dt->enirq_ip[irq];
	e->enirq_stack[irq] = stack ? get_current_stack() : NULL;

	set_check_iecxt(c, irq, e);
unlock:
	dept_unlock();
}

static void add_iwait(struct dept_class *c, int irq, struct dept_wait *w)
{
	struct dept_iwait *iw = iwait(c, irq);

	if (unlikely(READ_ONCE(iw->staled)))
		return;

	/*
	 * Skip add_iwait() if iw->wait has ever been set at least once.
	 * Which means it has a valid ->wait or been staled.
	 */
	if (READ_ONCE(iw->wait))
		return;

	if (unlikely(!dept_lock()))
		return;

	if (unlikely(iw->staled))
		goto unlock;
	if (iw->wait)
		goto unlock;

	w->irqf |= (1UL << irq);

	/*
	 * Should be NULL since it's the first time that these
	 * irq_{ip,stack}[irq] have ever set.
	 */
	DEPT_WARN_ON(w->irq_ip[irq]);
	DEPT_WARN_ON(w->irq_stack[irq]);

	w->irq_ip[irq] = w->wait_ip;
	w->irq_stack[irq] = get_current_stack();

	set_check_iwait(c, irq, w);
unlock:
	dept_unlock();
}

static inline struct dept_wait_hist *hist(int pos)
{
	struct dept_task *dt = dept_task();

	return dt->wait_hist + (pos % DEPT_MAX_WAIT_HIST);
}

static inline int hist_pos_next(void)
{
	struct dept_task *dt = dept_task();

	return dt->wait_hist_pos % DEPT_MAX_WAIT_HIST;
}

static inline void hist_advance(void)
{
	struct dept_task *dt = dept_task();

	dt->wait_hist_pos++;
	dt->wait_hist_pos %= DEPT_MAX_WAIT_HIST;
}

static inline struct dept_wait_hist *new_hist(void)
{
	struct dept_wait_hist *wh = hist(hist_pos_next());

	hist_advance();
	return wh;
}

static void add_hist(struct dept_wait *w, unsigned int wg, unsigned int ctxt_id)
{
	struct dept_wait_hist *wh = new_hist();

	if (likely(wh->wait))
		put_wait(wh->wait);

	wh->wait = get_wait(w);
	wh->wgen = wg;
	wh->ctxt_id = ctxt_id;
}

/*
 * Should be called after setting up e's iecxt and w's iwait.
 */
static void add_dep(struct dept_ecxt *e, struct dept_wait *w)
{
	struct dept_class *fc = e->class;
	struct dept_class *tc = w->class;
	struct dept_dep *d;
	int i;

	if (lookup_dep(fc, tc))
		return;

	if (unlikely(!dept_lock()))
		return;

	/*
	 * __add_dep() will lookup_dep() again with lock held.
	 */
	d = __add_dep(e, w);
	if (d) {
		check_dl_bfs(d);

		for (i = 0; i < DEPT_IRQS_NR; i++) {
			struct dept_iwait *fiw = iwait(fc, i);
			struct dept_iecxt *found_ie;
			struct dept_iwait *found_iw;

			/*
			 * '->touched == false' guarantees there's no
			 * parent that has been set ->wait.
			 */
			if (!fiw->touched)
				continue;

			/*
			 * find_iw_bfs() will untouch the iwait if
			 * not found.
			 */
			found_iw = find_iw_bfs(fc, i);

			if (!found_iw)
				continue;

			found_ie = touch_iw_find_ie_bfs(tc, i);
			__add_idep(found_ie, found_iw);
		}
	}
	dept_unlock();
}

static atomic_t wgen = ATOMIC_INIT(1);

static void add_wait(struct dept_class *c, unsigned long ip,
		     const char *w_fn, int ne)
{
	struct dept_task *dt = dept_task();
	struct dept_wait *w;
	unsigned int wg = 0U;
	int irq;
	int i;

	w = new_wait();
	if (unlikely(!w))
		return;

	WRITE_ONCE(w->class, get_class(c));
	w->wait_ip = ip;
	w->wait_fn = w_fn;
	w->wait_stack = get_current_stack();

	irq = cur_irq();
	if (irq < DEPT_IRQS_NR)
		add_iwait(c, irq, w);

	/*
	 * Avoid adding dependency between user aware nested ecxt and
	 * wait.
	 */
	for (i = dt->ecxt_held_pos - 1; i >= 0; i--) {
		struct dept_ecxt_held *eh;

		eh = dt->ecxt_held + i;
		if (eh->ecxt->class != c || eh->nest == ne)
			break;
	}

	for (; i >= 0; i--) {
		struct dept_ecxt_held *eh;

		eh = dt->ecxt_held + i;
		add_dep(eh->ecxt, w);
	}

	if (!wait_consumed(w) && !rich_stack) {
		if (w->wait_stack)
			put_stack(w->wait_stack);
		w->wait_stack = NULL;
	}

	/*
	 * Avoid zero wgen.
	 */
	wg = atomic_inc_return(&wgen) ?: atomic_inc_return(&wgen);
	add_hist(w, wg, cur_ctxt_id());

	del_wait(w);
}

static void add_ecxt(void *obj, struct dept_class *c, unsigned long ip,
		     const char *c_fn, const char *e_fn, int ne)
{
	struct dept_task *dt = dept_task();
	struct dept_ecxt_held *eh;
	struct dept_ecxt *e;
	unsigned long irqf;
	int irq;

	if (DEPT_WARN_ON(dt->ecxt_held_pos == DEPT_MAX_ECXT_HELD))
		return;

	e = new_ecxt();
	if (unlikely(!e))
		return;

	e->class = get_class(c);
	e->ecxt_ip = ip;
	e->ecxt_stack = ip && rich_stack ? get_current_stack() : NULL;
	e->event_fn = e_fn;
	e->ecxt_fn = c_fn;

	eh = dt->ecxt_held + (dt->ecxt_held_pos++);
	eh->ecxt = get_ecxt(e);
	eh->key = (unsigned long)obj;
	eh->wgen = atomic_read(&wgen);
	eh->nest = ne;

	irqf = cur_enirqf();
	for_each_set_bit(irq, &irqf, DEPT_IRQS_NR)
		add_iecxt(c, irq, e, false);

	del_ecxt(e);
}

static int find_ecxt_pos(unsigned long key, bool newfirst)
{
	struct dept_task *dt = dept_task();
	int i;

	if (newfirst) {
		for (i = dt->ecxt_held_pos - 1; i >= 0; i--)
			if (dt->ecxt_held[i].key == key)
				return i;
	} else {
		for (i = 0; i < dt->ecxt_held_pos; i++)
			if (dt->ecxt_held[i].key == key)
				return i;
	}
	return -1;
}

static void pop_ecxt(void *obj)
{
	struct dept_task *dt = dept_task();
	unsigned long key = (unsigned long)obj;
	int pos;
	int i;

	/*
	 * TODO: Warn on pos == -1.
	 */
	pos = find_ecxt_pos(key, true);
	if (pos == -1)
		return;

	put_ecxt(dt->ecxt_held[pos].ecxt);
	dt->ecxt_held_pos--;

	for (i = pos; i < dt->ecxt_held_pos; i++)
		dt->ecxt_held[i] = dt->ecxt_held[i + 1];
}

static inline bool good_hist(struct dept_wait_hist *wh, unsigned int wg)
{
	return wh->wait != NULL && before(wg, wh->wgen);
}

/*
 * Binary-search the ring buffer for the earliest valid wait.
 */
static int find_hist_pos(unsigned int wg)
{
	int oldest;
	int l;
	int r;
	int pos;

	oldest = hist_pos_next();
	if (unlikely(good_hist(hist(oldest), wg))) {
		DEPT_WARN_ONCE("Need to expand the ring buffer.\n");
		return oldest;
	}

	l = oldest + 1;
	r = oldest + DEPT_MAX_WAIT_HIST - 1;
	for (pos = (l + r) / 2; l <= r; pos = (l + r) / 2) {
		struct dept_wait_hist *p = hist(pos - 1);
		struct dept_wait_hist *wh = hist(pos);

		if (!good_hist(p, wg) && good_hist(wh, wg))
			return pos % DEPT_MAX_WAIT_HIST;
		if (good_hist(wh, wg))
			r = pos - 1;
		else
			l = pos + 1;
	}
	return -1;
}

static void do_event(void *obj, struct dept_class *c, unsigned int wg,
		     unsigned long ip)
{
	struct dept_task *dt = dept_task();
	struct dept_wait_hist *wh;
	struct dept_ecxt_held *eh;
	unsigned long key = (unsigned long)obj;
	unsigned int ctxt_id;
	int end;
	int pos;
	int i;

	/*
	 * The event was triggered before wait.
	 */
	if (!wg)
		return;

	pos = find_ecxt_pos(key, false);
	if (pos == -1)
		return;

	eh = dt->ecxt_held + pos;
	eh->ecxt->event_ip = ip;
	eh->ecxt->event_stack = get_current_stack();

	/*
	 * The ecxt already has done what it needs.
	 */
	if (!before(wg, eh->wgen))
		return;

	pos = find_hist_pos(wg);
	if (pos == -1)
		return;

	ctxt_id = cur_ctxt_id();
	end = hist_pos_next();
	end = end > pos ? end : end + DEPT_MAX_WAIT_HIST;
	for (wh = hist(pos); pos < end; wh = hist(++pos)) {
		if (wh->ctxt_id == ctxt_id)
			add_dep(eh->ecxt, wh->wait);
		if (!before(wh->wgen, eh->wgen))
			break;
	}

	for (i = 0; i < DEPT_IRQS_NR; i++) {
		struct dept_ecxt *e;

		if (before(dt->wgen_enirq[i], wg))
			continue;

		e = eh->ecxt;
		add_iecxt(e->class, i, e, false);
	}
}

static void del_dep_rcu(struct rcu_head *rh)
{
	struct dept_dep *d = container_of(rh, struct dept_dep, rh);

	preempt_disable();
	del_dep(d);
	preempt_enable();
}

/*
 * NOTE: Must be called with dept_lock held.
 */
static void disconnect_class(struct dept_class *c)
{
	struct dept_dep *d, *n;
	int i;

	list_for_each_entry_safe(d, n, &c->dep_head, dep_node) {
		list_del_rcu(&d->dep_node);
		list_del_rcu(&d->dep_rev_node);
		hash_del_dep(d);
		call_rcu(&d->rh, del_dep_rcu);
	}

	list_for_each_entry_safe(d, n, &c->dep_rev_head, dep_rev_node) {
		list_del_rcu(&d->dep_node);
		list_del_rcu(&d->dep_rev_node);
		hash_del_dep(d);
		call_rcu(&d->rh, del_dep_rcu);
	}

	for (i = 0; i < DEPT_IRQS_NR; i++) {
		stale_iecxt(iecxt(c, i));
		stale_iwait(iwait(c, i));
	}
}

/*
 * IRQ context control
 * =====================================================================
 * Whether a wait is in {hard,soft}-IRQ context or whether
 * {hard,soft}-IRQ has been enabled on the way to an event is very
 * important to check dependency. All those things should be tracked.
 */

static inline unsigned long cur_enirqf(void)
{
	struct dept_task *dt = dept_task();
	int he = dt->hardirqs_enabled;
	int se = dt->softirqs_enabled;

	if (he)
		return DEPT_HIRQF | (se ? DEPT_SIRQF : 0UL);
	return 0UL;
}

static inline int cur_irq(void)
{
	if (lockdep_softirq_context(current))
		return DEPT_SIRQ;
	if (lockdep_hardirq_context())
		return DEPT_HIRQ;
	return DEPT_IRQS_NR;
}

static inline unsigned int cur_ctxt_id(void)
{
	struct dept_task *dt = dept_task();
	int irq = cur_irq();

	/*
	 * Normal process context
	 */
	if (irq == DEPT_IRQS_NR)
		return 0U;

	return dt->irq_id[irq] | (1UL << irq);
}

static void enirq_transition(int irq)
{
	struct dept_task *dt = dept_task();
	int i;

	/*
	 * READ wgen >= wgen of an event with IRQ enabled has been
	 * observed on the way to the event means, the IRQ can cut in
	 * within the ecxt. Used for cross-event detection.
	 *
	 *    wait context	event context(ecxt)
	 *    ------------	-------------------
	 *    wait event
	 *       WRITE wgen
	 *			observe IRQ enabled
	 *			   READ wgen
	 *			   keep the wgen locally
	 *
	 *			on the event
	 *			   check the local wgen
	 */
	dt->wgen_enirq[irq] = atomic_read(&wgen);

	for (i = dt->ecxt_held_pos - 1; i >= 0; i--) {
		struct dept_ecxt_held *eh;
		struct dept_ecxt *e;

		eh = dt->ecxt_held + i;
		e = eh->ecxt;
		add_iecxt(e->class, irq, e, true);
	}
}

static void enirq_update(unsigned long ip)
{
	struct dept_task *dt = dept_task();
	unsigned long irqf;
	unsigned long prev;
	int irq;

	prev = dt->eff_enirqf;
	irqf = cur_enirqf();
	dt->eff_enirqf = irqf;

	/*
	 * Do enirq_transition() only on an OFF -> ON transition.
	 */
	for_each_set_bit(irq, &irqf, DEPT_IRQS_NR) {
		if (prev & (1UL << irq))
			continue;

		dt->enirq_ip[irq] = ip;
		enirq_transition(irq);
	}
}

/*
 * Ensure it has been called on OFF -> ON transition.
 */
void dept_enable_softirq(unsigned long ip)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	if (DEPT_WARN_ON(early_boot_irqs_disabled))
		goto exit;

	if (DEPT_WARN_ON(!irqs_disabled()))
		goto exit;

	dt->softirqs_enabled = true;
	enirq_update(ip);
exit:
	dept_exit(flags);
}

/*
 * Ensure it has been called on OFF -> ON transition.
 */
void dept_enable_hardirq(unsigned long ip)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	if (DEPT_WARN_ON(early_boot_irqs_disabled))
		goto exit;

	if (DEPT_WARN_ON(!irqs_disabled()))
		goto exit;

	dt->hardirqs_enabled = true;
	enirq_update(ip);
exit:
	dept_exit(flags);
}

/*
 * Ensure it has been called on ON -> OFF transition.
 */
void dept_disable_softirq(unsigned long ip)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	if (DEPT_WARN_ON(!irqs_disabled()))
		goto exit;

	dt->softirqs_enabled = false;
	enirq_update(ip);
exit:
	dept_exit(flags);
}

/*
 * Ensure it has been called on ON -> OFF transition.
 */
void dept_disable_hardirq(unsigned long ip)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	if (DEPT_WARN_ON(!irqs_disabled()))
		goto exit;

	dt->hardirqs_enabled = false;
	enirq_update(ip);
exit:
	dept_exit(flags);
}

/*
 * Ensure it's the outmost softirq context.
 */
void dept_softirq_enter(void)
{
	struct dept_task *dt = dept_task();

	dt->irq_id[DEPT_SIRQ] += (1UL << DEPT_IRQS_NR);
}

/*
 * Ensure it's the outmost hardirq context.
 */
void dept_hardirq_enter(void)
{
	struct dept_task *dt = dept_task();

	dt->irq_id[DEPT_HIRQ] += (1UL << DEPT_IRQS_NR);
}

/*
 * DEPT API
 * =====================================================================
 * Main DEPT APIs.
 */

static inline void clean_classes_cache(struct dept_key *k)
{
	int i;

	for (i = 0; i < DEPT_MAX_SUBCLASSES_CACHE; i++)
		k->classes[i] = NULL;
}

void dept_map_init(struct dept_map *m, struct dept_key *k, int sub,
		   const char *n)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	if (DEPT_WARN_ON(sub < 0 || sub >= DEPT_MAX_SUBCLASSES_USR)) {
		m->nocheck = true;
		goto exit;
	}

	if (m->keys != k)
		m->keys = k;
	clean_classes_cache(&m->keys_local);

	m->sub_usr = sub;
	m->name = n;
	m->wgen = 0U;
	m->nocheck = false;
exit:
	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_map_init);

void dept_map_reinit(struct dept_map *m)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	if (m->nocheck)
		return;

	flags = dept_enter();

	clean_classes_cache(&m->keys_local);
	m->wgen = 0U;

	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_map_reinit);

void dept_map_nocheck(struct dept_map *m)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	m->nocheck = true;

	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_map_nocheck);

static LIST_HEAD(classes);

static inline bool within(const void *addr, void *start, unsigned long size)
{
	return addr >= start && addr < start + size;
}

void dept_free_range(void *start, unsigned int sz)
{
	struct dept_task *dt = dept_task();
	struct dept_class *c, *n;
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	/*
	 * dept_free_range() should not fail.
	 *
	 * FIXME: Should be fixed if dept_free_range() causes deadlock
	 * with dept_lock().
	 */
	while (unlikely(!dept_lock()))
		cpu_relax();

	list_for_each_entry_safe(c, n, &classes, all_node) {
		if (!within((void *)c->key, start, sz) &&
		    !within(c->name, start, sz))
			continue;

		hash_del_class(c);
		disconnect_class(c);
		list_del(&c->all_node);
		inval_class(c);

		/*
		 * Actual deletion will happen on the rcu callback
		 * that has been added in disconnect_class().
		 */
		del_class(c);
	}
	dept_unlock();
	dept_exit(flags);

	/*
	 * Wait until even lockless hash_lookup_class() for the class
	 * returns NULL.
	 */
	might_sleep();
	synchronize_rcu();
}

static inline int map_sub(struct dept_map *m, int e)
{
	return m->sub_usr + e * DEPT_MAX_SUBCLASSES_USR;
}

static struct dept_class *check_new_class(struct dept_key *local,
					  struct dept_key *k, int sub,
					  const char *n)
{
	struct dept_class *c = NULL;

	if (DEPT_WARN_ON(sub >= DEPT_MAX_SUBCLASSES))
		return NULL;

	if (DEPT_WARN_ON(!k))
		return NULL;

	if (sub < DEPT_MAX_SUBCLASSES_CACHE)
		c = READ_ONCE(local->classes[sub]);

	if (c)
		return c;

	c = lookup_class((unsigned long)k->subkeys + sub);
	if (c)
		goto caching;

	if (unlikely(!dept_lock()))
		return NULL;

	c = lookup_class((unsigned long)k->subkeys + sub);
	if (unlikely(c))
		goto unlock;

	c = new_class();
	if (unlikely(!c))
		goto unlock;

	c->name = n;
	c->sub = sub;
	c->key = (unsigned long)(k->subkeys + sub);
	hash_add_class(c);
	list_add(&c->all_node, &classes);
unlock:
	dept_unlock();
caching:
	if (sub < DEPT_MAX_SUBCLASSES_CACHE && c)
		WRITE_ONCE(local->classes[sub], c);

	return c;
}

void __dept_wait(struct dept_map *m, unsigned long w_f, unsigned long ip,
		 const char *w_fn, int ne)
{
	int e;

	/*
	 * Be as conservative as possible. In case of mulitple waits for
	 * a single dept_map, we are going to keep only the last wait's
	 * wgen for simplicity - keeping all wgens seems overengineering.
	 *
	 * Of course, it might cause missing some dependencies that
	 * would rarely, probabily never, happen but it helps avoid
	 * false positive report.
	 */
	for_each_set_bit(e, &w_f, DEPT_MAX_SUBCLASSES_EVT) {
		struct dept_class *c;
		struct dept_key *k;

		k = m->keys ?: &m->keys_local;
		c = check_new_class(&m->keys_local, k,
				    map_sub(m, e), m->name);
		if (!c)
			continue;

		add_wait(c, ip, w_fn, ne);
	}
}

void dept_wait(struct dept_map *m, unsigned long w_f, unsigned long ip,
	       const char *w_fn, int ne)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	if (m->nocheck)
		return;

	flags = dept_enter();

	__dept_wait(m, w_f, ip, w_fn, ne);

	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_wait);

static inline void stage_map(struct dept_task *dt, struct dept_map *m)
{
	dt->stage_m = m;
}

static inline void unstage_map(struct dept_task *dt)
{
	dt->stage_m = NULL;
}

static inline struct dept_map *staged_map(struct dept_task *dt)
{
	return dt->stage_m;
}

void dept_stage_wait(struct dept_map *m, unsigned long w_f,
		     const char *w_fn, int ne)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	if (m->nocheck)
		return;

	flags = dept_enter();

	stage_map(dt, m);

	dt->stage_w_f = w_f;
	dt->stage_w_fn = w_fn;
	dt->stage_ne = ne;

	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_stage_wait);

void dept_clean_stage(void)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	unstage_map(dt);

	dt->stage_w_f = 0UL;
	dt->stage_w_fn = NULL;
	dt->stage_ne = 0;

	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_clean_stage);

/*
 * Always called from __schedule().
 */
void dept_ask_event_wait_commit(unsigned long ip)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;
	unsigned int wg;
	struct dept_map *m;
	unsigned long w_f;
	const char *w_fn;
	int ne;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	m = staged_map(dt);

	/*
	 * Checks if current has staged a wait before __schedule().
	 */
	if (!m)
		goto exit;

	if (m->nocheck)
		goto exit;

	w_f = dt->stage_w_f;
	w_fn = dt->stage_w_fn;
	ne = dt->stage_ne;

	/*
	 * Avoid zero wgen.
	 */
	wg = atomic_inc_return(&wgen) ?: atomic_inc_return(&wgen);
	WRITE_ONCE(m->wgen, wg);

	__dept_wait(m, w_f, ip, w_fn, ne);
exit:
	dept_exit(flags);
}

void dept_ecxt_enter(struct dept_map *m, unsigned long e_f, unsigned long ip,
		     const char *c_fn, const char *e_fn, int ne)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;
	int e;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	if (m->nocheck)
		return;

	flags = dept_enter();

	for_each_set_bit(e, &e_f, DEPT_MAX_SUBCLASSES_EVT) {
		struct dept_class *c;
		struct dept_key *k;

		k = m->keys ?: &m->keys_local;
		c = check_new_class(&m->keys_local, k,
				    map_sub(m, e), m->name);
		if (!c)
			continue;

		add_ecxt((void *)m, c, ip, c_fn, e_fn, ne);
	}
	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_ecxt_enter);

void dept_ask_event(struct dept_map *m)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;
	unsigned int wg;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	if (m->nocheck)
		return;

	flags = dept_enter();

	/*
	 * Avoid zero wgen.
	 */
	wg = atomic_inc_return(&wgen) ?: atomic_inc_return(&wgen);
	WRITE_ONCE(m->wgen, wg);

	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_ask_event);

void dept_event(struct dept_map *m, unsigned long e_f, unsigned long ip,
		const char *e_fn)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;
	int e;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	if (m->nocheck)
		return;

	flags = dept_enter();

	for_each_set_bit(e, &e_f, DEPT_MAX_SUBCLASSES_EVT) {
		struct dept_class *c;
		struct dept_key *k;

		k = m->keys ?: &m->keys_local;
		c = check_new_class(&m->keys_local, k,
				    map_sub(m, e), m->name);
		if (!c)
			continue;

		add_ecxt((void *)m, c, 0UL, NULL, e_fn, 0);
		do_event((void *)m, c, READ_ONCE(m->wgen), ip);
		pop_ecxt((void *)m);
	}
	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_event);

void dept_ecxt_exit(struct dept_map *m, unsigned long ip)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	if (m->nocheck)
		return;

	flags = dept_enter();
	pop_ecxt((void *)m);
	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_ecxt_exit);

void dept_task_exit(struct task_struct *t)
{
	struct dept_task *dt = &t->dept_task;
	int i;

	raw_local_irq_disable();

	if (dt->stack)
		put_stack(dt->stack);

	for (i = 0; i < dt->ecxt_held_pos; i++)
		put_ecxt(dt->ecxt_held[i].ecxt);

	for (i = 0; i < DEPT_MAX_WAIT_HIST; i++)
		if (dt->wait_hist[i].wait)
			put_wait(dt->wait_hist[i].wait);

	dept_off();

	raw_local_irq_enable();
}

void dept_task_init(struct task_struct *t)
{
	memset(&t->dept_task, 0x0, sizeof(struct dept_task));
}

void dept_key_init(struct dept_key *k)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;
	int sub;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	/*
	 * dept_key_init() should not fail.
	 *
	 * FIXME: Should be fixed if dept_key_init() causes deadlock
	 * with dept_lock().
	 */
	while (unlikely(!dept_lock()))
		cpu_relax();

	for (sub = 0; sub < DEPT_MAX_SUBCLASSES; sub++) {
		struct dept_class *c;

		c = lookup_class((unsigned long)k->subkeys + sub);
		if (!c)
			continue;

		DEPT_STOP("The class(%s/%d) has not been removed.\n",
			  c->name, sub);
		break;
	}

	dept_unlock();
	dept_exit(flags);
}
EXPORT_SYMBOL_GPL(dept_key_init);

void dept_key_destroy(struct dept_key *k)
{
	struct dept_task *dt = dept_task();
	unsigned long flags;
	int sub;

	if (READ_ONCE(dept_stop) || dt->recursive)
		return;

	flags = dept_enter();

	/*
	 * dept_key_destroy() should not fail.
	 *
	 * FIXME: Should be fixed if dept_key_destroy() causes deadlock
	 * with dept_lock().
	 */
	while (unlikely(!dept_lock()))
		cpu_relax();

	for (sub = 0; sub < DEPT_MAX_SUBCLASSES; sub++) {
		struct dept_class *c;

		c = lookup_class((unsigned long)k->subkeys + sub);
		if (!c)
			continue;

		hash_del_class(c);
		disconnect_class(c);
		list_del(&c->all_node);
		inval_class(c);

		/*
		 * Actual deletion will happen on the rcu callback
		 * that has been added in disconnect_class().
		 */
		del_class(c);
	}

	dept_unlock();
	dept_exit(flags);

	/*
	 * Wait until even lockless hash_lookup_class() for the class
	 * returns NULL.
	 */
	might_sleep();
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(dept_key_destroy);

static void move_llist(struct llist_head *to, struct llist_head *from)
{
	struct llist_node *first = llist_del_all(from);
	struct llist_node *last;

	if (!first)
		return;

	for (last = first; last->next; last = last->next);
	llist_add_batch(first, last, to);
}

static void migrate_per_cpu_pool(void)
{
	const int boot_cpu = 0;
	int i;

	/*
	 * The boot CPU has been using the temperal local pool so far.
	 * From now on that per_cpu areas have been ready, use the
	 * per_cpu local pool instead.
	 */
	DEPT_WARN_ON(smp_processor_id() != boot_cpu);
	for (i = 0; i < OBJECT_NR; i++) {
		struct llist_head *from;
		struct llist_head *to;

		from = &pool[i].boot_pool;
		to = per_cpu_ptr(pool[i].lpool, boot_cpu);
		move_llist(to, from);
	}
}

#define B2KB(B) ((B) / 1024)

/*
 * Should be called after setup_per_cpu_areas() and before no non-boot
 * CPUs have been on.
 */
void __init dept_init(void)
{
	size_t mem_total = 0;

	local_irq_disable();
	dept_per_cpu_ready = 1;
	migrate_per_cpu_pool();
	local_irq_enable();

#define OBJECT(id, nr) mem_total += sizeof(struct dept_##id) * nr;
	#include "dept_object.h"
#undef  OBJECT
#define HASH(id, bits) mem_total += sizeof(struct hlist_head) * (1UL << bits);
	#include "dept_hash.h"
#undef  HASH

	pr_info("DEPendency Tracker: Copyright (c) 2020 LG Electronics, Inc., Byungchul Park\n");
	pr_info("... DEPT_MAX_STACK_ENTRY: %d\n", DEPT_MAX_STACK_ENTRY);
	pr_info("... DEPT_MAX_WAIT_HIST  : %d\n", DEPT_MAX_WAIT_HIST);
	pr_info("... DEPT_MAX_ECXT_HELD  : %d\n", DEPT_MAX_ECXT_HELD);
	pr_info("... DEPT_MAX_SUBCLASSES : %d\n", DEPT_MAX_SUBCLASSES);
#define OBJECT(id, nr)							\
	pr_info("... memory used by %s: %zu KB\n",			\
	       #id, B2KB(sizeof(struct dept_##id) * nr));
	#include "dept_object.h"
#undef  OBJECT
#define HASH(id, bits)							\
	pr_info("... hash list head used by %s: %zu KB\n",		\
	       #id, B2KB(sizeof(struct hlist_head) * (1UL << bits)));
	#include "dept_hash.h"
#undef  HASH
	pr_info("... total memory used by objects and hashs: %zu KB\n", B2KB(mem_total));
	pr_info("... per task memory footprint: %zu bytes\n", sizeof(struct dept_task));
}
