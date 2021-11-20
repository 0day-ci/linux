// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "cxlmem.h"

/**
 * DOC: cxl port
 *
 * The port driver implements the set of functionality needed to allow full
 * decoder enumeration and routing. A CXL port is an abstraction of a CXL
 * component that implements some amount of CXL decoding of CXL.mem traffic.
 * As of the CXL 2.0 spec, this includes:
 *
 *	.. list-table:: CXL Components w/ Ports
 *		:widths: 25 25 50
 *		:header-rows: 1
 *
 *		* - component
 *		  - upstream
 *		  - downstream
 *		* - Hostbridge
 *		  - ACPI0016
 *		  - root port
 *		* - Switch
 *		  - Switch Upstream Port
 *		  - Switch Downstream Port
 *		* - Endpoint
 *		  - Endpoint Port
 *		  - N/A
 *
 * The primary service this driver provides is enumerating HDM decoders and
 * presenting APIs to other drivers to utilize the decoders.
 */

static struct workqueue_struct *cxl_port_wq;

struct cxl_port_data {
	struct cxl_component_regs regs;

	struct port_caps {
		unsigned int count;
		unsigned int tc;
		unsigned int interleave11_8;
		unsigned int interleave14_12;
	} caps;
};

static inline int to_interleave_granularity(u32 ctrl)
{
	int val = FIELD_GET(CXL_HDM_DECODER0_CTRL_IG_MASK, ctrl);

	return 256 << val;
}

static inline int to_interleave_ways(u32 ctrl)
{
	int val = FIELD_GET(CXL_HDM_DECODER0_CTRL_IW_MASK, ctrl);

	return 1 << val;
}

static void get_caps(struct cxl_port *port, struct cxl_port_data *cpd)
{
	void __iomem *hdm_decoder = cpd->regs.hdm_decoder;
	struct port_caps *caps = &cpd->caps;
	u32 hdm_cap;

	hdm_cap = readl(hdm_decoder + CXL_HDM_DECODER_CAP_OFFSET);

	caps->count = cxl_hdm_decoder_count(hdm_cap);
	caps->tc = FIELD_GET(CXL_HDM_DECODER_TARGET_COUNT_MASK, hdm_cap);
	caps->interleave11_8 =
		FIELD_GET(CXL_HDM_DECODER_INTERLEAVE_11_8, hdm_cap);
	caps->interleave14_12 =
		FIELD_GET(CXL_HDM_DECODER_INTERLEAVE_14_12, hdm_cap);
}

static int map_regs(struct cxl_port *port, void __iomem *crb,
		    struct cxl_port_data *cpd)
{
	struct cxl_register_map map;
	struct cxl_component_reg_map *comp_map = &map.component_map;

	cxl_probe_component_regs(&port->dev, crb, comp_map);
	if (!comp_map->hdm_decoder.valid) {
		dev_err(&port->dev, "HDM decoder registers invalid\n");
		return -ENXIO;
	}

	cpd->regs.hdm_decoder = crb + comp_map->hdm_decoder.offset;

	return 0;
}

static u64 get_decoder_size(void __iomem *hdm_decoder, int n)
{
	u32 ctrl = readl(hdm_decoder + CXL_HDM_DECODER0_CTRL_OFFSET(n));

	if (ctrl & CXL_HDM_DECODER0_CTRL_COMMITTED)
		return 0;

	return ioread64_hi_lo(hdm_decoder +
			      CXL_HDM_DECODER0_SIZE_LOW_OFFSET(n));
}

static bool is_endpoint_port(struct cxl_port *port)
{
	/*
	 * It's tempting to just check list_empty(port->dports) here, but this
	 * might get called before dports are setup for a port.
	 */

	if (!port->uport->driver)
		return false;

	return to_cxl_drv(port->uport->driver)->id ==
	       CXL_DEVICE_MEMORY_EXPANDER;
}

static void rescan_ports(struct work_struct *work)
{
	if (bus_rescan_devices(&cxl_bus_type))
		pr_warn("Failed to rescan\n");
}

/* Minor layering violation */
static int dvsec_range_used(struct cxl_port *port)
{
	struct cxl_endpoint_dvsec_info *info;
	int i, ret = 0;

	if (!is_endpoint_port(port))
		return 0;

	info = port->data;
	for (i = 0; i < info->ranges; i++)
		if (info->range[i].size)
			ret |= 1 << i;

	return ret;
}

static int enumerate_hdm_decoders(struct cxl_port *port,
				  struct cxl_port_data *portdata)
{
	void __iomem *hdm_decoder = portdata->regs.hdm_decoder;
	bool global_enable;
	u32 global_ctrl;
	int i = 0;

	global_ctrl = readl(hdm_decoder + CXL_HDM_DECODER_CTRL_OFFSET);
	global_enable = global_ctrl & CXL_HDM_DECODER_ENABLE;
	if (!global_enable) {
		i = dvsec_range_used(port);
		if (i) {
			dev_err(&port->dev,
				"Couldn't add port because device is using DVSEC range registers\n");
			return -EBUSY;
		}
	}

	for (i = 0; i < portdata->caps.count; i++) {
		int rc, target_count = portdata->caps.tc;
		struct cxl_decoder *cxld;
		int *target_map = NULL;
		u64 size;

		if (is_endpoint_port(port))
			target_count = 0;

		cxld = cxl_decoder_alloc(port, target_count);
		if (IS_ERR(cxld)) {
			dev_warn(&port->dev,
				 "Failed to allocate the decoder\n");
			return PTR_ERR(cxld);
		}

		cxld->target_type = CXL_DECODER_EXPANDER;
		cxld->interleave_ways = 1;
		cxld->interleave_granularity = 0;

		size = get_decoder_size(hdm_decoder, i);
		if (size != 0) {
#define decoderN(reg, n) hdm_decoder + CXL_HDM_DECODER0_##reg(n)
			int temp[CXL_DECODER_MAX_INTERLEAVE];
			u64 base;
			u32 ctrl;
			int j;
			union {
				u64 value;
				char target_id[8];
			} target_list;

			target_map = temp;
			ctrl = readl(decoderN(CTRL_OFFSET, i));
			base = ioread64_hi_lo(decoderN(BASE_LOW_OFFSET, i));

			cxld->decoder_range = (struct range){
				.start = base,
				.end = base + size - 1
			};

			cxld->flags = CXL_DECODER_F_ENABLE;
			cxld->interleave_ways = to_interleave_ways(ctrl);
			cxld->interleave_granularity =
				to_interleave_granularity(ctrl);

			if (FIELD_GET(CXL_HDM_DECODER0_CTRL_TYPE, ctrl) == 0)
				cxld->target_type = CXL_DECODER_ACCELERATOR;

			target_list.value = ioread64_hi_lo(decoderN(TL_LOW, i));
			for (j = 0; j < cxld->interleave_ways; j++)
				target_map[j] = target_list.target_id[j];
#undef decoderN
		}

		rc = cxl_decoder_add_locked(cxld, target_map);
		if (rc)
			put_device(&cxld->dev);
		else
			rc = cxl_decoder_autoremove(&port->dev, cxld);
		if (rc)
			dev_err(&port->dev, "Failed to add decoder\n");
		else
			dev_dbg(&cxld->dev, "Added to port %s\n",
				dev_name(&port->dev));
	}

	/*
	 * Turn on global enable now since DVSEC ranges aren't being used and
	 * we'll eventually want the decoder enabled.
	 */
	if (!global_enable) {
		dev_dbg(&port->dev, "Enabling HDM decode\n");
		writel(global_ctrl | CXL_HDM_DECODER_ENABLE, hdm_decoder + CXL_HDM_DECODER_CTRL_OFFSET);
	}

	return 0;
}

/*
 * Per the CXL specification (8.2.5.12 CXL HDM Decoder Capability Structure)
 * single ported host-bridges need not publish a decoder capability when a
 * passthrough decode can be assumed, i.e. all transactions that the uport sees
 * are claimed and passed to the single dport. Disable the range until the first
 * CXL region is enumerated / activated.
 */
static int add_passthrough_decoder(struct cxl_port *port)
{
	int single_port_map[1], rc;
	struct cxl_decoder *cxld;
	struct cxl_dport *dport;

	device_lock_assert(&port->dev);

	cxld = cxl_decoder_alloc(port, 1);
	if (IS_ERR(cxld))
		return PTR_ERR(cxld);

	cxld->interleave_ways = 1;
	cxld->interleave_granularity = PAGE_SIZE;
	cxld->target_type = CXL_DECODER_EXPANDER;
	cxld->platform_res = (struct resource)DEFINE_RES_MEM(0, 0);

	dport = list_first_entry(&port->dports, typeof(*dport), list);
	single_port_map[0] = dport->port_id;

	rc = cxl_decoder_add_locked(cxld, single_port_map);
	if (rc)
		put_device(&cxld->dev);
	else
		rc = cxl_decoder_autoremove(&port->dev, cxld);

	if (rc == 0)
		dev_dbg(&port->dev, "add: %s\n", dev_name(&cxld->dev));

	return rc;
}

static int cxl_port_probe(struct device *dev)
{
	struct cxl_port *port = to_cxl_port(dev);
	struct cxl_port_data *portdata;
	void __iomem *crb;
	int rc = 0;

	if (list_is_singular(&port->dports)) {
		struct device *host_dev = get_cxl_topology_host();

		/*
		 * Root ports (single host bridge downstream) are handled by
		 * platform driver
		 */
		if (port->uport != host_dev)
			rc = add_passthrough_decoder(port);
		put_cxl_topology_host(host_dev);
		return rc;
	}

	if (port->component_reg_phys == CXL_RESOURCE_NONE)
		return 0;

	portdata = devm_kzalloc(dev, sizeof(*portdata), GFP_KERNEL);
	if (!portdata)
		return -ENOMEM;

	crb = devm_cxl_iomap_block(&port->dev, port->component_reg_phys,
				   CXL_COMPONENT_REG_BLOCK_SIZE);
	if (!crb) {
		dev_err(&port->dev, "No component registers mapped\n");
		return -ENXIO;
	}

	rc = map_regs(port, crb, portdata);
	if (rc)
		return rc;

	get_caps(port, portdata);
	if (portdata->caps.count == 0) {
		dev_err(&port->dev, "Spec violation. Caps invalid\n");
		return -ENXIO;
	}

	rc = enumerate_hdm_decoders(port, portdata);
	if (rc) {
		dev_err(&port->dev, "Couldn't enumerate decoders (%d)\n", rc);
		return rc;
	}

	/*
	 * Bus rescan is done in a workqueue so that we can do so with the
	 * device lock dropped.
	 *
	 * Why do we need to rescan? There is a race between cxl_acpi and
	 * cxl_mem (which depends on cxl_pci). cxl_mem will only create a port
	 * if it can establish a path up to a root port, which is enumerated by
	 * a platform specific driver (ie. cxl_acpi) and bound by this driver.
	 * While cxl_acpi could do the rescan, it makes sense to be here as
	 * other platform drivers might require the same functionality.
	 */
	INIT_WORK(&port->rescan_work, rescan_ports);
	queue_work(cxl_port_wq, &port->rescan_work);

	return 0;
}

static struct cxl_driver cxl_port_driver = {
	.name = "cxl_port",
	.probe = cxl_port_probe,
	.id = CXL_DEVICE_PORT,
};

static __init int cxl_port_init(void)
{
	int rc;

	cxl_port_wq = alloc_ordered_workqueue("cxl_port", 0);
	if (!cxl_port_wq)
		return -ENOMEM;

	rc = cxl_driver_register(&cxl_port_driver);
	if (rc) {
		destroy_workqueue(cxl_port_wq);
		return rc;
	}

	return 0;
}

static __exit void cxl_port_exit(void)
{
	destroy_workqueue(cxl_port_wq);
	cxl_driver_unregister(&cxl_port_driver);
}

module_init(cxl_port_init);
module_exit(cxl_port_exit);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_PORT);
