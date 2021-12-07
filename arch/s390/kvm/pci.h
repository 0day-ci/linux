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

struct zpci_gaite {
	unsigned int gisa;
	u8 gisc;
	u8 count;
	u8 reserved;
	u8 aisbo;
	unsigned long aisb;
};

struct zpci_aift {
	struct zpci_gaite *gait;
	struct airq_iv *sbv;
	struct kvm_zdev **kzdev;
	spinlock_t gait_lock; /* Protects the gait, used during AEN forward */
	struct mutex lock; /* Protects the other structures in aift */
};

static inline struct kvm *kvm_s390_pci_si_to_kvm(struct zpci_aift *aift,
						 unsigned long si)
{
	if (aift->kzdev == 0 || aift->kzdev[si] == 0)
		return 0;
	return aift->kzdev[si]->kvm;
};

struct zpci_aift *kvm_s390_pci_get_aift(void);

int kvm_s390_pci_aen_init(u8 nisc);
void kvm_s390_pci_aen_exit(void);

void kvm_s390_pci_init(void);

#endif /* __KVM_S390_PCI_H */
