/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_RAS_H
#define __ASM_RAS_H

#include <linux/types.h>
#include <linux/bits.h>

#define ERR_STATUS_AV		BIT(31)
#define ERR_STATUS_V		BIT(30)
#define ERR_STATUS_UE		BIT(29)
#define ERR_STATUS_MV		BIT(26)
#define ERR_STATUS_CE_MASK	(BIT(25) | BIT(24))
#define ERR_STATUS_DE		BIT(23)
#define ERR_STATUS_UET_MASK	(BIT(21) | BIT(20))
#define ERR_STATUS_IERR_SHIFT	8
#define ERR_STATUS_IERR_MASK	0xff
#define ERR_STATUS_SERR_SHIFT	0
#define ERR_STATUS_SERR_MASK	0xff
#define ERR_STATUS_W1TC_MASK	0xfff80000

#define ERRIDR_NUM_MASK		0xffff

#define ERRGSR_OFFSET		0xe00
#define ERRDEVARCH_OFFSET	0xfbc

#define ERRDEVARCH_REV_SHIFT	0x16
#define ERRDEVARCH_REV_MASK	0xf

#define RAS_REV_v1_1		0x1

struct ras_ext_regs {
	u64 err_fr;
	u64 err_ctlr;
	u64 err_status;
	u64 err_addr;
	u64 err_misc0;
	u64 err_misc1;
	u64 err_misc2;
	u64 err_misc3;
};

#ifdef CONFIG_ARM64_RAS_EXTN
void arch_arm_ras_print_error(struct ras_ext_regs *regs, unsigned int i, bool misc23_present);
u64 arch_arm_ras_get_status_clear_value(u64 err_status);
void arch_arm_ras_report_error(u64 implemented, bool clear_misc);
#else
void arch_arm_ras_print_error(struct ras_ext_regs *regs, unsigned int i, bool misc23_present) { }
u64 arch_arm_ras_get_status_clear_value(u64 err_status) { return 0; }
void arch_arm_ras_report_error(u64 implemented, bool clear_misc) { }
#endif

#endif	/* __ASM_RAS_H */
