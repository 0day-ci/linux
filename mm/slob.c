// SPDX-License-Identifier: GPL-2.0
/*
 * SLOB Allocator: Simple List Of Blocks
 *
 * Matt Mackall <mpm@selenic.com> 12/30/03
 *
 * NUMA support by Paul Mundt, 2007.
 *
 * Rewritten by Hyeonggon Yoo <42.hyeyoo@gmail.com>, 2021
 *
 * How SLOB works:
 *
 * The core of SLOB is a simple segregated free list, with
 * support for returning aligned objects. The granularity of this
 * allocator is as little as size of pointers. This will require
 * 4 bytes on 32-bit and 8 bytes on 64-bit.
 *
 * A cache manages a linked list of pages allocated from alloc_pages()
 * per node. and within each page, there is a singly-linked list of
 * free blocks. The heap is grown on demand.
 *
 * If SLOB is asked for objects of PAGE_SIZE or larger, it calls
 * alloc_pages() directly, allocating compound pages so the page order
 * does not have to be separately tracked.
 * These objects are detected in kfree()/slob_free() because PageSlab()
 * is false for them.
 *
 * Allocation from heap is done by taking object from freelist of the page.
 * Deallocation inserts objects back into the head of a freelist.
 *
 * Allocation/Deallocation is done in constant time as SLOB does not
 * iterate list of free objects. SLOB supports slab merging to minimize
 * memory usage.
 *
 * SLAB is emulated on top of SLOB by simply calling constructors and
 * destructors for every SLAB allocation. Objects are returned with the
 * pointer size alignment unless the cache does not specify its alignment.
 * Again, objects of page-size or greater are allocated by calling
 * alloc_pages(). As SLAB objects know their size, no separate size
 * bookkeeping is necessary and there is essentially no allocation
 * space overhead, and compound pages aren't needed for multi-page
 * allocations.
 *
 * NUMA support in SLOB is fairly simplistic, pushing most of the real
 * logic down to the page allocator, and simply doing the node accounting
 * on the upper levels. In the event that a node id is explicitly
 * provided, __alloc_pages_node() with the specified node id is used
 * instead. The common case (or when the node id isn't explicitly provided)
 * will default to the current node, as per numa_node_id().
 *
 * A cache manages list of pages per node. So allocations that can be
 * satisfied from the freelist will only be done so on pages residing
 * on the same node, in order to prevent random node placement.
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/mm.h>
#include <linux/swap.h> /* struct reclaim_state */
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/kmemleak.h>

#include <trace/events/kmem.h>

#include <linux/atomic.h>

#include "slab.h"


#define for_each_object(__p, __size, __addr, __objects) \
		for (__p = __addr; \
		__p < __addr + __objects * __size; \
		__p += __size)

/*
 * slob_page_free: true for pages on free_slob_pages list.
 */
static inline int slob_page_free(struct page *sp)
{
	return PageSlobFree(sp);
}

static void set_slob_page_free(struct page *sp, struct list_head *list)
{
	list_add(&sp->slab_list, list);
	__SetPageSlobFree(sp);
}

static inline void clear_slob_page_free(struct page *sp)
{
	list_del(&sp->slab_list);
	__ClearPageSlobFree(sp);
}

static __always_inline unsigned int slob_size(size_t size, int order)
{
	return (PAGE_SIZE << order) / size;
}

static void *get_freepointer(void *objp)
{
	return (void *)(*(unsigned long *)objp);
}

static void set_freepointer(void *objp, void *fp)
{
	*(unsigned long *)(objp) = (unsigned long)fp;
}

/*
 * struct slob_rcu is inserted at the tail of allocated slob blocks, which
 * were created with a SLAB_TYPESAFE_BY_RCU slab. slob_rcu is used to free
 * the block using call_rcu.
 */
struct slob_rcu {
	struct rcu_head head;
	unsigned int size;
};


static struct page *slob_new_pages(gfp_t gfp, int order, int node)
{
	struct page *page;

#ifdef CONFIG_NUMA
	if (node != NUMA_NO_NODE)
		page = __alloc_pages_node(node, gfp, order);
	else
#endif
		page = alloc_pages(gfp, order);

	if (!page)
		return NULL;

	mod_node_page_state(page_pgdat(page), NR_SLAB_UNRECLAIMABLE_B,
			    PAGE_SIZE << order);
	return page;
}

static void slob_free_pages(const void *b, int order)
{
	struct page *sp = virt_to_page(b);

	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += 1 << order;

	mod_node_page_state(page_pgdat(sp), NR_SLAB_UNRECLAIMABLE_B,
			    -(PAGE_SIZE << order));

	sp->mapping = NULL;
	__free_pages(sp, order);
}

/*
 * slob_page_alloc() - Allocate a slob block within a given slob_page sp.
 * @sp: Page to look in.
 *
 * Tries to find a chunk of memory at least @size bytes big within @page.
 * Caller must hold slob->lock.
 *
 * Return: Pointer to memory if allocated, %NULL otherwise.
 * it is wrong if slob_alloc_page() returns NULL because a page should
 * be removed when it becomes empty.
 */
static void *slob_page_alloc(struct page *sp)
{
	void *cur;

	cur = sp->freelist;
	VM_BUG_ON(!cur);
	sp->freelist = get_freepointer(cur);
	sp->inuse++;

	return cur;
}

/*
 * slob_init_page() - Initialize a page for slab allocation
 *
 * Initializes free objects and its free pointer.
 * Caller must hold slob->lock.
 */
static void slob_init_page(struct kmem_cache *s, struct page *sp,
		int order, size_t size)
{
	void *cur, *prev, *addr = page_address(sp);
	struct slob *slob = &s->slob;
	int node = page_to_nid(sp);
	struct list_head *head = &slob->head[node];

	size = ALIGN(size, sizeof(void *));
	size = ALIGN(size, s->align);

	__SetPageSlab(sp);
	sp->objects = slob_size(size, order);
	sp->inuse = 0;
	sp->freelist = addr;
	sp->slab_cache = s;
	INIT_LIST_HEAD(&sp->slab_list);

	prev = NULL;
	for_each_object(cur, size, addr, sp->objects) {
		if (prev)
			set_freepointer(prev, cur);
		set_freepointer(cur, NULL);
		prev = cur;
	}

	set_slob_page_free(sp, head);
}

/*
 * slob_alloc: entry point into the slob allocator.
 */
static void *slob_alloc(struct kmem_cache *s, gfp_t gfp, int node)
{
	struct slob *slob;
	struct page *sp;
	struct list_head *head;
	void *objp = NULL;
	unsigned long flags;

	if (node == NUMA_NO_NODE)
		node = numa_mem_id();

	slob = &s->slob;
	head = &slob->head[node];

	spin_lock_irqsave(&slob->lock, flags);

	if (list_empty(head)) {
		spin_unlock_irqrestore(&slob->lock, flags);
		sp = slob_new_pages(gfp & ~__GFP_ZERO, 0, node);

		if (!sp)
			return NULL;

		spin_lock_irqsave(&slob->lock, flags);
		slob_init_page(s, sp, 0, s->size);
	} else
		sp = list_first_entry(head, struct page, slab_list);

	objp = slob_page_alloc(sp);

	VM_BUG_ON(((unsigned long)objp & PAGE_MASK) !=
			((unsigned long)page_address(sp) & PAGE_MASK));

	if (sp->inuse == sp->objects)
		clear_slob_page_free(sp);

	spin_unlock_irqrestore(&slob->lock, flags);

	if (unlikely(gfp & __GFP_ZERO))
		memset(objp, 0, s->size);
	return objp;
}

/*
 * slob_free: entry point into the slob allocator.
 */
static void slob_free(struct kmem_cache *c, void *block)
{
	struct page *sp;
	struct slob *slob;
	unsigned long flags;
	int node;

	if (unlikely(ZERO_OR_NULL_PTR(block)))
		return;

	sp = virt_to_page(block);
	slob = &c->slob;
	node = page_to_nid(sp);

	spin_lock_irqsave(&slob->lock, flags);

	set_freepointer(block, sp->freelist);
	sp->freelist = block;
	sp->inuse--;

	VM_BUG_ON(((unsigned long)block & PAGE_MASK) !=
			((unsigned long)page_address(sp) & PAGE_MASK));

	if (!sp->inuse) {
		/* Go directly to page allocator. Do not pass slob allocator */
		if (slob_page_free(sp))
			clear_slob_page_free(sp);
		spin_unlock_irqrestore(&slob->lock, flags);

		__ClearPageSlab(sp);
		slob_free_pages(block, 0);
		return;
	}

	if (!slob_page_free(sp))
		set_slob_page_free(sp, &slob->head[node]);

	spin_unlock_irqrestore(&slob->lock, flags);
}

#ifdef CONFIG_PRINTK
void kmem_obj_info(struct kmem_obj_info *kpp, void *object, struct page *page)
{
	kpp->kp_ptr = object;
	kpp->kp_page = page;
}
#endif

/*
 * End of slob allocator proper. Begin kmem_cache_alloc and kmalloc frontend.
 */

static __always_inline void *
__do_kmalloc_node(size_t size, gfp_t gfp, int node, unsigned long caller)
{
	void *ret;
	struct page *sp;

	gfp &= gfp_allowed_mask;

	might_alloc(gfp);

	if (unlikely(!size))
		return ZERO_SIZE_PTR;

	size = ALIGN(size, ARCH_KMALLOC_MINALIGN);

	if (size < PAGE_SIZE) {
		struct kmem_cache *s;

		s = kmalloc_slab(size, gfp);
		ret = slob_alloc(s, gfp, node);
		trace_kmalloc_node(caller, ret,
				   size, s->size, gfp, node);
	} else {
		unsigned int order = get_order(size);

		if (likely(order))
			gfp |= __GFP_COMP;

		sp = slob_new_pages(gfp, order, node);
		if (!sp)
			return NULL;

		ret = page_address(sp);
		trace_kmalloc_node(caller, ret,
				   size, PAGE_SIZE << order, gfp, node);
	}

	kmemleak_alloc(ret, size, 1, gfp);
	return ret;
}

void *__kmalloc(size_t size, gfp_t gfp)
{
	return __do_kmalloc_node(size, gfp, NUMA_NO_NODE, _RET_IP_);
}
EXPORT_SYMBOL(__kmalloc);

void *__kmalloc_track_caller(size_t size, gfp_t gfp, unsigned long caller)
{
	return __do_kmalloc_node(size, gfp, NUMA_NO_NODE, caller);
}
EXPORT_SYMBOL(__kmalloc_track_caller);

#ifdef CONFIG_NUMA
void *__kmalloc_node_track_caller(size_t size, gfp_t gfp,
					int node, unsigned long caller)
{
	return __do_kmalloc_node(size, gfp, node, caller);
}
EXPORT_SYMBOL(__kmalloc_node_track_caller);
#endif

void kfree(const void *block)
{
	struct page *sp;

	trace_kfree(_RET_IP_, block);

	if (unlikely(ZERO_OR_NULL_PTR(block)))
		return;
	kmemleak_free(block);

	sp = virt_to_page(block);

	if (PageSlab(sp))
		slob_free(sp->slab_cache, (void *)block);
	else
		slob_free_pages(block, compound_order(sp));
}
EXPORT_SYMBOL(kfree);

size_t __ksize(const void *block)
{
	struct page *sp;

	BUG_ON(!block);

	if (unlikely(block == ZERO_SIZE_PTR))
		return 0;

	sp = virt_to_page(block);
	if (unlikely(!PageSlab(sp)))
		return page_size(sp);

	return sp->slab_cache->size;
}
EXPORT_SYMBOL(__ksize);

int __kmem_cache_create(struct kmem_cache *c, slab_flags_t flags)
{
	int node;
	struct slob *slob;

	if (flags & SLAB_TYPESAFE_BY_RCU) {
		/* leave room for rcu footer at the end of object */
		c->size += sizeof(struct slob_rcu);
	}

	slob = &c->slob;
	spin_lock_init(&slob->lock);
	for_each_node(node) {
		INIT_LIST_HEAD(&slob->head[node]);
	}

	c->flags = flags;
	return 0;
}

static void *slob_alloc_node(struct kmem_cache *c, gfp_t flags, int node)
{
	void *b;
	struct page *sp;

	flags &= gfp_allowed_mask;

	might_alloc(flags);

	if (c->size < PAGE_SIZE) {
		b = slob_alloc(c, flags, node);
		trace_kmem_cache_alloc_node(_RET_IP_, b, c->object_size,
					    c->size, flags, node);
	} else {
		unsigned int order = get_order(c->size);

		sp = slob_new_pages(flags, order, node);
		if (!sp)
			return NULL;

		b = page_address(sp);
		trace_kmem_cache_alloc_node(_RET_IP_, b, c->object_size,
					    PAGE_SIZE << order, flags, node);
	}

	if (b && c->ctor) {
		WARN_ON_ONCE(flags & __GFP_ZERO);
		c->ctor(b);
	}

	kmemleak_alloc_recursive(b, c->size, 1, c->flags, flags);
	return b;
}

void *kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags)
{
	return slob_alloc_node(cachep, flags, NUMA_NO_NODE);
}
EXPORT_SYMBOL(kmem_cache_alloc);

#ifdef CONFIG_NUMA
void *__kmalloc_node(size_t size, gfp_t gfp, int node)
{
	return __do_kmalloc_node(size, gfp, node, _RET_IP_);
}
EXPORT_SYMBOL(__kmalloc_node);

void *kmem_cache_alloc_node(struct kmem_cache *cachep, gfp_t gfp, int node)
{
	return slob_alloc_node(cachep, gfp, node);
}
EXPORT_SYMBOL(kmem_cache_alloc_node);
#endif

static void __kmem_cache_free(struct kmem_cache *c, void *b)
{
	struct page *sp = virt_to_page(b);

	if (PageSlab(sp))
		slob_free(c, b);
	else
		slob_free_pages(b, get_order(c->size));
}

static void kmem_rcu_free(struct rcu_head *head)
{
	struct slob_rcu *slob_rcu = (struct slob_rcu *)head;
	void *b = (void *)slob_rcu - (slob_rcu->size - sizeof(struct slob_rcu));
	struct page *sp = virt_to_page(b);

	__kmem_cache_free(sp->slab_cache, b);
}

void kmem_cache_free(struct kmem_cache *c, void *b)
{
	kmemleak_free_recursive(b, c->flags);

	if (unlikely(c->flags & SLAB_TYPESAFE_BY_RCU)) {
		struct slob_rcu *slob_rcu;
		slob_rcu = b + (c->size - sizeof(struct slob_rcu));
		slob_rcu->size = c->size;
		call_rcu(&slob_rcu->head, kmem_rcu_free);
	} else
		__kmem_cache_free(c, b);

	trace_kmem_cache_free(_RET_IP_, b, c->name);
}
EXPORT_SYMBOL(kmem_cache_free);

void kmem_cache_free_bulk(struct kmem_cache *s, size_t size, void **p)
{
	__kmem_cache_free_bulk(s, size, p);
}
EXPORT_SYMBOL(kmem_cache_free_bulk);

int kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t size,
								void **p)
{
	return __kmem_cache_alloc_bulk(s, flags, size, p);
}
EXPORT_SYMBOL(kmem_cache_alloc_bulk);

int __kmem_cache_shutdown(struct kmem_cache *c)
{
	/* No way to check for remaining objects */
	return 0;
}

void __kmem_cache_release(struct kmem_cache *c)
{
}

int __kmem_cache_shrink(struct kmem_cache *d)
{
	return 0;
}

struct kmem_cache kmem_cache_boot = {
	.name = "kmem_cache",
	.size = sizeof(struct kmem_cache),
	.flags = SLAB_PANIC,
	.align = ARCH_KMALLOC_MINALIGN,
};

void __init kmem_cache_init(void)
{

	kmem_cache = &kmem_cache_boot;
	__kmem_cache_create(kmem_cache, kmem_cache->flags);

	setup_kmalloc_cache_index_table();
	create_kmalloc_caches(0);

	slab_state = UP;
}

void __init kmem_cache_init_late(void)
{
	slab_state = FULL;
}

struct kmem_cache *
__kmem_cache_alias(const char *name, unsigned int size, unsigned int align,
		   slab_flags_t flags, void (*ctor)(void *))
{
	struct kmem_cache *s = NULL;

	s = find_mergeable(size, align, flags, name, ctor);
	if (s) {
		s->refcount++;
		s->object_size = max(s->object_size, size);
	}

	return s;
}

slab_flags_t kmem_cache_flags(unsigned int object_size,
	slab_flags_t flags, const char *name)
{
	return flags;
}

#ifdef CONFIG_TRACING
void *kmem_cache_alloc_trace(struct kmem_cache *s, gfp_t gfp, size_t size)
{
	void *ret = slob_alloc_node(s, gfp, NUMA_NO_NODE);

	trace_kmalloc(_RET_IP, ret, size, s->size, gfp);
	return ret
}

#ifdef CONFIG_NUMA
void *kmem_cache_alloc_node_trace(struct kmem_cache *s,
					   gfp_t gfp,
					   int node, size_t size)
{
	void *ret = slob_alloc_node(s, gfp, node);

	trace_kmalloc_node(_RET_IP, ret,
			   size, s->size, gfp, node);
	return ret;
}
#endif

#endif
