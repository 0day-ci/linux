// SPDX-License-Identifier: GPL-2.0-only
/*
 * Code for Kernel probes Jump optimization.
 *
 * Copyright (C) 2021 Hisilicon Limited
 */

#include <linux/jump_label.h>
#include <linux/kprobes.h>

#include <asm/cacheflush.h>
#include <asm/kprobes.h>
/* for aarch64_insn_gen_branch_imm */
#include <asm/insn.h>

#define TMPL_VAL_IDX \
	(optprobe_template_val - optprobe_template_entry)
#define TMPL_CALL_BACK \
	(optprobe_template_call - optprobe_template_entry)
#define TMPL_END_IDX \
	(optprobe_template_end - optprobe_template_entry)
#define TMPL_RESTORE_ORIGN_INSN \
	(optprobe_template_restore_orig_insn - optprobe_template_entry)
#define TMPL_RESTORE_END \
	(optprobe_template_restore_end - optprobe_template_entry)

int arch_check_optimized_kprobe(struct optimized_kprobe *op)
{
	return 0;
}

int arch_prepared_optinsn(struct arch_optimized_insn *optinsn)
{
	return optinsn->insn != NULL;
}

int arch_within_optimized_kprobe(struct optimized_kprobe *op,
				unsigned long addr)
{
	return ((unsigned long)op->kp.addr <= addr &&
		(unsigned long)op->kp.addr + RELATIVEJUMP_SIZE > addr);
}

static void
optimized_callback(struct optimized_kprobe *op, struct pt_regs *regs)
{
	/* This is possible if op is under delayed unoptimizing */
	if (kprobe_disabled(&op->kp))
		return;

	preempt_disable();

	if (kprobe_running()) {
		kprobes_inc_nmissed_count(&op->kp);
	} else {
		__this_cpu_write(current_kprobe, &op->kp);
		regs->pc = (unsigned long)op->kp.addr;
		get_kprobe_ctlblk()->kprobe_status = KPROBE_HIT_ACTIVE;
		opt_pre_handler(&op->kp, regs);
		__this_cpu_write(current_kprobe, NULL);
	}

	preempt_enable_no_resched();
}
NOKPROBE_SYMBOL(optimized_callback)

bool is_offset_in_branch_range(long offset)
{
	return (offset >= -0x08000000 && offset <= 0x07fffffc && !(offset & 0x3));
}

int arch_prepare_optimized_kprobe(struct optimized_kprobe *op, struct kprobe *orig)
{
	kprobe_opcode_t *code;
	long rel_chk;
	u32 insn, size;
	int ret, i;
	void *addr;

	code = get_optinsn_slot();
	if (!code)
		return -ENOMEM;

	/*
	 * Verify if the address gap is in 128MiB range, because this uses
	 * a relative jump.
	 *
	 * kprobe opt use a 'b' instruction to branch to optinsn.insn.
	 * According to ARM manual, branch instruction is:
	 *
	 *   31  30                  25              0
	 *  +----+---+---+---+---+---+---------------+
	 *  |cond| 0 | 0 | 1 | 0 | 1 |     imm26     |
	 *  +----+---+---+---+---+---+---------------+
	 *
	 * imm26 is a signed 26 bits integer. The real branch offset is computed
	 * by: imm64 = SignExtend(imm26:'00', 64);
	 *
	 * So the maximum forward branch should be:
	 *   (0x01ffffff << 2) = 1720x07fffffc =  0x07fffffc
	 * The maximum backward branch should be:
	 *   (0xfe000000 << 2) = 0xFFFFFFFFF8000000 = -0x08000000
	 *
	 * We can simply check (rel & 0xf8000003):
	 *  if rel is positive, (rel & 0xf8000003) should be 0
	 *  if rel is negitive, (rel & 0xf8000003) should be 0xf8000000
	 *  the last '3' is used for alignment checking.
	 */
	rel_chk = (unsigned long)code -
			(unsigned long)orig->addr + 8;
	if (!is_offset_in_branch_range(rel_chk)) {
		pr_err("%s is out of branch range.\n", orig->symbol_name);
		free_optinsn_slot(code, 0);
		return -ERANGE;
	}

	/* Setup template */
	size = (TMPL_END_IDX * sizeof(kprobe_opcode_t)) / sizeof(int);
	for (i = 0; i < size; i++) {
		addr = code + i;
		insn = *(optprobe_template_entry + i);
		ret = aarch64_insn_patch_text(&addr, &insn, 1);
		if (ret < 0) {
			free_optinsn_slot(code, 0);
			return -ERANGE;
		}
	}

	/* Set probe information */
	addr = code + TMPL_VAL_IDX;
	insn =  (unsigned long long)op & 0xffffffff;
	aarch64_insn_patch_text(&addr, &insn, 1);

	addr = addr + 4;
	insn = ((unsigned long long)op & GENMASK_ULL(63, 32)) >> 32;
	aarch64_insn_patch_text(&addr, &insn, 1);

	addr = code + TMPL_CALL_BACK;
	insn =  aarch64_insn_gen_branch_imm((unsigned long)addr,
				(unsigned long)optimized_callback,
				AARCH64_INSN_BRANCH_LINK);
	aarch64_insn_patch_text(&addr, &insn, 1);

	/* The original probed instruction */
	addr = code + TMPL_RESTORE_ORIGN_INSN;
	insn =  orig->opcode;
	aarch64_insn_patch_text(&addr, &insn, 1);

	/* Jump back to next instruction */
	addr = code + TMPL_RESTORE_END;
	insn = aarch64_insn_gen_branch_imm(
				(unsigned long)(&code[TMPL_RESTORE_END]),
				(unsigned long)(op->kp.addr) + 4,
				AARCH64_INSN_BRANCH_NOLINK);
	aarch64_insn_patch_text(&addr, &insn, 1);

	flush_icache_range((unsigned long)code,
			   (unsigned long)(&code[TMPL_END_IDX]));
	/* Set op->optinsn.insn means prepared. */
	op->optinsn.insn = code;
	return 0;
}

void arch_optimize_kprobes(struct list_head *oplist)
{
	struct optimized_kprobe *op, *tmp;

	list_for_each_entry_safe(op, tmp, oplist, list) {
		u32 insn;

		WARN_ON(kprobe_disabled(&op->kp));

		/*
		 * Backup instructions which will be replaced
		 * by jump address
		 */
		memcpy(op->optinsn.copied_insn, op->kp.addr,
			RELATIVEJUMP_SIZE);
		insn = aarch64_insn_gen_branch_imm((unsigned long)op->kp.addr,
				(unsigned long)op->optinsn.insn,
				AARCH64_INSN_BRANCH_NOLINK);

		WARN_ON(insn == 0);

		aarch64_insn_patch_text((void *)&(op->kp.addr), &insn, 1);

		list_del_init(&op->list);
	}
}

void arch_unoptimize_kprobe(struct optimized_kprobe *op)
{
	arch_arm_kprobe(&op->kp);
}

/*
 * Recover original instructions and breakpoints from relative jumps.
 * Caller must call with locking kprobe_mutex.
 */
void arch_unoptimize_kprobes(struct list_head *oplist,
			    struct list_head *done_list)
{
	struct optimized_kprobe *op, *tmp;

	list_for_each_entry_safe(op, tmp, oplist, list) {
		arch_unoptimize_kprobe(op);
		list_move(&op->list, done_list);
	}
}

void arch_remove_optimized_kprobe(struct optimized_kprobe *op)
{
	if (op->optinsn.insn) {
		free_optinsn_slot(op->optinsn.insn, 1);
		op->optinsn.insn = NULL;
	}
}

