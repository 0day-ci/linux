// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Error Source Table Support
 *
 * Copyright (c) 2021, Ampere Computing LLC
 */

#include <linux/acpi.h>
#include <linux/acpi_aest.h>
#include <linux/cpuhotplug.h>
#include <linux/kernel.h>

#include <acpi/actbl.h>

#include <asm/ras.h>

#include <ras/ras_event.h>

#undef pr_fmt
#define pr_fmt(fmt) "ACPI AEST: " fmt

static struct acpi_table_header *aest_table;

static struct aest_node_data __percpu **ppi_data;
static int ppi_irqs[AEST_MAX_PPI];
static u8 num_ppi;
static u8 ppi_idx;

static bool aest_mmio_ras_misc23_present(u64 base_addr)
{
	u32 val;

	val = readl((void *) (base_addr + ERRDEVARCH_OFFSET));
	val <<= ERRDEVARCH_REV_SHIFT;
	val &= ERRDEVARCH_REV_MASK;

	return val >= RAS_REV_v1_1;
}

static void aest_print(struct aest_node_data *data, struct ras_ext_regs regs,
		       int index, bool misc23_present)
{
	/* No more than 2 corrected messages every 5 seconds */
	static DEFINE_RATELIMIT_STATE(ratelimit_corrected, 5*HZ, 2);

	if (regs.err_status & ERR_STATUS_UE ||
	    regs.err_status & ERR_STATUS_DE ||
	    __ratelimit(&ratelimit_corrected)) {
		switch (data->node_type) {
		case ACPI_AEST_PROCESSOR_ERROR_NODE:
			if (!(data->data.processor.flags & AEST_PROC_GLOBAL) &&
			    !(data->data.processor.flags & AEST_PROC_SHARED))
				pr_err("error from processor 0x%x\n",
				       data->data.processor.processor_id);
			break;
		case ACPI_AEST_MEMORY_ERROR_NODE:
			pr_err("error from memory at SRAT proximity domain 0x%x\n",
			       data->data.memory.srat_proximity_domain);
			break;
		case ACPI_AEST_SMMU_ERROR_NODE:
			pr_err("error from SMMU IORT node 0x%x subcomponent 0x%x\n",
			       data->data.smmu.iort_node_reference,
			       data->data.smmu.subcomponent_reference);
			break;
		case ACPI_AEST_VENDOR_ERROR_NODE:
			pr_err("error from vendor hid 0x%x uid 0x%x\n",
			       data->data.vendor.acpi_hid, data->data.vendor.acpi_uid);
			break;
		case ACPI_AEST_GIC_ERROR_NODE:
			pr_err("error from GIC type 0x%x instance 0x%x\n",
			       data->data.gic.interface_type, data->data.gic.instance_id);
		}

		arch_arm_ras_print_error(&regs, index, misc23_present);
	}
}

static void aest_proc(struct aest_node_data *data)
{
	struct ras_ext_regs *regs_p, regs = {0};
	bool misc23_present;
	bool fatal = false;
	u64 errgsr = 0;
	int i;

	/*
	 * Currently SR based handling is done through the architected
	 * discovery exposed through SRs. That may change in the future
	 * if there is supplemental information in the AEST that is
	 * needed.
	 */
	if (data->interface.type == ACPI_AEST_NODE_SYSTEM_REGISTER) {
		arch_arm_ras_report_error(data->interface.implemented,
					  data->interface.flags & AEST_INTERFACE_CLEAR_MISC);
		return;
	}

	regs_p = data->interface.regs;
	errgsr = readq((void *) (((u64) regs_p) + ERRGSR_OFFSET));

	for (i = data->interface.start; i < data->interface.end; i++) {
		if (!(data->interface.implemented & BIT(i)))
			continue;

		if (!(data->interface.status_reporting & BIT(i)) && !(errgsr & BIT(i)))
			continue;

		regs.err_status = readq(&regs_p[i].err_status);
		if (!(regs.err_status & ERR_STATUS_V))
			continue;

		if (regs.err_status & ERR_STATUS_AV)
			regs.err_addr = readq(&regs_p[i].err_addr);

		regs.err_fr = readq(&regs_p[i].err_fr);
		regs.err_ctlr = readq(&regs_p[i].err_ctlr);

		if (regs.err_status & ERR_STATUS_MV) {
			misc23_present = aest_mmio_ras_misc23_present((u64) regs_p);
			regs.err_misc0 = readq(&regs_p[i].err_misc0);
			regs.err_misc1 = readq(&regs_p[i].err_misc1);

			if (misc23_present) {
				regs.err_misc2 = readq(&regs_p[i].err_misc2);
				regs.err_misc3 = readq(&regs_p[i].err_misc3);
			}
		}

		aest_print(data, regs, i, misc23_present);

		trace_arm_ras_ext_event(data->node_type, data->data.vendor.acpi_hid,
					data->data.vendor.acpi_uid, i, &regs);

		if (regs.err_status & ERR_STATUS_UE)
			fatal = true;

		regs.err_status = arch_arm_ras_get_status_clear_value(regs.err_status);
		writeq(regs.err_status, &regs_p[i].err_status);

		if (data->interface.flags & AEST_INTERFACE_CLEAR_MISC) {
			writeq(0x0, &regs_p[i].err_misc0);
			writeq(0x0, &regs_p[i].err_misc1);

			if (misc23_present) {
				writeq(0x0, &regs_p[i].err_misc2);
				writeq(0x0, &regs_p[i].err_misc3);
			}
		}
	}

	if (fatal)
		panic("AEST: uncorrectable error encountered");
}

static irqreturn_t aest_irq_func(int irq, void *input)
{
	struct aest_node_data *data = input;

	aest_proc(data);

	return IRQ_HANDLED;
}

static int __init aest_register_gsi(u32 gsi, int trigger, void *data)
{
	int cpu, irq;

	irq = acpi_register_gsi(NULL, gsi, trigger, ACPI_ACTIVE_HIGH);

	if (irq == -EINVAL) {
		pr_err("failed to map AEST GSI %d\n", gsi);
		return -EINVAL;
	}

	if (gsi < 16) {
		pr_err("invalid GSI %d\n", gsi);
		return -EINVAL;
	} else if (gsi < 32) {
		if (ppi_idx >= AEST_MAX_PPI) {
			pr_err("Unable to register PPI %d\n", gsi);
			return -EINVAL;
		}
		ppi_irqs[ppi_idx] = irq;
		enable_percpu_irq(irq, IRQ_TYPE_NONE);
		for_each_possible_cpu(cpu) {
			memcpy(per_cpu_ptr(ppi_data[ppi_idx], cpu), data,
			       sizeof(struct aest_node_data));
		}
		if (request_percpu_irq(irq, aest_irq_func, "AEST",
				       ppi_data[ppi_idx++])) {
			pr_err("failed to register AEST IRQ %d\n", irq);
			return -EINVAL;
		}
	} else if (gsi < 1020) {
		if (request_irq(irq, aest_irq_func, IRQF_SHARED, "AEST",
				data)) {
			pr_err("failed to register AEST IRQ %d\n", irq);
			return -EINVAL;
		}
	} else {
		pr_err("invalid GSI %d\n", gsi);
		return -EINVAL;
	}

	return 0;
}

static int __init aest_init_interrupts(struct acpi_aest_hdr *node,
				       struct aest_node_data *data)
{
	struct acpi_aest_node_interrupt *interrupt;
	int i, trigger, ret = 0;

	interrupt = ACPI_ADD_PTR(struct acpi_aest_node_interrupt, node,
				 node->node_interrupt_offset);

	for (i = 0; i < node->node_interrupt_count; i++, interrupt++) {
		trigger = (interrupt->flags & AEST_INTERRUPT_MODE) ?
			  ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE;
		if (aest_register_gsi(interrupt->gsiv, trigger, data))
			ret = -EINVAL;
	}

	return ret;
}

static int __init aest_init_interface(struct acpi_aest_hdr *node,
				       struct aest_node_data *data)
{
	struct acpi_aest_node_interface *interface;
	struct resource *res;
	int size;

	interface = ACPI_ADD_PTR(struct acpi_aest_node_interface, node,
				 node->node_interface_offset);

	if (interface->type >= ACPI_AEST_XFACE_RESERVED) {
		pr_err("invalid interface type: %d\n", interface->type);
		return -EINVAL;
	}

	data->interface.type = interface->type;
	data->interface.start = interface->error_record_index;
	data->interface.end = interface->error_record_index + interface->error_record_count;
	data->interface.flags = interface->flags;
	data->interface.implemented = interface->error_record_implemented;
	data->interface.status_reporting = interface->error_status_reporting;

	/*
	 * Currently SR based handling is done through the architected
	 * discovery exposed through SRs. That may change in the future
	 * if there is supplemental information in the AEST that is
	 * needed.
	 */
	if (interface->type == ACPI_AEST_NODE_SYSTEM_REGISTER)
		return 0;

	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	size = interface->error_record_count * sizeof(struct ras_ext_regs);
	res->name = "AEST";
	res->start = interface->address;
	res->end = res->start + size;
	res->flags = IORESOURCE_MEM;
	if (request_resource_conflict(&iomem_resource, res)) {
		pr_err("unable to request region starting at 0x%llx\n",
			res->start);
		kfree(res);
		return -EEXIST;
	}

	data->interface.regs = ioremap(interface->address, size);
	if (data->interface.regs == NULL) {
		kfree(res);
		return -EINVAL;
	}

	return 0;
}

static int __init aest_init_node(struct acpi_aest_hdr *node)
{
	union acpi_aest_processor_data *proc_data;
	union aest_node_spec *node_spec;
	struct aest_node_data *data;
	int ret;

	data = kzalloc(sizeof(struct aest_node_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->node_type = node->type;

	node_spec = ACPI_ADD_PTR(union aest_node_spec, node, node->node_specific_offset);

	switch (node->type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE:
		memcpy(&data->data, node_spec, sizeof(struct acpi_aest_processor));
		break;
	case ACPI_AEST_MEMORY_ERROR_NODE:
		memcpy(&data->data, node_spec, sizeof(struct acpi_aest_memory));
		break;
	case ACPI_AEST_SMMU_ERROR_NODE:
		memcpy(&data->data, node_spec, sizeof(struct acpi_aest_smmu));
		break;
	case ACPI_AEST_VENDOR_ERROR_NODE:
		memcpy(&data->data, node_spec, sizeof(struct acpi_aest_vendor));
		break;
	case ACPI_AEST_GIC_ERROR_NODE:
		memcpy(&data->data, node_spec, sizeof(struct acpi_aest_gic));
		break;
	default:
		kfree(data);
		return -EINVAL;
	}

	if (node->type == ACPI_AEST_PROCESSOR_ERROR_NODE) {
		proc_data = ACPI_ADD_PTR(union acpi_aest_processor_data, node_spec,
					 sizeof(acpi_aest_processor));

		switch (data->data.processor.resource_type) {
		case ACPI_AEST_CACHE_RESOURCE:
			memcpy(&data->proc_data, proc_data,
			       sizeof(struct acpi_aest_processor_cache));
			break;
		case ACPI_AEST_TLB_RESOURCE:
			memcpy(&data->proc_data, proc_data,
			       sizeof(struct acpi_aest_processor_tlb));
			break;
		case ACPI_AEST_GENERIC_RESOURCE:
			memcpy(&data->proc_data, proc_data,
			       sizeof(struct acpi_aest_processor_generic));
			break;
		}
	}

	ret = aest_init_interface(node, data);
	if (ret) {
		kfree(data);
		return ret;
	}

	return aest_init_interrupts(node, data);
}

static void aest_count_ppi(struct acpi_aest_hdr *node)
{
	struct acpi_aest_node_interrupt *interrupt;
	int i;

	interrupt = ACPI_ADD_PTR(struct acpi_aest_node_interrupt, node,
				 node->node_interrupt_offset);

	for (i = 0; i < node->node_interrupt_count; i++, interrupt++) {
		if (interrupt->gsiv >= 16 && interrupt->gsiv < 32)
			num_ppi++;
	}
}

static int aest_starting_cpu(unsigned int cpu)
{
	int i;

	for (i = 0; i < num_ppi; i++)
		enable_percpu_irq(ppi_irqs[i], IRQ_TYPE_NONE);

	return 0;
}

static int aest_dying_cpu(unsigned int cpu)
{
	return 0;
}

int __init acpi_aest_init(void)
{
	struct acpi_aest_hdr *aest_node, *aest_end;
	struct acpi_table_aest *aest;
	int i, ret = 0;

	if (acpi_disabled)
		return 0;

	if (!IS_ENABLED(CONFIG_ARM64_RAS_EXTN))
		return 0;

	if (ACPI_FAILURE(acpi_get_table(ACPI_SIG_AEST, 0, &aest_table)))
		return -EINVAL;

	aest = (struct acpi_table_aest *)aest_table;

	/* Get the first AEST node */
	aest_node = ACPI_ADD_PTR(struct acpi_aest_hdr, aest,
				 sizeof(struct acpi_table_header));
	/* Pointer to the end of the AEST table */
	aest_end = ACPI_ADD_PTR(struct acpi_aest_hdr, aest,
				aest_table->length);

	while (aest_node < aest_end) {
		if (((u64)aest_node + aest_node->length) > (u64)aest_end) {
			pr_err("AEST node pointer overflow, bad table\n");
			return -EINVAL;
		}

		aest_count_ppi(aest_node);

		aest_node = ACPI_ADD_PTR(struct acpi_aest_hdr, aest_node,
					 aest_node->length);
	}

	if (num_ppi > AEST_MAX_PPI) {
		pr_err("Limiting PPI support to %d PPIs\n", AEST_MAX_PPI);
		num_ppi = AEST_MAX_PPI;
	}

	ppi_data = kcalloc(num_ppi, sizeof(struct aest_node_data *),
			   GFP_KERNEL);

	for (i = 0; i < num_ppi; i++) {
		ppi_data[i] = alloc_percpu(struct aest_node_data);
		if (!ppi_data[i]) {
			pr_err("Failed percpu allocation\n");
			ret = -ENOMEM;
			goto fail;
		}
	}

	aest_node = ACPI_ADD_PTR(struct acpi_aest_hdr, aest,
				 sizeof(struct acpi_table_header));

	while (aest_node < aest_end) {
		ret = aest_init_node(aest_node);
		if (ret)
			pr_err("failed to init node: %d", ret);

		aest_node = ACPI_ADD_PTR(struct acpi_aest_hdr, aest_node,
					 aest_node->length);
	}

	cpuhp_setup_state(CPUHP_AP_ARM_AEST_STARTING,
			  "drivers/acpi/arm64/aest:starting",
			  aest_starting_cpu, aest_dying_cpu);

	return 0;

fail:
	for (i = 0; i < num_ppi; i++)
		free_percpu(ppi_data[i]);
	kfree(ppi_data);
	return ret;
}

early_initcall(acpi_aest_init);
