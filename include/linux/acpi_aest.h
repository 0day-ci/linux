/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AEST_H
#define AEST_H

#include <acpi/actbl.h>

#define ACPI_SIG_AEST			"AEST"	/* ARM Error Source Table */

#define AEST_INTERRUPT_MODE		BIT(0)

#define AEST_MAX_PPI			4

#define AEST_PROC_GLOBAL		BIT(0)
#define AEST_PROC_SHARED		BIT(1)

#define AEST_INTERFACE_SHARED		BIT(0)
#define AEST_INTERFACE_CLEAR_MISC	BIT(1)

struct aest_interface_data {
	u8 type;
	u16 start;
	u16 end;
	u32 flags;
	u64 implemented;
	u64 status_reporting;
	struct ras_ext_regs *regs;
};

union acpi_aest_processor_data {
	struct acpi_aest_processor_cache cache_data;
	struct acpi_aest_processor_tlb tlb_data;
	struct acpi_aest_processor_generic generic_data;
};

union aest_node_spec {
	struct acpi_aest_processor processor;
	struct acpi_aest_memory memory;
	struct acpi_aest_smmu smmu;
	struct acpi_aest_vendor vendor;
	struct acpi_aest_gic gic;
};

struct aest_node_data {
	u8 node_type;
	struct aest_interface_data interface;
	union aest_node_spec data;
	union acpi_aest_processor_data proc_data;
};

#endif /* AEST_H */
