/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * powerpc processor specific defines
 */
#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#define PPC_BIT(x) (1ULL << (63 - x))

#define MSR_SF  PPC_BIT(0)
#define MSR_IR  PPC_BIT(58)
#define MSR_DR  PPC_BIT(59)
#define MSR_LE  PPC_BIT(63)

#define LPCR_UPRT  PPC_BIT(41)
#define LPCR_EVIRT PPC_BIT(42)
#define LPCR_HR    PPC_BIT(43)
#define LPCR_GTSE  PPC_BIT(53)

#define PATB_GR	PPC_BIT(0)

#define PTE_VALID PPC_BIT(0)
#define PTE_LEAF  PPC_BIT(1)
#define PTE_R	  PPC_BIT(55)
#define PTE_C	  PPC_BIT(56)
#define PTE_RC	  (PTE_R | PTE_C)
#define PTE_READ  0x4
#define PTE_WRITE 0x2
#define PTE_EXEC  0x1
#define PTE_RWX   (PTE_READ|PTE_WRITE|PTE_EXEC)

extern uint64_t hcall(uint64_t nr, ...);

static inline uint32_t mfpvr(void)
{
	uint32_t pvr;

	asm ("mfpvr %0"
	     : "=r"(pvr));
	return pvr;
}

#endif
