// SPDX-License-Identifier: GPL-2.0-only
/*
 * Branch Record Buffer Extension Driver.
 *
 * Copyright (C) 2021 ARM Limited
 *
 * Author: Anshuman Khandual <anshuman.khandual@arm.com>
 */
#include "arm_pmu_brbe.h"

#define BRBE_FCR_MASK (BRBFCR_BRANCH_ALL)
#define BRBE_CR_MASK  (BRBCR_EXCEPTION | BRBCR_ERTN | BRBCR_CC | \
		       BRBCR_MPRED | BRBCR_E1BRE | BRBCR_E0BRE)

static void set_brbe_disabled(struct pmu_hw_events *cpuc)
{
	cpuc->brbe_nr = 0;
}

static bool brbe_disabled(struct pmu_hw_events *cpuc)
{
	return !cpuc->brbe_nr;
}

bool arm64_pmu_brbe_supported(struct perf_event *event)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *hw_events = per_cpu_ptr(armpmu->hw_events, event->cpu);

	if (event->attr.branch_sample_type & PERF_SAMPLE_BRANCH_KERNEL) {
		if (!perfmon_capable()) {
			pr_warn_once("does not have permission for kernel branch filter\n");
			return false;
		}
	}

	/*
	 * If the event does not have at least one of the privilege
	 * branch filters as in PERF_SAMPLE_BRANCH_PLM_ALL, the core
	 * perf will adjust its value based on perf event's existing
	 * privilege level via attr.exclude_[user|kernel|hv].
	 *
	 * As event->attr.branch_sample_type might have been changed
	 * when the event reaches here, it is not possible to figure
	 * out whether the event originally had HV privilege request
	 * or got added via the core perf. Just report this situation
	 * once and continue ignoring if there are other instances.
	 */
	if (event->attr.branch_sample_type & PERF_SAMPLE_BRANCH_HV)
		pr_warn_once("does not support hypervisor privilege branch filter\n");

	if (event->attr.branch_sample_type & PERF_SAMPLE_BRANCH_ABORT_TX) {
		pr_warn_once("does not support aborted transaction branch filter\n");
		return false;
	}

	if (event->attr.branch_sample_type & PERF_SAMPLE_BRANCH_NO_TX) {
		pr_warn_once("does not support non transaction branch filter\n");
		return false;
	}

	if (event->attr.branch_sample_type & PERF_SAMPLE_BRANCH_IN_TX) {
		pr_warn_once("does not support in transaction branch filter\n");
		return false;
	}
	return !brbe_disabled(hw_events);
}

void arm64_pmu_brbe_probe(struct pmu_hw_events *cpuc)
{
	u64 aa64dfr0, brbidr;
	unsigned int brbe, format, cpu = smp_processor_id();

	aa64dfr0 = read_sysreg_s(SYS_ID_AA64DFR0_EL1);
	brbe = cpuid_feature_extract_unsigned_field(aa64dfr0, ID_AA64DFR0_BRBE_SHIFT);
	if (!brbe) {
		pr_info("no implementation found on cpu %d\n", cpu);
		set_brbe_disabled(cpuc);
		return;
	} else if (brbe == ID_AA64DFR0_BRBE) {
		pr_info("implementation found on cpu %d\n", cpu);
		cpuc->v1p1 = false;
	} else if (brbe == ID_AA64DFR0_BRBE_V1P1) {
		pr_info("implementation (v1p1) found on cpu %d\n", cpu);
		cpuc->v1p1 = true;
	}

	brbidr = read_sysreg_s(SYS_BRBIDR0_EL1);
	format = brbe_fetch_format(brbidr);
	if (format != BRBIDR0_FORMAT_0) {
		pr_warn("format 0 not implemented\n");
		set_brbe_disabled(cpuc);
		return;
	}

	cpuc->brbe_cc = brbe_fetch_cc_bits(brbidr);
	if (cpuc->brbe_cc != BRBIDR0_CC_20_BIT) {
		pr_warn("20-bit counter not implemented\n");
		set_brbe_disabled(cpuc);
		return;
	}

	cpuc->brbe_nr = brbe_fetch_numrec(brbidr);
	if (!valid_brbe_nr(cpuc->brbe_nr)) {
		pr_warn("invalid number of records\n");
		set_brbe_disabled(cpuc);
		return;
	}
}

void arm64_pmu_brbe_enable(struct pmu_hw_events *cpuc)
{
	u64 brbfcr, brbcr;

	if (brbe_disabled(cpuc))
		return;

	brbfcr = read_sysreg_s(SYS_BRBFCR_EL1);
	brbfcr &= ~(BRBFCR_BANK_MASK << BRBFCR_BANK_SHIFT);
	brbfcr &= ~(BRBFCR_ENL | BRBFCR_PAUSED | BRBE_FCR_MASK);
	brbfcr |= (cpuc->brbfcr & BRBE_FCR_MASK);
	write_sysreg_s(brbfcr, SYS_BRBFCR_EL1);
	isb();

	brbcr = read_sysreg_s(SYS_BRBCR_EL1);
	brbcr &= ~BRBE_CR_MASK;
	brbcr |= BRBCR_FZP;
	brbcr |= (BRBCR_TS_PHYSICAL << BRBCR_TS_SHIFT);
	brbcr |= (cpuc->brbcr & BRBE_CR_MASK);
	write_sysreg_s(brbcr, SYS_BRBCR_EL1);
	isb();
}

void arm64_pmu_brbe_disable(struct pmu_hw_events *cpuc)
{
	u64 brbcr;

	if (brbe_disabled(cpuc))
		return;

	brbcr = read_sysreg_s(SYS_BRBCR_EL1);
	brbcr &= ~(BRBCR_E0BRE | BRBCR_E1BRE);
	write_sysreg_s(brbcr, SYS_BRBCR_EL1);
	isb();
}

static void perf_branch_to_brbfcr(struct pmu_hw_events *cpuc, int branch_type)
{
	cpuc->brbfcr = 0;

	if (branch_type & PERF_SAMPLE_BRANCH_ANY) {
		cpuc->brbfcr |= BRBFCR_BRANCH_ALL;
		return;
	}

	if (branch_type & PERF_SAMPLE_BRANCH_ANY_CALL)
		cpuc->brbfcr |= (BRBFCR_INDCALL | BRBFCR_DIRCALL);

	if (branch_type & PERF_SAMPLE_BRANCH_ANY_RETURN)
		cpuc->brbfcr |= BRBFCR_RTN;

	if (branch_type & PERF_SAMPLE_BRANCH_IND_CALL)
		cpuc->brbfcr |= BRBFCR_INDCALL;

	if (branch_type & PERF_SAMPLE_BRANCH_COND)
		cpuc->brbfcr |= BRBFCR_CONDDIR;

	if (branch_type & PERF_SAMPLE_BRANCH_IND_JUMP)
		cpuc->brbfcr |= BRBFCR_INDIRECT;

	if (branch_type & PERF_SAMPLE_BRANCH_CALL)
		cpuc->brbfcr |= BRBFCR_DIRCALL;
}

static void perf_branch_to_brbcr(struct pmu_hw_events *cpuc, int branch_type)
{
	cpuc->brbcr = (BRBCR_CC | BRBCR_MPRED);

	if (branch_type & PERF_SAMPLE_BRANCH_USER)
		cpuc->brbcr |= BRBCR_E0BRE;

	if (branch_type & PERF_SAMPLE_BRANCH_KERNEL) {
		/*
		 * This should have been verified earlier.
		 */
		WARN_ON(!perfmon_capable());
		cpuc->brbcr |= BRBCR_E1BRE;
	}

	if (branch_type & PERF_SAMPLE_BRANCH_NO_CYCLES)
		cpuc->brbcr &= ~BRBCR_CC;

	if (branch_type & PERF_SAMPLE_BRANCH_NO_FLAGS)
		cpuc->brbcr &= ~BRBCR_MPRED;

	if (!perfmon_capable())
		return;

	if (branch_type & PERF_SAMPLE_BRANCH_ANY) {
		cpuc->brbcr |= BRBCR_EXCEPTION;
		cpuc->brbcr |= BRBCR_ERTN;
		return;
	}

	if (branch_type & PERF_SAMPLE_BRANCH_ANY_CALL)
		cpuc->brbcr |= BRBCR_EXCEPTION;

	if (branch_type & PERF_SAMPLE_BRANCH_ANY_RETURN)
		cpuc->brbcr |= BRBCR_ERTN;
}


void arm64_pmu_brbe_filter(struct pmu_hw_events *cpuc, struct perf_event *event)
{
	u64 branch_type = event->attr.branch_sample_type;

	if (brbe_disabled(cpuc))
		return;

	perf_branch_to_brbfcr(cpuc, branch_type);
	perf_branch_to_brbcr(cpuc, branch_type);
}

static int brbe_fetch_perf_type(u64 brbinf)
{
	int brbe_type = brbe_fetch_type(brbinf);

	switch (brbe_type) {
	case BRBINF_TYPE_UNCOND_DIR:
		return PERF_BR_UNCOND;
	case BRBINF_TYPE_INDIR:
		return PERF_BR_IND;
	case BRBINF_TYPE_DIR_LINK:
		return PERF_BR_CALL;
	case BRBINF_TYPE_INDIR_LINK:
		return PERF_BR_IND_CALL;
	case BRBINF_TYPE_RET_SUB:
		return PERF_BR_RET;
	case BRBINF_TYPE_COND_DIR:
		return PERF_BR_COND;
	case BRBINF_TYPE_CALL:
		return PERF_BR_CALL;
	case BRBINF_TYPE_TRAP:
		return PERF_BR_SYSCALL;
	case BRBINF_TYPE_RET_EXCPT:
		return PERF_BR_EXPT_RET;
	case BRBINF_TYPE_IRQ:
		return PERF_BR_IRQ;
	case BRBINF_TYPE_FIQ:
		return PERF_BR_FIQ;
	case BRBINF_TYPE_DEBUG_HALT:
		return PERF_BR_DEBUG_HALT;
	case BRBINF_TYPE_DEBUG_EXIT:
		return PERF_BR_DEBUG_EXIT;
	case BRBINF_TYPE_SERROR:
	case BRBINF_TYPE_INST_DEBUG:
	case BRBINF_TYPE_DATA_DEBUG:
	case BRBINF_TYPE_ALGN_FAULT:
	case BRBINF_TYPE_INST_FAULT:
	case BRBINF_TYPE_DATA_FAULT:
		return PERF_BR_UNKNOWN;
	default:
		pr_warn("unknown branch type captured\n");
		return PERF_BR_UNKNOWN;
	}
}

static void capture_brbe_flags(struct pmu_hw_events *cpuc, struct perf_event *event,
			       u64 brbinf, int idx)
{
	int type = brbe_record_valid(brbinf);

	if (!branch_sample_no_cycles(event))
		cpuc->brbe_entries[idx].cycles = brbe_fetch_cycles(brbinf);

	if (branch_sample_type(event))
		cpuc->brbe_entries[idx].type = brbe_fetch_perf_type(brbinf);

	if (!branch_sample_no_flags(event)) {
		/*
		 * BRBINF_LASTFAILED does not indicate that the last transaction
		 * got failed or aborted during the current branch record itself.
		 * Rather, this indicates that all the branch records which were
		 * in transaction until the curret branch record have failed. So
		 * the entire BRBE buffer needs to be processed later on to find
		 * all branch records which might have failed.
		 */
		cpuc->brbe_entries[idx].abort = brbinf & BRBINF_LASTFAILED;

		/*
		 * All these information (i.e transaction state and mispredicts)
		 * are not available for target only branch records.
		 */
		if (type != BRBINF_VALID_TARGET) {
			cpuc->brbe_entries[idx].mispred = brbinf & BRBINF_MPRED;
			cpuc->brbe_entries[idx].predicted = !(brbinf & BRBINF_MPRED);
			cpuc->brbe_entries[idx].in_tx = brbinf & BRBINF_TX;
		}
	}
}

/*
 * A branch record with BRBINF_EL1.LASTFAILED set, implies that all
 * preceding consecutive branch records, that were in a transaction
 * (i.e their BRBINF_EL1.TX set) have been aborted.
 *
 * Similarly BRBFCR_EL1.LASTFAILED set, indicate that all preceding
 * consecutive branch records upto the last record, which were in a
 * transaction (i.e their BRBINF_EL1.TX set) have been aborted.
 *
 * --------------------------------- -------------------
 * | 00 | BRBSRC | BRBTGT | BRBINF | | TX = 1 | LF = 0 | [TX success]
 * --------------------------------- -------------------
 * | 01 | BRBSRC | BRBTGT | BRBINF | | TX = 1 | LF = 0 | [TX success]
 * --------------------------------- -------------------
 * | 02 | BRBSRC | BRBTGT | BRBINF | | TX = 0 | LF = 0 |
 * --------------------------------- -------------------
 * | 03 | BRBSRC | BRBTGT | BRBINF | | TX = 1 | LF = 0 | [TX failed]
 * --------------------------------- -------------------
 * | 04 | BRBSRC | BRBTGT | BRBINF | | TX = 1 | LF = 0 | [TX failed]
 * --------------------------------- -------------------
 * | 05 | BRBSRC | BRBTGT | BRBINF | | TX = 0 | LF = 1 |
 * --------------------------------- -------------------
 * | .. | BRBSRC | BRBTGT | BRBINF | | TX = 0 | LF = 0 |
 * --------------------------------- -------------------
 * | 61 | BRBSRC | BRBTGT | BRBINF | | TX = 1 | LF = 0 | [TX failed]
 * --------------------------------- -------------------
 * | 62 | BRBSRC | BRBTGT | BRBINF | | TX = 1 | LF = 0 | [TX failed]
 * --------------------------------- -------------------
 * | 63 | BRBSRC | BRBTGT | BRBINF | | TX = 1 | LF = 0 | [TX failed]
 * --------------------------------- -------------------
 *
 * BRBFCR_EL1.LASTFAILED == 1
 *
 * Here BRBFCR_EL1.LASTFAILED failes all those consecutive and also
 * in transaction branches near the end of the BRBE buffer.
 */
static void process_branch_aborts(struct pmu_hw_events *cpuc)
{
	u64 brbfcr = read_sysreg_s(SYS_BRBFCR_EL1);
	bool lastfailed = !!(brbfcr & BRBFCR_LASTFAILED);
	int idx = cpuc->brbe_nr - 1;

	do {
		if (cpuc->brbe_entries[idx].in_tx) {
			cpuc->brbe_entries[idx].abort = lastfailed;
		} else {
			lastfailed = cpuc->brbe_entries[idx].abort;
			cpuc->brbe_entries[idx].abort = false;
		}
	} while (idx--, idx >= 0);
}

void arm64_pmu_brbe_read(struct pmu_hw_events *cpuc, struct perf_event *event)
{
	u64 brbinf;
	int idx;

	if (brbe_disabled(cpuc))
		return;

	set_brbe_paused();
	for (idx = 0; idx < cpuc->brbe_nr; idx++) {
		select_brbe_bank_index(idx);
		brbinf = get_brbinf_reg(idx);
		/*
		 * There are no valid entries anymore on the buffer.
		 * Abort the branch record processing to save some
		 * cycles and also reduce the capture/process load
		 * for the user space as well.
		 */
		if (brbe_invalid(brbinf))
			break;

		if (brbe_valid(brbinf)) {
			cpuc->brbe_entries[idx].from =  get_brbsrc_reg(idx);
			cpuc->brbe_entries[idx].to =  get_brbtgt_reg(idx);
		} else if (brbe_source(brbinf)) {
			cpuc->brbe_entries[idx].from =  get_brbsrc_reg(idx);
			cpuc->brbe_entries[idx].to = 0;
		} else if (brbe_target(brbinf)) {
			cpuc->brbe_entries[idx].from = 0;
			cpuc->brbe_entries[idx].to =  get_brbtgt_reg(idx);
		}
		capture_brbe_flags(cpuc, event, brbinf, idx);
	}
	cpuc->brbe_stack.nr = idx;
	cpuc->brbe_stack.hw_idx = -1ULL;
	process_branch_aborts(cpuc);
}

void arm64_pmu_brbe_reset(struct pmu_hw_events *cpuc)
{
	if (brbe_disabled(cpuc))
		return;

	asm volatile(BRB_IALL);
	isb();
}
