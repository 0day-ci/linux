/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef __CXL_H__
#define __CXL_H__

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/pci-doe.h>

/* CXL 2.0 8.2.8.1 Device Capabilities Array Register */
#define CXLDEV_CAP_ARRAY_OFFSET 0x0
#define   CXLDEV_CAP_ARRAY_CAP_ID 0
#define   CXLDEV_CAP_ARRAY_ID_MASK GENMASK_ULL(15, 0)
#define   CXLDEV_CAP_ARRAY_COUNT_MASK GENMASK_ULL(47, 32)
/* CXL 2.0 8.2.8.2 CXL Device Capability Header Register */
#define CXLDEV_CAP_HDR_CAP_ID_MASK GENMASK(15, 0)
/* CXL 2.0 8.2.8.2.1 CXL Device Capabilities */
#define CXLDEV_CAP_CAP_ID_DEVICE_STATUS 0x1
#define CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX 0x2
#define CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX 0x3
#define CXLDEV_CAP_CAP_ID_MEMDEV 0x4000

/* CXL 2.0 8.2.8.4 Mailbox Registers */
#define CXLDEV_MBOX_CAPS_OFFSET 0x00
#define   CXLDEV_MBOX_CAP_PAYLOAD_SIZE_MASK GENMASK(4, 0)
#define CXLDEV_MBOX_CTRL_OFFSET 0x04
#define   CXLDEV_MBOX_CTRL_DOORBELL BIT(0)
#define CXLDEV_MBOX_CMD_OFFSET 0x08
#define   CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK GENMASK_ULL(15, 0)
#define   CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK GENMASK_ULL(36, 16)
#define CXLDEV_MBOX_STATUS_OFFSET 0x10
#define   CXLDEV_MBOX_STATUS_RET_CODE_MASK GENMASK_ULL(47, 32)
#define CXLDEV_MBOX_BG_CMD_STATUS_OFFSET 0x18
#define CXLDEV_MBOX_PAYLOAD_OFFSET 0x20

/*
 * CXL_DEVICE_REGS - Common set of CXL Device register block base pointers
 * @status: CXL 2.0 8.2.8.3 Device Status Registers
 * @mbox: CXL 2.0 8.2.8.4 Mailbox Registers
 * @memdev: CXL 2.0 8.2.8.5 Memory Device Registers
 */
#define CXL_DEVICE_REGS() \
	void __iomem *status; \
	void __iomem *mbox; \
	void __iomem *memdev

/* See note for 'struct cxl_regs' for the rationale of this organization */
struct cxl_device_regs {
	CXL_DEVICE_REGS();
};

/*
 * Note, the anonymous union organization allows for per
 * register-block-type helper routines, without requiring block-type
 * agnostic code to include the prefix. I.e.
 * cxl_setup_device_regs(&cxlm->regs.dev) vs readl(cxlm->regs.mbox).
 * The specificity reads naturally from left-to-right.
 */
struct cxl_regs {
	union {
		struct {
			CXL_DEVICE_REGS();
		};
		struct cxl_device_regs device_regs;
	};
};

void cxl_setup_device_regs(struct device *dev, void __iomem *base,
			   struct cxl_device_regs *regs);

/*
 * Address space properties derived from:
 * CXL 2.0 8.2.5.12.7 CXL HDM Decoder 0 Control Register
 */
#define CXL_ADDRSPACE_RAM   BIT(0)
#define CXL_ADDRSPACE_PMEM  BIT(1)
#define CXL_ADDRSPACE_TYPE2 BIT(2)
#define CXL_ADDRSPACE_TYPE3 BIT(3)
#define CXL_ADDRSPACE_MASK  GENMASK(3, 0)

#define CXL_DOE_PROTOCOL_COMPLIANCE 0
#define CXL_DOE_PROTOCOL_TABLE_ACCESS 2

/* Common to request and response */
#define CXL_DOE_TABLE_ACCESS_3_CODE GENMASK(7, 0)
#define   CXL_DOE_TABLE_ACCESS_3_CODE_READ 0
#define CXL_DOE_TABLE_ACCESS_3_TYPE GENMASK(15, 8)
#define   CXL_DOE_TABLE_ACCESS_3_TYPE_CDAT 0
#define CXL_DOE_TABLE_ACCESS_3_ENTRY_HANDLE GENMASK(31, 16)

extern struct bus_type cxl_bus_type;
#endif /* __CXL_H__ */
