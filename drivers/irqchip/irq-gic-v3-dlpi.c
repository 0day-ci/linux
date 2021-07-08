// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Microsoft Corporation.
 * Author: Sunil Muthuswamy <sunilmut@microsoft.com>
 *
 * This file implements an IRQ domain and chip to handle Direct LPI
 * when there is no ITS, for GIC v3.
 */

#include <linux/acpi_iort.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/list.h>
#include <linux/msi.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/dma-iommu.h>

#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <linux/irqchip/arm-gic-v4.h>

#include "irq-gic-common.h"

static struct rdists *gic_rdists;

#define gic_data_rdist_cpu(cpu)		(per_cpu_ptr(gic_rdists->rdist, cpu))

#define RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING	(1 << 0)

/*
 * Structure that holds most of the infrastructure needed to support
 * DirectLPI without an ITS.
 *
 * dev_alloc_lock has to be taken for device allocations, while the
 * spinlock must be taken to parse data structures such as the device
 * list.
 */

struct direct_lpi {
	raw_spinlock_t		lock;
	struct mutex		dev_alloc_lock;
	struct list_head	entry;
	struct fwnode_handle	*fwnode_handle;
	struct list_head	device_list;
	u64			flags;
	unsigned int		msi_domain_flags;
};

struct event_lpi_map {
	unsigned long		*lpi_map;
	u16			*col_map;
	irq_hw_number_t		lpi_base;
	int			nr_lpis;
};

struct direct_lpi_device {
	struct list_head	entry;
	struct direct_lpi	*dlpi;
	struct event_lpi_map	event_map;
	u32			device_id;
	bool			shared;
};

static int dlpi_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *args);

static void dlpi_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs);

static int dlpi_irq_domain_activate(struct irq_domain *domain,
				   struct irq_data *d, bool reserve);

static void dlpi_irq_domain_deactivate(struct irq_domain *domain,
				      struct irq_data *d);

static const struct irq_domain_ops dlpi_domain_ops = {
	.alloc			= dlpi_irq_domain_alloc,
	.free			= dlpi_irq_domain_free,
	.activate		= dlpi_irq_domain_activate,
	.deactivate		= dlpi_irq_domain_deactivate,
};

static int dlpi_msi_prepare(struct irq_domain *domain, struct device *dev,
			   int nvec, msi_alloc_info_t *info);

static struct msi_domain_ops dlpi_msi_domain_ops = {
	.msi_prepare	= dlpi_msi_prepare,
};

static int dlpi_init_domain(struct fwnode_handle *handle,
			      struct irq_domain *parent_domain,
			      struct direct_lpi *dlpi)
{
	struct irq_domain *inner_domain;
	struct msi_domain_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	inner_domain = irq_domain_create_tree(handle, &dlpi_domain_ops, NULL);
	if (!inner_domain) {
		kfree(info);
		return -ENOMEM;
	}

	inner_domain->parent = parent_domain;
	irq_domain_update_bus_token(inner_domain, DOMAIN_BUS_NEXUS);
	inner_domain->flags |= dlpi->msi_domain_flags;
	info->ops = &dlpi_msi_domain_ops;
	info->data = dlpi;
	inner_domain->host_data = info;

	return 0;
}

int __init direct_lpi_init(struct irq_domain *parent, struct rdists *rdists)
{
	struct fwnode_handle *fwnode;
	int err;
	struct direct_lpi *dlpi = NULL;

	gic_rdists = rdists;
	fwnode = irq_domain_alloc_named_fwnode("Direct LPI");
	if (!fwnode)
		return -ENOMEM;

	/*
	 * Registering with the iort allows other services to query the
	 * fwnode. But, the registration requires an ITS ID and base address,
	 * which does not apply here. So, probably best to just export the
	 * fwnode handle for other services. Keeping it here for comments
	 * from RFC submission.
	 */
	err = iort_register_domain_token(0, 0, fwnode);
	if (err)
		goto out_free_fwnode;

	dlpi = kzalloc(sizeof(*dlpi), GFP_KERNEL);
	if (!dlpi) {
		err = -ENOMEM;
		goto out_unregister_fwnode;
	}

	raw_spin_lock_init(&dlpi->lock);
	mutex_init(&dlpi->dev_alloc_lock);
	INIT_LIST_HEAD(&dlpi->entry);
	INIT_LIST_HEAD(&dlpi->device_list);
	dlpi->msi_domain_flags = IRQ_DOMAIN_FLAG_MSI_REMAP;
	err = dlpi_init_domain(fwnode, parent, dlpi);
	if (err)
		goto out_unregister_fwnode;

	return 0;

out_unregister_fwnode:
	iort_deregister_domain_token(0);
out_free_fwnode:
	irq_domain_free_fwnode(fwnode);
	kfree(dlpi);

	return err;

}

static inline u32 dlpi_get_event_id(struct irq_data *d)
{
	struct direct_lpi_device *dlpi_dev = irq_data_get_irq_chip_data(d);

	return d->hwirq - dlpi_dev->event_map.lpi_base;
}

static int dlpi_irq_to_cpuid(struct irq_data *d, unsigned long *flags)
{
	int cpu;

	/* Physical LPIs are already locked via the irq_desc lock */
	struct direct_lpi_device *dlpi_dev =
		irq_data_get_irq_chip_data(d);
	cpu = dlpi_dev->event_map.col_map[dlpi_get_event_id(d)];
	/* Keep GCC quiet... */
	*flags = 0;

	return cpu;
}

/*
 * irqchip functions - assumes MSI, mostly.
 */
//TODO: Maybe its::lpi_write_config can call into this routine
static void lpi_write_config(struct irq_data *d, u8 clr, u8 set)
{
	irq_hw_number_t hwirq;
	void *va;
	u8 *cfg;

	va = gic_rdists->prop_table_va;
	hwirq = d->hwirq;
	cfg = va + hwirq - 8192;
	*cfg &= ~clr;
	*cfg |= set | LPI_PROP_GROUP1;

	/*
	 * Make the above write visible to the redistributors.
	 * And yes, we're flushing exactly: One. Single. Byte.
	 * Humpf...
	 */
	if (gic_rdists->flags & RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING)
		gic_flush_dcache_to_poc(cfg, sizeof(*cfg));
	else
		dsb(ishst);
}

static void wait_for_syncr(void __iomem *rdbase)
{
	while (readl_relaxed(rdbase + GICR_SYNCR) & 1)
		cpu_relax();
}

static void dlpi_direct_lpi_inv(struct irq_data *d)
{
	void __iomem *rdbase;
	unsigned long flags;
	u64 val;
	int cpu;

	val = d->hwirq;

	/* Target the redistributor this LPI is currently routed to */
	cpu = dlpi_irq_to_cpuid(d, &flags);
	raw_spin_lock(&gic_data_rdist_cpu(cpu)->rd_lock);
	rdbase = per_cpu_ptr(gic_rdists->rdist, cpu)->rd_base;
	gic_write_lpir(val, rdbase + GICR_INVLPIR);
	wait_for_syncr(rdbase);
	raw_spin_unlock(&gic_data_rdist_cpu(cpu)->rd_lock);
}

static int dlpi_alloc_device_irq(struct direct_lpi_device *dlpi_dev,
				 int nvecs, irq_hw_number_t *hwirq)
{
	int idx;

	/* Find a free LPI region in lpi_map and allocate them. */
	idx = bitmap_find_free_region(dlpi_dev->event_map.lpi_map,
				      dlpi_dev->event_map.nr_lpis,
				      get_count_order(nvecs));
	if (idx < 0)
		return -ENOSPC;

	*hwirq = dlpi_dev->event_map.lpi_base + idx;

	return 0;
}

static void no_lpi_update_config(struct irq_data *d, u8 clr, u8 set)
{
	lpi_write_config(d, clr, set);
	dlpi_direct_lpi_inv(d);
}

static void dlpi_unmask_irq(struct irq_data *d)
{
	no_lpi_update_config(d, 0, LPI_PROP_ENABLED);
}

static void dlpi_mask_irq(struct irq_data *d)
{
	no_lpi_update_config(d, LPI_PROP_ENABLED, 0);
}

static int dlpi_select_cpu(struct irq_data *d,
			   const struct cpumask *aff_mask)
{
	cpumask_var_t tmpmask;
	int cpu;

	if (!alloc_cpumask_var(&tmpmask, GFP_ATOMIC))
		return -ENOMEM;

	/* There is no NUMA node affliation */
	if (!irqd_affinity_is_managed(d)) {
		/* Try the intersection of the affinity and online masks */
		cpumask_and(tmpmask, aff_mask, cpu_online_mask);

		/* If that doesn't fly, the online mask is the last resort */
		if (cpumask_empty(tmpmask))
			cpumask_copy(tmpmask, cpu_online_mask);

		cpu = cpumask_pick_least_loaded(d, tmpmask);
	} else {
		cpumask_and(tmpmask, irq_data_get_affinity_mask(d), cpu_online_mask);
		cpu = cpumask_pick_least_loaded(d, tmpmask);
	}

	free_cpumask_var(tmpmask);
	pr_debug("IRQ%d -> %*pbl CPU%d\n", d->irq, cpumask_pr_args(aff_mask), cpu);

	return cpu;
}

static int dlpi_set_affinity(struct irq_data *d, const struct cpumask *mask_val,
			     bool force)
{
	struct direct_lpi_device *dlpi_dev = irq_data_get_irq_chip_data(d);
	u32 id = dlpi_get_event_id(d);
	int cpu, prev_cpu;

	/*
	 * A forwarded interrupt should use irq_set_vcpu_affinity. Anyways,
	 * vcpu is not supported for Direct LPI, as it requires an ITS.
	 */
	if (irqd_is_forwarded_to_vcpu(d))
		return -EINVAL;

	prev_cpu = dlpi_dev->event_map.col_map[id];
	its_dec_lpi_count(d, prev_cpu);

	if (!force)
		cpu = dlpi_select_cpu(d, mask_val);
	else
		cpu = cpumask_pick_least_loaded(d, mask_val);

	if (cpu < 0 || cpu >= nr_cpu_ids)
		goto err;

	/* don't set the affinity when the target cpu is same as current one */
	if (cpu != prev_cpu) {
		dlpi_dev->event_map.col_map[id] = cpu;
		irq_data_update_effective_affinity(d, cpumask_of(cpu));
	}

	its_inc_lpi_count(d, cpu);

	return IRQ_SET_MASK_OK_DONE;

err:
	its_inc_lpi_count(d, prev_cpu);
	return -EINVAL;
}

static u64 dlpi_get_msi_base(struct irq_data *d)
{
	u64 addr;
	int cpu;
	unsigned long flags;

	cpu = dlpi_irq_to_cpuid(d, &flags);
	addr = (u64)(per_cpu_ptr(gic_rdists->rdist, cpu)->rd_base +
		     GICR_SETLPIR);

	return addr;
}

/*
 * As per the spec, MSI address is the address of the target processor's
 * GICR_SETLPIR location.
 */
static void dlpi_irq_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	u64 addr;

	addr = dlpi_get_msi_base(d);

	msg->address_lo		= lower_32_bits(addr);
	msg->address_hi		= upper_32_bits(addr);
	msg->data		= dlpi_get_event_id(d);

	iommu_dma_compose_msi_msg(irq_data_get_msi_desc(d), msg);
}

static int dlpi_irq_set_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which,
				     bool state)
{
	if (which != IRQCHIP_STATE_PENDING)
		return -EINVAL;

	return 0;
}

static int dlpi_irq_retrigger(struct irq_data *d)
{
	return !dlpi_irq_set_irqchip_state(d, IRQCHIP_STATE_PENDING, true);
}

static int dlpi_irq_set_vcpu_affinity(struct irq_data *d, void *vcpu_info)
{
	/* vCPU support requires an ITS */
	return -EINVAL;
}

static struct irq_chip dlpi_irq_chip = {
	.name			= "Direct LPI",
	.irq_mask		= dlpi_mask_irq,
	.irq_unmask		= dlpi_unmask_irq,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= dlpi_set_affinity,
	.irq_compose_msi_msg	= dlpi_irq_compose_msi_msg,
	.irq_set_irqchip_state	= dlpi_irq_set_irqchip_state,
	.irq_retrigger		= dlpi_irq_retrigger,
	.irq_set_vcpu_affinity	= dlpi_irq_set_vcpu_affinity,
};

static int dlpi_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *args)
{
	msi_alloc_info_t *info = args;
	struct direct_lpi_device *dlpi_dev = info->scratchpad[0].ptr;
	struct irq_data *irqd;
	irq_hw_number_t hwirq;
	int err;
	int i;

	err = dlpi_alloc_device_irq(dlpi_dev, nr_irqs, &hwirq);
	if (err)
		return err;

	/*
	 * TODO: Need to call 'iommu_dma_prepare_msi' to prepare for DMA,
	 *	 but, that requires an MSI address. And, for Direct LPI
	 *	 the MSI address comes from the Redistributor from
	 *	 'GICR_SETLPIR', which is per CPU and that is not known
	 *	 at the moment. Not sure what is the best way to handle
	 *	 this.
	 */

	/*
	err = iommu_dma_prepare_msi(info->desc, its->get_msi_base(its_dev));
	if (err)
		return err;
	*/

	for (i = 0; i < nr_irqs; i++) {
		err = its_irq_gic_domain_alloc(domain, virq + i, hwirq + i);
		if (err)
			return err;

		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &dlpi_irq_chip, dlpi_dev);
		irqd = irq_get_irq_data(virq + i);
		irqd_set_single_target(irqd);
		irqd_set_affinity_on_activate(irqd);
		pr_debug("ID:%d pID:%d vID:%d\n",
			 (int)(hwirq + i - dlpi_dev->event_map.lpi_base),
			 (int)(hwirq + i), virq + i);
	}

	return 0;
}

static void dlpi_free_device(struct direct_lpi_device *dlpi_dev)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&dlpi_dev->dlpi->lock, flags);
	list_del(&dlpi_dev->entry);
	raw_spin_unlock_irqrestore(&dlpi_dev->dlpi->lock, flags);
	kfree(dlpi_dev->event_map.col_map);
	kfree(dlpi_dev->event_map.lpi_map);
	kfree(dlpi_dev);
}

static void dlpi_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct direct_lpi_device *dlpi_dev = irq_data_get_irq_chip_data(d);
	int i;
	struct direct_lpi *dlpi = dlpi_dev->dlpi;

	bitmap_release_region(dlpi_dev->event_map.lpi_map,
			      dlpi_get_event_id(irq_domain_get_irq_data(domain, virq)),
			      get_count_order(nr_irqs));

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *data = irq_domain_get_irq_data(domain,
								virq + i);
		/* Nuke the entry in the domain */
		irq_domain_reset_irq_data(data);
	}

	mutex_lock(&dlpi->dev_alloc_lock);

	/*
	 * If all interrupts have been freed, start mopping the
	 * floor. This is conditionned on the device not being shared.
	 */
	if (!dlpi_dev->shared &&
	    bitmap_empty(dlpi_dev->event_map.lpi_map,
			 dlpi_dev->event_map.nr_lpis)) {
		its_lpi_free(dlpi_dev->event_map.lpi_map,
			     dlpi_dev->event_map.lpi_base,
			     dlpi_dev->event_map.nr_lpis);

		dlpi_free_device(dlpi_dev);
	}

	mutex_unlock(&dlpi->dev_alloc_lock);
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static int dlpi_irq_domain_activate(struct irq_domain *domain,
				   struct irq_data *d, bool reserve)
{
	struct direct_lpi_device *dlpi_dev = irq_data_get_irq_chip_data(d);
	u32 event;
	int cpu;

	event = dlpi_get_event_id(d);
	cpu = dlpi_select_cpu(d, cpu_online_mask);
	if (cpu < 0 || cpu >= nr_cpu_ids)
		return -EINVAL;

	its_inc_lpi_count(d, cpu);
	dlpi_dev->event_map.col_map[event] = cpu;
	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return 0;
}

static void dlpi_irq_domain_deactivate(struct irq_domain *domain,
				      struct irq_data *d)
{
	struct direct_lpi_device *dlpi_dev = irq_data_get_irq_chip_data(d);
	u32 event = dlpi_get_event_id(d);

	its_dec_lpi_count(d, dlpi_dev->event_map.col_map[event]);
}

static struct direct_lpi_device *dlpi_create_device(struct direct_lpi *dlpi,
					u32 dev_id, int nvecs, bool alloc_lpis)
{
	struct direct_lpi_device *dlpi_dev = NULL;
	unsigned long *lpi_map = NULL;
	u16 *col_map = NULL;
	int lpi_base;
	int nr_lpis;
	unsigned long flags;

	if (WARN_ON(!is_power_of_2(nvecs)))
		nvecs = roundup_pow_of_two(nvecs);

	dlpi_dev = kzalloc(sizeof(*dlpi_dev), GFP_KERNEL);
	if (!dlpi_dev)
		return NULL;

	lpi_map = its_lpi_alloc(nvecs, &lpi_base, &nr_lpis);
	if (!lpi_map) {
		kfree(dlpi_dev);
		return NULL;
	}

	col_map = kcalloc(nr_lpis, sizeof(*col_map), GFP_KERNEL);
	if (!col_map) {
		kfree(dlpi_dev);
		kfree(lpi_map);
		return NULL;
	}

	dlpi_dev->dlpi = dlpi;
	dlpi_dev->event_map.lpi_map = lpi_map;
	dlpi_dev->event_map.col_map = col_map;
	dlpi_dev->event_map.lpi_base = lpi_base;
	dlpi_dev->event_map.nr_lpis = nr_lpis;
	dlpi_dev->device_id = dev_id;

	raw_spin_lock_irqsave(&dlpi->lock, flags);
	list_add(&dlpi_dev->entry, &dlpi->device_list);
	raw_spin_unlock_irqrestore(&dlpi->lock, flags);

	return dlpi_dev;
}

static struct direct_lpi_device *dlpi_find_device(struct direct_lpi *dlpi, u32 dev_id)
{
	struct direct_lpi_device *dlpi_dev = NULL, *tmp;
	unsigned long flags;

	raw_spin_lock_irqsave(&dlpi->lock, flags);
	list_for_each_entry(tmp, &dlpi->device_list, entry) {
		if (tmp->device_id == dev_id) {
			dlpi_dev = tmp;
			break;
		}
	}

	raw_spin_unlock_irqrestore(&dlpi->lock, flags);

	return dlpi_dev;
}

static int dlpi_msi_prepare(struct irq_domain *domain, struct device *dev,
			   int nvec, msi_alloc_info_t *info)
{
	struct direct_lpi_device *dlpi_dev;
	struct direct_lpi *dlpi;
	struct msi_domain_info *msi_info;
	u32 dev_id;
	int err = 0;

	/*
	 * We ignore "dev" entirely, and rely on the dev_id that has
	 * been passed via the scratchpad. This limits this domain's
	 * usefulness to upper layers that definitely know that they
	 * are built on top of the ITS.
	 */
	dev_id = info->scratchpad[0].ul;
	msi_info = msi_get_domain_info(domain);
	dlpi = msi_info->data;

	mutex_lock(&dlpi->dev_alloc_lock);
	dlpi_dev = dlpi_find_device(dlpi, dev_id);
	if (dlpi_dev) {
		/*
		 * We already have seen this ID, probably through
		 * another alias (PCI bridge of some sort). No need to
		 * create the device.
		 */
		dlpi_dev->shared = true;
		pr_debug("Reusing ITT for devID %x\n", dev_id);
		goto out;
	}

	dlpi_dev = dlpi_create_device(dlpi, dev_id, nvec, true);
	if (!dlpi_dev) {
		err = -ENOMEM;
		goto out;
	}

out:
	mutex_unlock(&dlpi->dev_alloc_lock);
	info->scratchpad[0].ptr = dlpi_dev;

	return err;
}
