/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Architecture specific vector management for the Hyper-V vPCI.
 *
 * Copyright (C) 2021, Microsoft, Inc.
 *
 * Author : Sunil Muthuswamy <sunilmut@microsoft.com>
 */

int hv_pci_irqchip_init(struct irq_domain **parent_domain,
			bool *fasteoi_handler,
			u8 *delivery_mode);

void hv_pci_irqchip_free(void);
unsigned int hv_msi_get_int_vector(struct irq_data *data);
void hv_set_msi_entry_from_desc(union hv_msi_entry *msi_entry,
				struct msi_desc *msi_desc);

int hv_msi_prepare(struct irq_domain *domain, struct device *dev,
		   int nvec, msi_alloc_info_t *info);
