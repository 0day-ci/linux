// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <linux/netfilter.h>

#include <net/netfilter/nf_queue.h>

/* BPF translator for netfilter hooks.
 *
 * Copyright (c) 2021 Red Hat GmbH
 *
 * Author: Florian Westphal <fw@strlen.de>
 *
 * Unroll nf_hook_slow interpreter loop into an equivalent bpf
 * program that can be called *instead* of nf_hook_slow().
 * This program thus has same return value as nf_hook_slow and
 * handles nfqueue and packet drops internally.
 *
 * These bpf programs are called/run from nf_hook() inline function.
 *
 * Register usage is:
 *
 * BPF_REG_0: verdict.
 * BPF_REG_1: struct nf_hook_state *
 * BPF_REG_2: reserved as arg to nf_queue()
 * BPF_REG_3: reserved as arg to nf_queue()
 *
 * Prologue storage:
 * BPF_REG_6: copy of REG_1 (original struct nf_hook_state *)
 * BPF_REG_7: copy of original state->priv value
 * BPF_REG_8: hook_index.  Inited to 0, increments on each hook call.
 */

#define JMP_INVALID 0
#define JIT_SIZE_MAX 0xffff

struct nf_hook_prog {
	struct bpf_insn *insns;
	unsigned int pos;
};

struct nf_hook_bpf_prog {
	struct rcu_head rcu_head;

	struct hlist_node node_key;
	struct hlist_node node_prog;
	u32 key;
	u16 hook_count;
	refcount_t refcnt;
	struct bpf_prog	*prog;
	unsigned long hooks[64];
};

#define NF_BPF_PROG_HT_BITS	8

/* users need to hold nf_hook_mutex */
static DEFINE_HASHTABLE(nf_bpf_progs_ht_key, NF_BPF_PROG_HT_BITS);
static DEFINE_HASHTABLE(nf_bpf_progs_ht_prog, NF_BPF_PROG_HT_BITS);

static bool emit(struct nf_hook_prog *p, struct bpf_insn insn)
{
	if (WARN_ON_ONCE(p->pos >= BPF_MAXINSNS))
		return false;

	p->insns[p->pos] = insn;
	p->pos++;
	return true;
}

static bool xlate_one_hook(struct nf_hook_prog *p,
			   const struct nf_hook_entries *e,
			   const struct nf_hook_entry *h)
{
	int width = bytes_to_bpf_size(sizeof(h->priv));

	/* if priv is NULL, the called hookfn does not use the priv member. */
	if (!h->priv)
		goto emit_hook_call;

	if (WARN_ON_ONCE(width < 0))
		return false;

	/* x = entries[s]->priv; */
	if (!emit(p, BPF_LDX_MEM(width, BPF_REG_2, BPF_REG_7,
				 (unsigned long)&h->priv - (unsigned long)e)))
		return false;

	/* state->priv = x */
	if (!emit(p, BPF_STX_MEM(width, BPF_REG_6, BPF_REG_2,
				 offsetof(struct nf_hook_state, priv))))
		return false;

emit_hook_call:
	if (!emit(p, BPF_EMIT_CALL(h->hook)))
		return false;

	/* Only advance to next hook on ACCEPT verdict.
	 * Else, skip rest and move to tail.
	 *
	 * Postprocessing patches the jump offset to the
	 * correct position, after last hook.
	 */
	if (!emit(p, BPF_JMP_IMM(BPF_JNE, BPF_REG_0, NF_ACCEPT, JMP_INVALID)))
		return false;

	return true;
}

static bool emit_mov_ptr_reg(struct nf_hook_prog *p, u8 dreg, u8 sreg)
{
	if (sizeof(void *) == sizeof(u64))
		return emit(p, BPF_MOV64_REG(dreg, sreg));
	if (sizeof(void *) == sizeof(u32))
		return emit(p, BPF_MOV32_REG(dreg, sreg));

	return false;
}

static bool do_prologue(struct nf_hook_prog *p)
{
	int width = bytes_to_bpf_size(sizeof(void *));

	if (WARN_ON_ONCE(width < 0))
		return false;

	/* argument to program is a pointer to struct nf_hook_state, in BPF_REG_1. */
	if (!emit_mov_ptr_reg(p, BPF_REG_6, BPF_REG_1))
		return false;

	if (!emit(p, BPF_LDX_MEM(width, BPF_REG_7, BPF_REG_1,
				 offsetof(struct nf_hook_state, priv))))
		return false;

	/* Could load state->hook_index here, but we don't support index > 0 for bpf call. */
	if (!emit(p, BPF_MOV32_IMM(BPF_REG_8, 0)))
		return false;

	return true;
}

static void patch_hook_jumps(struct nf_hook_prog *p)
{
	unsigned int i;

	if (!p->insns)
		return;

	for (i = 0; i < p->pos; i++) {
		if (BPF_CLASS(p->insns[i].code) != BPF_JMP)
			continue;

		if (p->insns[i].code == (BPF_EXIT | BPF_JMP))
			continue;
		if (p->insns[i].code == (BPF_CALL | BPF_JMP))
			continue;

		if (p->insns[i].off != JMP_INVALID)
			continue;
		p->insns[i].off = p->pos - i - 1;
	}
}

static bool emit_retval(struct nf_hook_prog *p, int retval)
{
	if (!emit(p, BPF_MOV32_IMM(BPF_REG_0, retval)))
		return false;

	return emit(p, BPF_EXIT_INSN());
}

static bool emit_nf_hook_slow(struct nf_hook_prog *p)
{
	int width = bytes_to_bpf_size(sizeof(void *));

	/* restore the original state->priv. */
	if (!emit(p, BPF_STX_MEM(width, BPF_REG_6, BPF_REG_7,
				 offsetof(struct nf_hook_state, priv))))
		return false;

	/* arg1 is state->skb */
	if (!emit(p, BPF_LDX_MEM(width, BPF_REG_1, BPF_REG_6,
				 offsetof(struct nf_hook_state, skb))))
		return false;

	/* arg2 is "struct nf_hook_state *" */
	if (!emit(p, BPF_MOV64_REG(BPF_REG_2, BPF_REG_6)))
		return false;

	/* arg3 is nf_hook_entries (original state->priv) */
	if (!emit(p, BPF_MOV64_REG(BPF_REG_3, BPF_REG_7)))
		return false;

	if (!emit(p, BPF_EMIT_CALL(nf_hook_slow)))
		return false;

	/* No further action needed, return retval provided by nf_hook_slow */
	return emit(p, BPF_EXIT_INSN());
}

static bool emit_nf_queue(struct nf_hook_prog *p)
{
	int width = bytes_to_bpf_size(sizeof(void *));

	if (width < 0) {
		WARN_ON_ONCE(1);
		return false;
	}

	/* int nf_queue(struct sk_buff *skb, struct nf_hook_state *state, unsigned int verdict) */
	if (!emit(p, BPF_LDX_MEM(width, BPF_REG_1, BPF_REG_6, offsetof(struct nf_hook_state, skb))))
		return false;
	if (!emit(p, BPF_STX_MEM(BPF_H, BPF_REG_6, BPF_REG_8,
				 offsetof(struct nf_hook_state, hook_index))))
		return false;
	/* arg2: struct nf_hook_state * */
	if (!emit(p, BPF_MOV64_REG(BPF_REG_2, BPF_REG_6)))
		return false;
	/* arg3: original hook return value: (NUM << NF_VERDICT_QBITS | NF_QUEUE) */
	if (!emit(p, BPF_MOV32_REG(BPF_REG_3, BPF_REG_0)))
		return false;
	if (!emit(p, BPF_EMIT_CALL(nf_queue)))
		return false;

	/* Check nf_queue return value.  Abnormal case: nf_queue returned != 0.
	 *
	 * Fall back to nf_hook_slow().
	 */
	if (!emit(p, BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2)))
		return false;

	/* Normal case: skb was stolen. Return 0. */
	return emit_retval(p, 0);
}

static bool do_epilogue_base_hooks(struct nf_hook_prog *p)
{
	int width = bytes_to_bpf_size(sizeof(void *));

	if (WARN_ON_ONCE(width < 0))
		return false;

	/* last 'hook'. We arrive here if previous hook returned ACCEPT,
	 * i.e. all hooks passed -- we are done.
	 *
	 * Return 1, skb can continue traversing network stack.
	 */
	if (!emit_retval(p, 1))
		return false;

	/* Patch all hook jumps, in case any of these are taken
	 * we need to jump to this location.
	 *
	 * This happens when verdict is != ACCEPT.
	 */
	patch_hook_jumps(p);

	/* need to ignore upper 24 bits, might contain errno or queue number */
	if (!emit(p, BPF_MOV32_REG(BPF_REG_3, BPF_REG_0)))
		return false;
	if (!emit(p, BPF_ALU32_IMM(BPF_AND, BPF_REG_3, 0xff)))
		return false;

	/* ACCEPT handled, check STOLEN. */
	if (!emit(p, BPF_JMP_IMM(BPF_JNE, BPF_REG_3, NF_STOLEN, 2)))
		return false;

	if (!emit_retval(p, 0))
		return false;

	/* ACCEPT and STOLEN handled.  Check DROP next */
	if (!emit(p, BPF_JMP_IMM(BPF_JNE, BPF_REG_3, NF_DROP, 1 + 2 + 2 + 2 + 2)))
		return false;

	/* First step. Extract the errno number. 1 insn. */
	if (!emit(p, BPF_ALU32_IMM(BPF_RSH, BPF_REG_0, NF_VERDICT_QBITS)))
		return false;

	/* Second step: replace errno with EPERM if it was 0. 2 insns. */
	if (!emit(p, BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1)))
		return false;
	if (!emit(p, BPF_MOV32_IMM(BPF_REG_0, EPERM)))
		return false;

	/* Third step: negate reg0: Caller expects -EFOO and stash the result.  2 insns. */
	if (!emit(p, BPF_ALU32_IMM(BPF_NEG, BPF_REG_0, 0)))
		return false;
	if (!emit(p, BPF_MOV32_REG(BPF_REG_8, BPF_REG_0)))
		return false;

	/* Fourth step: free the skb. 2 insns. */
	if (!emit(p, BPF_LDX_MEM(width, BPF_REG_1, BPF_REG_6, offsetof(struct nf_hook_state, skb))))
		return false;
	if (!emit(p, BPF_EMIT_CALL(kfree_skb)))
		return false;

	/* Last step: return. 2 insns. */
	if (!emit(p, BPF_MOV32_REG(BPF_REG_0, BPF_REG_8)))
		return false;
	if (!emit(p, BPF_EXIT_INSN()))
		return false;

	/* ACCEPT, STOLEN and DROP have been handled.
	 * REPEAT and STOP are not allowed anymore for individual hook functions.
	 * This leaves NFQUEUE as only remaing return value.
	 *
	 * In this case BPF_REG_0 still contains the original verdict of
	 * '(NUM << NF_VERDICT_QBITS | NF_QUEUE)', so pass it to nf_queue() as-is.
	 */
	if (!emit_nf_queue(p))
		return false;

	/* Increment hook index and store it in nf_hook_state so nf_hook_slow will
	 * start at the next hook, if any.
	 */
	if (!emit(p, BPF_ALU32_IMM(BPF_ADD, BPF_REG_8, 1)))
		return false;
	if (!emit(p, BPF_STX_MEM(BPF_H, BPF_REG_6, BPF_REG_8,
				 offsetof(struct nf_hook_state, hook_index))))
		return false;

	return emit_nf_hook_slow(p);
}

static int nf_hook_prog_init(struct nf_hook_prog *p)
{
	memset(p, 0, sizeof(*p));

	p->insns = kcalloc(BPF_MAXINSNS, sizeof(*p->insns), GFP_KERNEL);
	if (!p->insns)
		return -ENOMEM;

	return 0;
}

static void nf_hook_prog_free(struct nf_hook_prog *p)
{
	kfree(p->insns);
}

static int xlate_base_hooks(struct nf_hook_prog *p, const struct nf_hook_entries *e)
{
	unsigned int i, len;

	len = e->num_hook_entries;

	if (!do_prologue(p))
		goto out;

	for (i = 0; i < len; i++) {
		if (!xlate_one_hook(p, e, &e->hooks[i]))
			goto out;

		if (i + 1 < len) {
			if (!emit(p, BPF_MOV64_REG(BPF_REG_1, BPF_REG_6)))
				goto out;

			if (!emit(p, BPF_ALU32_IMM(BPF_ADD, BPF_REG_8, 1)))
				goto out;
		}
	}

	if (!do_epilogue_base_hooks(p))
		goto out;

	return 0;
out:
	return -EINVAL;
}

static struct bpf_prog *nf_hook_jit_compile(struct bpf_insn *insns, unsigned int len)
{
	struct bpf_prog *prog;
	int err = 0;

	prog = bpf_prog_alloc(bpf_prog_size(len), 0);
	if (!prog)
		return NULL;

	prog->len = len;
	prog->type = BPF_PROG_TYPE_SOCKET_FILTER;
	memcpy(prog->insnsi, insns, prog->len * sizeof(struct bpf_insn));

	prog = bpf_prog_select_runtime(prog, &err);
	if (err) {
		bpf_prog_free(prog);
		return NULL;
	}

	return prog;
}

/* fallback program, invokes nf_hook_slow interpreter.
 *
 * Used when a hook is unregsitered and new program cannot
 * be compiled for some reason.
 */
struct bpf_prog *nf_hook_bpf_create_fb(void)
{
	struct bpf_prog *prog;
	struct nf_hook_prog p;
	int err;

	err = nf_hook_prog_init(&p);
	if (err)
		return NULL;

	if (!do_prologue(&p))
		goto err;

	if (!emit_nf_hook_slow(&p))
		goto err;

	prog = nf_hook_jit_compile(p.insns, p.pos);
err:
	nf_hook_prog_free(&p);
	return prog;
}

static u32 nf_hook_entries_hash(const struct nf_hook_entries *new)
{
	int i, hook_count = new->num_hook_entries;
	u32 a, b, c;

	a = b = c = JHASH_INITVAL + hook_count;
	i = 0;
	while (hook_count > 3) {
		a += hash32_ptr(new->hooks[i+0].hook);
		b += hash32_ptr(new->hooks[i+1].hook);
		c += hash32_ptr(new->hooks[i+2].hook);
		__jhash_mix(a, b, c);
		hook_count -= 3;
		i += 3;
	}

	switch (hook_count) {
	case 3: c += hash32_ptr(new->hooks[i+2].hook); fallthrough;
	case 2: b += hash32_ptr(new->hooks[i+1].hook); fallthrough;
	case 1: a += hash32_ptr(new->hooks[i+0].hook);
		__jhash_final(a, b, c);
		break;
	}

	return c;
}

static struct bpf_prog *nf_hook_bpf_find_prog_by_key(const struct nf_hook_entries *new, u32 key)
{
	int i, hook_count = new->num_hook_entries;
	struct nf_hook_bpf_prog *pc;

	hash_for_each_possible(nf_bpf_progs_ht_key, pc, node_key, key) {
		if (pc->hook_count != hook_count ||
		    pc->key != key)
			continue;

		for (i = 0; i < hook_count; i++) {
			if (pc->hooks[i] != (unsigned long)new->hooks[i].hook)
				break;
		}

		if (i == hook_count) {
			refcount_inc(&pc->refcnt);
			return pc->prog;
		}
	}

	return NULL;
}

static struct nf_hook_bpf_prog *nf_hook_bpf_find_prog(const struct bpf_prog *p)
{
	struct nf_hook_bpf_prog *pc;

	hash_for_each_possible(nf_bpf_progs_ht_prog, pc, node_prog, (unsigned long)p) {
		if (pc->prog == p)
			return pc;
	}

	return NULL;
}

static void nf_hook_bpf_prog_store(const struct nf_hook_entries *new, struct bpf_prog *prog, u32 key)
{
	unsigned int i, hook_count = new->num_hook_entries;
	struct nf_hook_bpf_prog *alloc;

	if (hook_count >= ARRAY_SIZE(alloc->hooks))
		return;

	alloc = kzalloc(sizeof(*alloc), GFP_KERNEL);
	if (!alloc)
		return;

	alloc->hook_count = new->num_hook_entries;
	alloc->prog = prog;
	alloc->key = key;

	for (i = 0; i < hook_count; i++)
		alloc->hooks[i] = (unsigned long)new->hooks[i].hook;

	hash_add(nf_bpf_progs_ht_key, &alloc->node_key, key);
	hash_add(nf_bpf_progs_ht_prog, &alloc->node_prog, (unsigned long)prog);
	refcount_set(&alloc->refcnt, 1);

	bpf_prog_inc(prog);
}

struct bpf_prog *nf_hook_bpf_create(const struct nf_hook_entries *new)
{
	u32 key = nf_hook_entries_hash(new);
	struct bpf_prog *prog;
	struct nf_hook_prog p;
	int err;

	prog = nf_hook_bpf_find_prog_by_key(new, key);
	if (prog)
		return prog;

	err = nf_hook_prog_init(&p);
	if (err)
		return NULL;

	err = xlate_base_hooks(&p, new);
	if (err)
		goto err;

	prog = nf_hook_jit_compile(p.insns, p.pos);
	if (prog)
		nf_hook_bpf_prog_store(new, prog, key);
err:
	nf_hook_prog_free(&p);
	return prog;
}

static void __nf_hook_free_prog(struct rcu_head *head)
{
	struct nf_hook_bpf_prog *old = container_of(head, struct nf_hook_bpf_prog, rcu_head);

	bpf_prog_put(old->prog);
	kfree(old);
}

static void nf_hook_free_prog(struct nf_hook_bpf_prog *old)
{
	call_rcu(&old->rcu_head, __nf_hook_free_prog);
}

void nf_hook_bpf_change_prog(struct bpf_dispatcher *d, struct bpf_prog *from, struct bpf_prog *to)
{
	if (from == to)
		return;

	if (from) {
		struct nf_hook_bpf_prog *old;

		old = nf_hook_bpf_find_prog(from);
		if (old) {
			WARN_ON_ONCE(from != old->prog);
			if (refcount_dec_and_test(&old->refcnt)) {
				hash_del(&old->node_key);
				hash_del(&old->node_prog);
				nf_hook_free_prog(old);
			}
		}
	}

	bpf_dispatcher_change_prog(d, from, to);
}
