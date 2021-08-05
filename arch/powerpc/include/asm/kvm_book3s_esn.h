/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KVM_BOOK3S_ESN_H__
#define __ASM_KVM_BOOK3S_ESN_H__

/* SNS buffer EQ state flags */
#define SNS_EQ_STATE_OPERATIONAL 0X0
#define SNS_EQ_STATE_OVERFLOW 0x1

/* SNS buffer Notification control bits */
#define SNS_EQ_CNTRL_TRIGGER 0x1

struct kvmppc_sns {
	unsigned long gpa;
	unsigned long len;
	void *hva;
	uint16_t exp_corr_nr;
	uint16_t *eq;
	uint8_t *eq_cntrl;
	uint8_t *eq_state;
	unsigned long next_eq_entry;
	unsigned long nr_eq_entries;
};

#endif /* __ASM_KVM_BOOK3S_ESN_H__ */
