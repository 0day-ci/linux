// SPDX-License-Identifier: GPL-2.0

/*
 * Hyper-V vPCI irqchip.
 *
 * Copyright (C) 2021, Microsoft, Inc.
 *
 * Author : Sunil Muthuswamy <sunilmut@microsoft.com>
 */

#include <asm/mshyperv.h>
#include <linux/acpi.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/msi.h>

#ifdef CONFIG_X86_64
int hv_pci_irqchip_init(struct irq_domain **parent_domain,
			bool *fasteoi_handler,
			u8 *delivery_mode)
{
	*parent_domain = x86_vector_domain;
	*fasteoi_handler = false;
	*delivery_mode = APIC_DELIVERY_MODE_FIXED;

	return 0;
}

void hv_pci_irqchip_free(void) {}

unsigned int hv_msi_get_int_vector(struct irq_data *data)
{
	struct irq_cfg *cfg = irqd_cfg(data);

	return cfg->vector;
}

void hv_set_msi_entry_from_desc(union hv_msi_entry *msi_entry,
				struct msi_desc *msi_desc)
{
	msi_entry->address.as_uint32 = msi_desc->msg.address_lo;
	msi_entry->data.as_uint32 = msi_desc->msg.data;
}

int hv_msi_prepare(struct irq_domain *domain, struct device *dev,
		   int nvec, msi_alloc_info_t *info)
{
	return pci_msi_prepare(domain, dev, nvec, info);
}

#endif
