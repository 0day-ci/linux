// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA AFI bridge.
 * Copyright (c) 2018-2021 Xilinx Inc.
 */

#include <linux/err.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/**
 * struct zynqmp_afi_fpga - AFI register description
 * @value: value to be written to the register
 * @regid: Register id for the register to be written
 */
struct zynqmp_afi_fpga {
	u32 value;
	u32 regid;
};

static int zynqmp_afi_fpga_probe(struct platform_device *pdev)
{
	struct zynqmp_afi_fpga *zynqmp_afi_fpga;
	struct device_node *np = pdev->dev.of_node;
	int i, entries, ret;
	u32 reg, val;

	zynqmp_afi_fpga = devm_kzalloc(&pdev->dev,
				       sizeof(*zynqmp_afi_fpga), GFP_KERNEL);
	if (!zynqmp_afi_fpga)
		return -ENOMEM;
	platform_set_drvdata(pdev, zynqmp_afi_fpga);

	entries = of_property_count_u32_elems(np, "config-afi");
	if (!entries || (entries % 2)) {
		dev_err(&pdev->dev, "Invalid number of registers\n");
		return -EINVAL;
	}

	for (i = 0; i < entries / 2; i++) {
		ret = of_property_read_u32_index(np, "config-afi", i * 2, &reg);
		if (ret) {
			dev_err(&pdev->dev, "failed to read register\n");
			return -EINVAL;
		}
		ret = of_property_read_u32_index(np, "config-afi", i * 2 + 1,
						 &val);
		if (ret) {
			dev_err(&pdev->dev, "failed to read value\n");
			return -EINVAL;
		}
		ret = zynqmp_pm_afi(reg, val);
		if (ret < 0) {
			dev_err(&pdev->dev, "AFI register write error %d\n",
				ret);
			return ret;
		}
	}
	return 0;
}

static const struct of_device_id zynqmp_afi_fpga_ids[] = {
	{ .compatible = "xlnx,zynqmp-afi-fpga" },
	{ },
};
MODULE_DEVICE_TABLE(of, zynqmp_afi_fpga_ids);

static struct platform_driver zynqmp_afi_fpga_driver = {
	.driver = {
		.name = "zynqmp-afi-fpga",
		.of_match_table = zynqmp_afi_fpga_ids,
	},
	.probe = zynqmp_afi_fpga_probe,
};
module_platform_driver(zynqmp_afi_fpga_driver);

MODULE_DESCRIPTION("ZYNQMP FPGA afi module");
MODULE_AUTHOR("Nava kishore Manne <nava.manne@xilinx.com>");
MODULE_LICENSE("GPL v2");
