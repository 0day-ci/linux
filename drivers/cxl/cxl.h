/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef __CXL_H__
#define __CXL_H__

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>

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

/* See note for 'struct cxl_regs' for the rationale of this organization */
#define CXL_DEVICE_REGS() \
	void __iomem *status; \
	void __iomem *mbox; \
	void __iomem *memdev

/**
 * struct cxl_device_regs - Common container of CXL Device register
 * 			    block base pointers
 * @status: CXL 2.0 8.2.8.3 Device Status Registers
 * @mbox: CXL 2.0 8.2.8.4 Mailbox Registers
 * @memdev: CXL 2.0 8.2.8.5 Memory Device Registers
 */
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

struct cxl_address_space {
	struct range range;
	int interleave_size;
	unsigned long flags;
	unsigned long targets;
};

struct cxl_address_space_dev {
	struct device dev;
	struct resource res;
	struct cxl_address_space *address_space;
};

/**
 * struct cxl_port - object representing a root, upstream, or downstream port
 * @dev: this port's device
 * @port_host: PCI or platform device host of the CXL capability
 * @id: id for port device-name
 * @target_id: this port's HDM decoder id in the parent port
 * @component_regs_phys: component register capability array base address
 */
struct cxl_port {
	struct device dev;
	struct device *port_host;
	int id;
	int target_id;
	resource_size_t component_regs_phys;
};

/*
 * struct cxl_root - platform object parent of CXL host bridges
 *
 * A cxl_root object represents a set of address spaces that are
 * interleaved across a set of child host bridges, but never interleaved
 * to another cxl_root object. It contains a cxl_port that is a special
 * case in that it does not have a parent port and related HDMs, instead
 * its decode is derived from the root (platform firmware defined)
 * address space description. Not to be confused with CXL Root Ports
 * that are the PCIE Root Ports within PCIE Host Bridges that are
 * flagged by platform firmware (ACPI0016 on ACPI platforms) as having
 * CXL capabilities.
 */
struct cxl_root {
	struct cxl_port port;
	int nr_spaces;
	struct cxl_address_space address_space[];
};

struct cxl_root *to_cxl_root(struct device *dev);
struct cxl_port *to_cxl_port(struct device *dev);
struct cxl_address_space_dev *to_cxl_address_space(struct device *dev);
struct cxl_root *devm_cxl_add_root(struct device *parent,
				   struct cxl_address_space *cxl_space,
				   int nr_spaces);
extern struct bus_type cxl_bus_type;
#endif /* __CXL_H__ */
