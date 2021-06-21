/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for DFL driver and device API
 *
 * Copyright (C) 2020 Intel Corporation, Inc.
 */

#ifndef __LINUX_DFL_H
#define __LINUX_DFL_H

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>

/**
 * enum dfl_id_type - define the DFL FIU types
 */
enum dfl_id_type {
	FME_ID = 0,
	PORT_ID = 1,
	DFL_ID_MAX,
};

/**
 * struct dfl_device - represent an dfl device on dfl bus
 *
 * @dev: generic device interface.
 * @id: id of the dfl device.
 * @type: type of DFL FIU of the device. See enum dfl_id_type.
 * @feature_id: feature identifier local to its DFL FIU type.
 * @mmio_res: mmio resource of this dfl device.
 * @irqs: list of Linux IRQ numbers of this dfl device.
 * @num_irqs: number of IRQs supported by this dfl device.
 * @cdev: pointer to DFL FPGA container device this dfl device belongs to.
 * @id_entry: matched id entry in dfl driver's id table.
 */
struct dfl_device {
	struct device dev;
	int id;
	u16 type;
	u16 feature_id;
	struct resource mmio_res;
	int *irqs;
	unsigned int num_irqs;
	struct dfl_fpga_cdev *cdev;
	const struct dfl_device_id *id_entry;
};

/**
 * struct dfl_driver - represent an dfl device driver
 *
 * @drv: driver model structure.
 * @id_table: pointer to table of device IDs the driver is interested in.
 *	      { } member terminated.
 * @probe: mandatory callback for device binding.
 * @remove: callback for device unbinding.
 */
struct dfl_driver {
	struct device_driver drv;
	const struct dfl_device_id *id_table;

	int (*probe)(struct dfl_device *dfl_dev);
	void (*remove)(struct dfl_device *dfl_dev);
};

#define to_dfl_dev(d) container_of(d, struct dfl_device, dev)
#define to_dfl_drv(d) container_of(d, struct dfl_driver, drv)

/*
 * use a macro to avoid include chaining to get THIS_MODULE.
 */
#define dfl_driver_register(drv) \
	__dfl_driver_register(drv, THIS_MODULE)
int __dfl_driver_register(struct dfl_driver *dfl_drv, struct module *owner);
void dfl_driver_unregister(struct dfl_driver *dfl_drv);

/*
 * module_dfl_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit().
 */
#define module_dfl_driver(__dfl_driver) \
	module_driver(__dfl_driver, dfl_driver_register, \
		      dfl_driver_unregister)

/*
 * Device Feature Header Register Set
 *
 * For FIUs, they all have DFH + GUID + NEXT_AFU as common header registers.
 * For AFUs, they have DFH + GUID as common header registers.
 * For private features, they only have DFH register as common header.
 */
#define DFH                     0x0
#define GUID_L                  0x8
#define GUID_H                  0x10
#define NEXT_AFU                0x18

#define DFH_SIZE                0x8

/* Device Feature Header Register Bitfield */
#define DFH_ID                  GENMASK_ULL(11, 0)      /* Feature ID */
#define DFH_ID_FIU_FME          0
#define DFH_ID_FIU_PORT         1
#define DFH_REVISION            GENMASK_ULL(15, 12)
#define DFH_NEXT_HDR_OFST       GENMASK_ULL(39, 16)     /* Offset to next DFH */
#define DFH_EOL                 BIT_ULL(40)             /* End of list */
#define DFH_TYPE                GENMASK_ULL(63, 60)     /* Feature type */
#define DFH_TYPE_AFU            1
#define DFH_TYPE_PRIVATE        3
#define DFH_TYPE_FIU            4

/* Function to read from DFH and check if the Feature type is FME */
static inline bool dfl_feature_is_fme(void __iomem *base)
{
	u64 v = readq(base + DFH);

	return (FIELD_GET(DFH_TYPE, v) == DFH_TYPE_FIU) &&
		(FIELD_GET(DFH_ID, v) == DFH_ID_FIU_FME);
}

/* Function to read from DFH and check if the Feature type is port*/
static inline bool dfl_feature_is_port(void __iomem *base)
{
	u64 v = readq(base + DFH);

	return (FIELD_GET(DFH_TYPE, v) == DFH_TYPE_FIU) &&
		 (FIELD_GET(DFH_ID, v) == DFH_ID_FIU_PORT);
}

/* Function to read feature revision from DFH */
static inline u8 dfl_feature_revision(void __iomem *base)
{
	return (u8)FIELD_GET(DFH_REVISION, readq(base + DFH));
}

#endif /* __LINUX_DFL_H */
