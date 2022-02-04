/* SPDX-License-Identifier: GPL-2.0 */
/*
 * s390 kvm PCI passthrough support
 *
 * Copyright IBM Corp. 2021
 *
 *    Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 */

#ifndef __KVM_S390_PCI_H
#define __KVM_S390_PCI_H

#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/kvm_host.h>
#include <asm/airq.h>
#include <asm/kvm_pci.h>

#define KVM_S390_PCI_DTSM_MASK 0x40

#define KVM_S390_RPCIT_INS_RES 0x10
#define KVM_S390_RPCIT_ERR 0x28

struct zpci_gaite {
	u32 gisa;
	u8 gisc;
	u8 count;
	u8 reserved;
	u8 aisbo;
	u64 aisb;
};

struct zpci_aift {
	struct zpci_gaite *gait;
	struct airq_iv *sbv;
	struct kvm_zdev **kzdev;
	spinlock_t gait_lock; /* Protects the gait, used during AEN forward */
	struct mutex aift_lock; /* Protects the other structures in aift */
	u32 mdd;
};

extern struct zpci_aift *aift;

static inline struct kvm *kvm_s390_pci_si_to_kvm(struct zpci_aift *aift,
						 unsigned long si)
{
	if (!IS_ENABLED(CONFIG_VFIO_PCI_ZDEV) || aift->kzdev == 0 ||
	    aift->kzdev[si] == 0)
		return 0;
	return aift->kzdev[si]->kvm;
};

int kvm_s390_pci_aen_init(u8 nisc);
void kvm_s390_pci_aen_exit(void);
int kvm_s390_pci_refresh_trans(struct kvm_vcpu *vcpu, unsigned long req,
			       unsigned long start, unsigned long end,
			       u8 *status);
int kvm_s390_pci_init(void);

#endif /* __KVM_S390_PCI_H */
