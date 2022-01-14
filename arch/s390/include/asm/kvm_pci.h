/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KVM PCI Passthrough for virtual machines on s390
 *
 * Copyright IBM Corp. 2021
 *
 *    Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 */


#ifndef ASM_KVM_PCI_H
#define ASM_KVM_PCI_H

#include <linux/types.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/pci.h>

struct kvm_zdev {
	struct zpci_dev *zdev;
	struct kvm *kvm;
};

int kvm_s390_pci_dev_open(struct zpci_dev *zdev);
void kvm_s390_pci_dev_release(struct zpci_dev *zdev);
void kvm_s390_pci_attach_kvm(struct zpci_dev *zdev, struct kvm *kvm);

int kvm_s390_pci_interp_probe(struct zpci_dev *zdev);
int kvm_s390_pci_interp_enable(struct zpci_dev *zdev);
int kvm_s390_pci_interp_disable(struct zpci_dev *zdev);

#endif /* ASM_KVM_PCI_H */
