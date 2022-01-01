// SPDX-License-Identifier: GPL-2.0
/*
 * Interconnect framework driver for i.MX8MQ SoC
 *
 * Copyright (c) 2019-2020, NXP
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interconnect-provider.h>
#include <dt-bindings/interconnect/imx8mq.h>

#include "imx.h"

/*
 * Describe bus masters, slaves and connections between them
 *
 * This is a simplified subset of the bus diagram, there are several other
 * PL301 nics which are skipped/merged into PL301_MAIN
 */
static struct imx_icc_node_desc nodes[] = {
	DEFINE_BUS_INTERCONNECT("NOC", IMX8MQ_ICN_NOC, IMX8MQ_ICS_DRAM, IMX8MQ_ICN_MAIN),

	DEFINE_BUS_SLAVE("DRAM", IMX8MQ_ICS_DRAM),
	DEFINE_BUS_SLAVE("OCRAM", IMX8MQ_ICS_OCRAM),
	DEFINE_BUS_MASTER("A53", IMX8MQ_ICM_A53, IMX8MQ_ICN_NOC),

	/* VPUMIX */
	DEFINE_BUS_MASTER("VPU", IMX8MQ_ICM_VPU, IMX8MQ_ICN_VIDEO),
	DEFINE_BUS_INTERCONNECT("PL301_VIDEO", IMX8MQ_ICN_VIDEO, IMX8MQ_ICN_NOC),

	/* GPUMIX */
	DEFINE_BUS_MASTER("GPU", IMX8MQ_ICM_GPU, IMX8MQ_ICN_GPU),
	DEFINE_BUS_INTERCONNECT("PL301_GPU", IMX8MQ_ICN_GPU, IMX8MQ_ICN_NOC),

	/* DISPMIX (only for DCSS) */
	DEFINE_BUS_MASTER("DC", IMX8MQ_ICM_DCSS, IMX8MQ_ICN_DCSS),
	DEFINE_BUS_INTERCONNECT("PL301_DC", IMX8MQ_ICN_DCSS, IMX8MQ_ICN_NOC),

	/* USBMIX */
	DEFINE_BUS_MASTER("USB1", IMX8MQ_ICM_USB1, IMX8MQ_ICN_USB),
	DEFINE_BUS_MASTER("USB2", IMX8MQ_ICM_USB2, IMX8MQ_ICN_USB),
	DEFINE_BUS_INTERCONNECT("PL301_USB", IMX8MQ_ICN_USB, IMX8MQ_ICN_NOC),

	/* PL301_DISPLAY (IPs other than DCSS, inside SUPERMIX) */
	DEFINE_BUS_MASTER("CSI1", IMX8MQ_ICM_CSI1, IMX8MQ_ICN_DISPLAY),
	DEFINE_BUS_MASTER("CSI2", IMX8MQ_ICM_CSI2, IMX8MQ_ICN_DISPLAY),
	DEFINE_BUS_MASTER("LCDIF", IMX8MQ_ICM_LCDIF, IMX8MQ_ICN_DISPLAY),
	DEFINE_BUS_INTERCONNECT("PL301_DISPLAY", IMX8MQ_ICN_DISPLAY, IMX8MQ_ICN_MAIN),

	/* AUDIO */
	DEFINE_BUS_MASTER("SDMA2", IMX8MQ_ICM_SDMA2, IMX8MQ_ICN_AUDIO),
	DEFINE_BUS_INTERCONNECT("PL301_AUDIO", IMX8MQ_ICN_AUDIO, IMX8MQ_ICN_DISPLAY),

	/* ENET */
	DEFINE_BUS_MASTER("ENET", IMX8MQ_ICM_ENET, IMX8MQ_ICN_ENET),
	DEFINE_BUS_INTERCONNECT("PL301_ENET", IMX8MQ_ICN_ENET, IMX8MQ_ICN_MAIN),

	/* OTHER */
	DEFINE_BUS_MASTER("SDMA1", IMX8MQ_ICM_SDMA1, IMX8MQ_ICN_MAIN),
	DEFINE_BUS_MASTER("NAND", IMX8MQ_ICM_NAND, IMX8MQ_ICN_MAIN),
	DEFINE_BUS_MASTER("USDHC1", IMX8MQ_ICM_USDHC1, IMX8MQ_ICN_MAIN),
	DEFINE_BUS_MASTER("USDHC2", IMX8MQ_ICM_USDHC2, IMX8MQ_ICN_MAIN),
	DEFINE_BUS_MASTER("PCIE1", IMX8MQ_ICM_PCIE1, IMX8MQ_ICN_MAIN),
	DEFINE_BUS_MASTER("PCIE2", IMX8MQ_ICM_PCIE2, IMX8MQ_ICN_MAIN),
	DEFINE_BUS_INTERCONNECT("PL301_MAIN", IMX8MQ_ICN_MAIN,
			IMX8MQ_ICN_NOC, IMX8MQ_ICS_OCRAM),
};

static int imx8mq_icc_probe(struct platform_device *pdev)
{
	return imx_icc_register(pdev, nodes, ARRAY_SIZE(nodes));
}

static int imx8mq_icc_remove(struct platform_device *pdev)
{
	return imx_icc_unregister(pdev);
}

static struct platform_driver imx8mq_icc_driver = {
	.probe = imx8mq_icc_probe,
	.remove = imx8mq_icc_remove,
	.driver = {
		.name = "imx8mq-interconnect",
		.sync_state = icc_sync_state,
	},
};

module_platform_driver(imx8mq_icc_driver);
MODULE_ALIAS("platform:imx8mq-interconnect");
MODULE_AUTHOR("Leonard Crestez <leonard.crestez@nxp.com>");
MODULE_LICENSE("GPL v2");
