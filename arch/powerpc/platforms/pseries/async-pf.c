// SPDX-License-Identifier: GPL-2.0
/*
 * Async page fault support via PAPR Expropriation/Subvention Notification
 * option(ESN)
 *
 * Copyright 2020 Bharata B Rao, IBM Corp. <bharata@linux.ibm.com>
 */

#include <linux/interrupt.h>
#include <linux/swait.h>
#include <linux/irqdomain.h>
#include <asm/machdep.h>
#include <asm/hvcall.h>
#include <asm/paca.h>

static char sns_buffer[PAGE_SIZE] __aligned(4096);
static uint16_t *esn_q = (uint16_t *)sns_buffer + 1;
static unsigned long next_eq_entry, nr_eq_entries;

#define ASYNC_PF_SLEEP_HASHBITS 8
#define ASYNC_PF_SLEEP_HASHSIZE (1<<ASYNC_PF_SLEEP_HASHBITS)

/* Controls access to SNS buffer */
static DEFINE_RAW_SPINLOCK(async_sns_guest_lock);

/* Wait queue handling is from x86 asyn-pf implementation */
struct async_pf_sleep_node {
	struct hlist_node link;
	struct swait_queue_head wq;
	u64 token;
	int cpu;
};

static struct async_pf_sleep_head {
	raw_spinlock_t lock;
	struct hlist_head list;
} async_pf_sleepers[ASYNC_PF_SLEEP_HASHSIZE];

static struct async_pf_sleep_node *_find_apf_task(struct async_pf_sleep_head *b,
						  u64 token)
{
	struct hlist_node *p;

	hlist_for_each(p, &b->list) {
		struct async_pf_sleep_node *n =
			hlist_entry(p, typeof(*n), link);
		if (n->token == token)
			return n;
	}

	return NULL;
}
static int async_pf_queue_task(u64 token, struct async_pf_sleep_node *n)
{
	u64 key = hash_64(token, ASYNC_PF_SLEEP_HASHBITS);
	struct async_pf_sleep_head *b = &async_pf_sleepers[key];
	struct async_pf_sleep_node *e;

	raw_spin_lock(&b->lock);
	e = _find_apf_task(b, token);
	if (e) {
		/* dummy entry exist -> wake up was delivered ahead of PF */
		hlist_del(&e->link);
		raw_spin_unlock(&b->lock);
		kfree(e);
		return false;
	}

	n->token = token;
	n->cpu = smp_processor_id();
	init_swait_queue_head(&n->wq);
	hlist_add_head(&n->link, &b->list);
	raw_spin_unlock(&b->lock);
	return true;
}

/*
 * Handle Expropriation notification.
 */
int handle_async_page_fault(struct pt_regs *regs, unsigned long addr)
{
	struct async_pf_sleep_node n;
	DECLARE_SWAITQUEUE(wait);
	unsigned long exp_corr_nr;

	/* Is this Expropriation notification? */
	if (!(mfspr(SPRN_SRR1) & SRR1_PROGTRAP))
		return 0;

	if (unlikely(!user_mode(regs)))
		panic("Host injected async PF in kernel mode\n");

	exp_corr_nr = be16_to_cpu(get_lppaca()->exp_corr_nr);
	if (!async_pf_queue_task(exp_corr_nr, &n))
		return 0;

	for (;;) {
		prepare_to_swait_exclusive(&n.wq, &wait, TASK_UNINTERRUPTIBLE);
		if (hlist_unhashed(&n.link))
			break;

		local_irq_enable();
		schedule();
		local_irq_disable();
	}

	finish_swait(&n.wq, &wait);
	return 1;
}

static void apf_task_wake_one(struct async_pf_sleep_node *n)
{
	hlist_del_init(&n->link);
	if (swq_has_sleeper(&n->wq))
		swake_up_one(&n->wq);
}

static void async_pf_wake_task(u64 token)
{
	u64 key = hash_64(token, ASYNC_PF_SLEEP_HASHBITS);
	struct async_pf_sleep_head *b = &async_pf_sleepers[key];
	struct async_pf_sleep_node *n;

again:
	raw_spin_lock(&b->lock);
	n = _find_apf_task(b, token);
	if (!n) {
		/*
		 * async PF was not yet handled.
		 * Add dummy entry for the token.
		 */
		n = kzalloc(sizeof(*n), GFP_ATOMIC);
		if (!n) {
			/*
			 * Allocation failed! Busy wait while other cpu
			 * handles async PF.
			 */
			raw_spin_unlock(&b->lock);
			cpu_relax();
			goto again;
		}
		n->token = token;
		n->cpu = smp_processor_id();
		init_swait_queue_head(&n->wq);
		hlist_add_head(&n->link, &b->list);
	} else {
		apf_task_wake_one(n);
	}
	raw_spin_unlock(&b->lock);
}

/*
 * Handle Subvention notification.
 */
static irqreturn_t async_pf_handler(int irq, void *dev_id)
{
	uint16_t exp_token, old;

	raw_spin_lock(&async_sns_guest_lock);
	do {
		exp_token = *(esn_q + next_eq_entry);
		if (!exp_token)
			break;

		old = arch_cmpxchg(esn_q + next_eq_entry, exp_token, 0);
		BUG_ON(old != exp_token);

		async_pf_wake_task(exp_token);
		next_eq_entry = (next_eq_entry + 1) % nr_eq_entries;
	} while (1);
	raw_spin_unlock(&async_sns_guest_lock);
	return IRQ_HANDLED;
}

static int __init pseries_async_pf_init(void)
{
	long rc;
	unsigned long ret[PLPAR_HCALL_BUFSIZE];
	unsigned int irq, cpu;
	int i;

	/* Register buffer via H_REG_SNS */
	rc = plpar_hcall(H_REG_SNS, ret, __pa(sns_buffer), PAGE_SIZE);
	if (rc != H_SUCCESS)
		return -1;

	nr_eq_entries = (PAGE_SIZE - 2) / sizeof(uint16_t);

	/* Register irq handler */
	irq = irq_create_mapping(NULL, ret[1]);
	if (!irq) {
		plpar_hcall(H_REG_SNS, ret, -1, PAGE_SIZE);
		return -1;
	}

	rc = request_irq(irq, async_pf_handler, 0, "sns-interrupt", NULL);
	if (rc < 0) {
		plpar_hcall(H_REG_SNS, ret, -1, PAGE_SIZE);
		return -1;
	}

	for (i = 0; i < ASYNC_PF_SLEEP_HASHSIZE; i++)
		raw_spin_lock_init(&async_pf_sleepers[i].lock);

	/*
	 * Enable subvention notifications from the hypervisor
	 * by setting bit 0, byte 0 of SNS buffer
	 */
	*sns_buffer |= 0x1;

	/* Enable LPPACA_EXP_INT_ENABLED in VPA */
	for_each_possible_cpu(cpu)
		lppaca_of(cpu).byte_b9 |= LPPACA_EXP_INT_ENABLED;

	pr_err("%s: Enabled Async PF\n", __func__);
	return 0;
}

machine_arch_initcall(pseries, pseries_async_pf_init);
