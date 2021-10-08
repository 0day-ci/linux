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

#elif CONFIG_ARM64

/*
 * SPI vectors to use for vPCI; arch SPIs range is [32, 1019], but leaving a bit
 * of room at the start to allow for SPIs to be specified through ACPI and
 * starting with a power of two to satisfy power of 2 multi-MSI requirement.
 */
#define HV_PCI_MSI_SPI_START	64
#define HV_PCI_MSI_SPI_NR	(1020 - HV_PCI_MSI_SPI_START)

struct hv_pci_chip_data {
	DECLARE_BITMAP(spi_map, HV_PCI_MSI_SPI_NR);
	struct mutex	map_lock;
};

/* Hyper-V vPCI MSI GIC IRQ domain */
static struct irq_domain *hv_msi_gic_irq_domain;

/* Hyper-V PCI MSI IRQ chip */
static struct irq_chip hv_msi_irq_chip = {
	.name = "MSI",
	.irq_set_affinity = irq_chip_set_affinity_parent,
	.irq_eoi = irq_chip_eoi_parent,
	.irq_mask = irq_chip_mask_parent,
	.irq_unmask = irq_chip_unmask_parent
};

unsigned int hv_msi_get_int_vector(struct irq_data *irqd)
{
	irqd = irq_domain_get_irq_data(hv_msi_gic_irq_domain, irqd->irq);

	return irqd->hwirq;
}

void hv_set_msi_entry_from_desc(union hv_msi_entry *msi_entry,
				struct msi_desc *msi_desc)
{
	msi_entry->address = ((u64)msi_desc->msg.address_hi << 32) |
			      msi_desc->msg.address_lo;
	msi_entry->data = msi_desc->msg.data;
}

int hv_msi_prepare(struct irq_domain *domain, struct device *dev,
		   int nvec, msi_alloc_info_t *info)
{
	return 0;
}

static void hv_pci_vec_irq_domain_free(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs)
{
	struct hv_pci_chip_data *chip_data = domain->host_data;
	struct irq_data *irqd = irq_domain_get_irq_data(domain, virq);
	int first = irqd->hwirq - HV_PCI_MSI_SPI_START;

	mutex_lock(&chip_data->map_lock);
	bitmap_release_region(chip_data->spi_map,
			      first,
			      get_count_order(nr_irqs));
	mutex_unlock(&chip_data->map_lock);
	irq_domain_reset_irq_data(irqd);
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static int hv_pci_vec_alloc_device_irq(struct irq_domain *domain,
				       unsigned int nr_irqs,
				       irq_hw_number_t *hwirq)
{
	struct hv_pci_chip_data *chip_data = domain->host_data;
	unsigned int index;

	/* Find and allocate region from the SPI bitmap */
	mutex_lock(&chip_data->map_lock);
	index = bitmap_find_free_region(chip_data->spi_map,
					HV_PCI_MSI_SPI_NR,
					get_count_order(nr_irqs));
	mutex_unlock(&chip_data->map_lock);
	if (index < 0)
		return -ENOSPC;

	*hwirq = index + HV_PCI_MSI_SPI_START;

	return 0;
}

static int hv_pci_vec_irq_gic_domain_alloc(struct irq_domain *domain,
					   unsigned int virq,
					   irq_hw_number_t hwirq)
{
	struct irq_fwspec fwspec;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 2;
	fwspec.param[0] = hwirq;
	fwspec.param[1] = IRQ_TYPE_EDGE_RISING;

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
}

static int hv_pci_vec_irq_domain_alloc(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs,
				       void *args)
{
	irq_hw_number_t hwirq;
	unsigned int i;
	int ret;

	ret = hv_pci_vec_alloc_device_irq(domain, nr_irqs, &hwirq);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		ret = hv_pci_vec_irq_gic_domain_alloc(domain, virq + i,
						      hwirq + i);
		if (ret)
			goto free_irq;

		ret = irq_domain_set_hwirq_and_chip(domain, virq + i,
						    hwirq + i, &hv_msi_irq_chip,
						    domain->host_data);
		if (ret)
			goto free_irq;

		pr_debug("pID:%d vID:%u\n", (int)(hwirq + i), virq + i);
	}

	return 0;

free_irq:
	hv_pci_vec_irq_domain_free(domain, virq, nr_irqs);

	return ret;
}

static int hv_pci_vec_irq_domain_activate(struct irq_domain *domain,
					  struct irq_data *irqd, bool reserve)
{
	/* All available online CPUs are available for targeting */
	irq_data_update_effective_affinity(irqd, cpu_online_mask);

	return 0;
}

static const struct irq_domain_ops hv_pci_domain_ops = {
	.alloc	= hv_pci_vec_irq_domain_alloc,
	.free	= hv_pci_vec_irq_domain_free,
	.activate = hv_pci_vec_irq_domain_activate,
};

int hv_pci_irqchip_init(struct irq_domain **parent_domain,
			bool *fasteoi_handler,
			u8 *delivery_mode)
{
	static struct hv_pci_chip_data *chip_data;
	struct fwnode_handle *fn = NULL;
	int ret = -ENOMEM;

	chip_data = kzalloc(sizeof(*chip_data), GFP_KERNEL);
	if (!chip_data)
		return ret;

	mutex_init(&chip_data->map_lock);
	fn = irq_domain_alloc_named_fwnode("Hyper-V ARM64 vPCI");
	if (!fn)
		goto free_chip;

	hv_msi_gic_irq_domain = acpi_irq_create_hierarchy(0, HV_PCI_MSI_SPI_NR,
							  fn, &hv_pci_domain_ops,
							  chip_data);

	if (!hv_msi_gic_irq_domain) {
		pr_err("Failed to create Hyper-V ARMV vPCI MSI IRQ domain\n");
		goto free_chip;
	}

	*parent_domain = hv_msi_gic_irq_domain;
	*fasteoi_handler = true;

	/* Delivery mode: Fixed */
	*delivery_mode = 0;

	return 0;

free_chip:
	kfree(chip_data);
	if (fn)
		irq_domain_free_fwnode(fn);

	return ret;
}

void hv_pci_irqchip_free(void)
{
	static struct hv_pci_chip_data *chip_data;

	if (!hv_msi_gic_irq_domain)
		return;

	/* Host data cannot be null if the domain was created successfully */
	chip_data = hv_msi_gic_irq_domain->host_data;
	irq_domain_remove(hv_msi_gic_irq_domain);
	hv_msi_gic_irq_domain = NULL;
	kfree(chip_data);
}

#endif
