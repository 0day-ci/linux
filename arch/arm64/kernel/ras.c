// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/cpu.h>

#include <asm/cpufeature.h>
#include <asm/ras.h>

static bool ras_extn_v1p1(void)
{
	unsigned long fld, reg = read_sanitised_ftr_reg(SYS_ID_AA64PFR0_EL1);

	fld = cpuid_feature_extract_unsigned_field(reg, ID_AA64PFR0_RAS_SHIFT);

	return fld >= ID_AA64PFR0_RAS_V1P1;
}

u64 arch_arm_ras_get_status_clear_value(u64 err_status)
{
	/* Write-one-to-clear the bits we've seen */
	err_status &= ERR_STATUS_W1TC_MASK;

	/* If CE field is non-zero, all bits must be written to properly clear */
	if (err_status & ERR_STATUS_CE_MASK)
		err_status |= ERR_STATUS_CE_MASK;

	/* If UET field is non-zero, all bits must be written to properly clear */
	if (err_status & ERR_STATUS_UET_MASK)
		err_status |= ERR_STATUS_UET_MASK;

	return err_status;
}

void arch_arm_ras_print_error(struct ras_ext_regs *regs, unsigned int i, bool misc23_present)
{
	pr_err(" ERR%uSTATUS: 0x%llx\n", i, regs->err_status);
	if (regs->err_status & ERR_STATUS_AV)
		pr_err(" ERR%uADDR: 0x%llx\n", i, regs->err_addr);

	if (regs->err_status & ERR_STATUS_MV) {
		pr_err(" ERR%uMISC0: 0x%llx\n", i, regs->err_misc0);
		pr_err(" ERR%uMISC1: 0x%llx\n", i, regs->err_misc1);

		if (misc23_present) {
			pr_err(" ERR%uMISC2: 0x%llx\n", i, regs->err_misc2);
			pr_err(" ERR%uMISC3: 0x%llx\n", i, regs->err_misc3);
		}
	}
}

#undef pr_fmt
#define pr_fmt(fmt) "ARM RAS: " fmt

void arch_arm_ras_report_error(u64 implemented, bool clear_misc)
{
	struct ras_ext_regs regs = {0};
	unsigned int i, cpu_num;
	bool misc23_present;
	bool fatal = false;
	u64 num_records;

	if (!this_cpu_has_cap(ARM64_HAS_RAS_EXTN))
		return;

	cpu_num = get_cpu();
	num_records = read_sysreg_s(SYS_ERRIDR_EL1) & ERRIDR_NUM_MASK;

	for (i = 0; i < num_records; i++) {
		if (!(implemented & BIT(i)))
			continue;

		write_sysreg_s(i, SYS_ERRSELR_EL1);
		isb();
		regs.err_status = read_sysreg_s(SYS_ERXSTATUS_EL1);

		if (!(regs.err_status & ERR_STATUS_V))
			continue;

		pr_err("error from processor 0x%x\n", cpu_num);

		if (regs.err_status & ERR_STATUS_AV)
			regs.err_addr = read_sysreg_s(SYS_ERXADDR_EL1);

		misc23_present = ras_extn_v1p1();

		if (regs.err_status & ERR_STATUS_MV) {
			regs.err_misc0 = read_sysreg_s(SYS_ERXMISC0_EL1);
			regs.err_misc1 = read_sysreg_s(SYS_ERXMISC1_EL1);

			if (misc23_present) {
				regs.err_misc2 = read_sysreg_s(SYS_ERXMISC2_EL1);
				regs.err_misc3 = read_sysreg_s(SYS_ERXMISC3_EL1);
			}
		}

		arch_arm_ras_print_error(&regs, i, misc23_present);

		/*
		 * In the future, we will treat UER conditions as potentially
		 * recoverable.
		 */
		if (regs.err_status & ERR_STATUS_UE)
			fatal = true;

		regs.err_status = arch_arm_ras_get_status_clear_value(regs.err_status);
		write_sysreg_s(regs.err_status, SYS_ERXSTATUS_EL1);

		if (clear_misc) {
			write_sysreg_s(0x0, SYS_ERXMISC0_EL1);
			write_sysreg_s(0x0, SYS_ERXMISC1_EL1);

			if (misc23_present) {
				write_sysreg_s(0x0, SYS_ERXMISC2_EL1);
				write_sysreg_s(0x0, SYS_ERXMISC3_EL1);
			}
		}

		isb();
	}

	if (fatal)
		panic("ARM RAS: uncorrectable error encountered");

	put_cpu();
}
