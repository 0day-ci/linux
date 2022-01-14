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
#include <asm/airq.h>
#include <asm/kvm_pci.h>

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
	struct mutex lock; /* Protects the other structures in aift */
};

extern struct zpci_aift *aift;

int kvm_s390_pci_aen_init(u8 nisc);
void kvm_s390_pci_aen_exit(void);

int kvm_s390_pci_init(void);

#endif /* __KVM_S390_PCI_H */
