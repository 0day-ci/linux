/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2021 Intel Corporation. */
#ifndef __CXL_ACPI_H__
#define __CXL_ACPI_H__

#ifndef ACPI_CEDT_CHBS_VERSION_CXL20
/*
 * NOTE: These definitions are temporary and to be deleted in v5.14-rc1
 * when the identical definitions become available from
 * include/acpi/actbl1.h.
 */

#define ACPI_CEDT_TYPE_CFMWS 1
#define ACPI_CEDT_TYPE_RESERVED 2

#define ACPI_CEDT_CHBS_VERSION_CXL11 (0)
#define ACPI_CEDT_CHBS_VERSION_CXL20 (1)

#define ACPI_CEDT_CHBS_LENGTH_CXL11 (0x2000)
#define ACPI_CEDT_CHBS_LENGTH_CXL20 (0x10000)

struct acpi_cedt_cfmws {
	struct acpi_cedt_header header;
	u32 reserved1;
	u64 base_hpa;
	u64 window_size;
	u8 interleave_ways;
	u8 interleave_arithmetic;
	u16 reserved2;
	u32 granularity;
	u16 restrictions;
	u16 qtg_id;
	u32 interleave_targets[];
};

/* Values for Interleave Arithmetic field above */

#define ACPI_CEDT_CFMWS_ARITHMETIC_MODULO (0)

/* Values for Restrictions field above */

#define ACPI_CEDT_CFMWS_RESTRICT_TYPE2 (1)
#define ACPI_CEDT_CFMWS_RESTRICT_TYPE3 (1 << 1)
#define ACPI_CEDT_CFMWS_RESTRICT_VOLATILE (1 << 2)
#define ACPI_CEDT_CFMWS_RESTRICT_PMEM (1 << 3)
#define ACPI_CEDT_CFMWS_RESTRICT_FIXED (1 << 4)
#endif /* ACPI_CEDT_CHBS_VERSION_CXL20 */
#endif /* __CXL_ACPI_H__ */
