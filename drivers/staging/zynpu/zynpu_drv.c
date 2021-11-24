// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_platform_driver.c
 * Implementations of the ZYNPU platform driver probe/remove func
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include "zynpu.h"

extern struct zynpu_priv z1_platform_priv;
extern struct zynpu_priv z2_platform_priv;

static const struct of_device_id zynpu_of_match[] = {
#ifdef CONFIG_ZHOUYI_V1
	{
		.compatible = "armchina,zhouyi-v1",
		.data = (void*)&z1_platform_priv,
	},
#elif CONFIG_ZHOUYI_V2
	{
		.compatible = "armchina,zhouyi-v2",
		.data = (void*)&z2_platform_priv,
	},
#endif
	{ }
};

MODULE_DEVICE_TABLE(of, zynpu_of_match);

struct clk *clk_pll_zynpu;
struct clk *clk_zynpu;
struct clk *clk_zynpu_slv;

/**
 * @brief remove operation registered to platfom_driver struct
 *	This function will be called while the module is unloading.
 * @param p_dev: platform devide struct pointer
 * @return 0 if successful; others if failed.
 */
static int zynpu_remove(struct platform_device *p_dev)
{
	int ret = 0;
	struct device *dev = &p_dev->dev;
	struct zynpu_priv *zynpu = platform_get_drvdata(p_dev);

	if (!IS_ERR_OR_NULL(clk_zynpu_slv))
	{
	    clk_disable_unprepare(clk_zynpu_slv);
	    dev_info(dev, "clk_zynpu_slv disable ok ");
	}

	if (!IS_ERR_OR_NULL(clk_zynpu))
	{
	    clk_disable_unprepare(clk_zynpu);
	    dev_info(dev, "clk_zynpu disable ok");
	}
	clk_zynpu     = NULL;
	clk_zynpu_slv = NULL;
	clk_pll_zynpu = NULL;

	zynpu_priv_disable_interrupt(zynpu);

	ret = deinit_zynpu_priv(zynpu);
	if (ret)
		return ret;

	/* success */
	dev_info(dev, "ZYNPU KMD remove done");
	return 0;
}

/**
 * @brief probe operation registered to platfom_driver struct
 *	This function will be called while the module is loading.
 *
 * @param p_dev: platform devide struct pointer
 * @return 0 if successful; others if failed.
 */
static int zynpu_probe(struct platform_device *p_dev)
{
	int ret = 0;
	struct device *dev = &p_dev->dev;
	struct device_node *dev_node = dev->of_node;
	const struct of_device_id *of_id;
	struct resource *res;
	struct resource res_mem;
	struct zynpu_priv *zynpu;
	struct device_node *np;
	int irqnum = 0;
	int cma_reserve_size = 0;
	u64 base = 0;
	u64 base_size = 0;

	dev_info(dev, "ZYNPU KMD probe start...\n");

	/* match */
	of_id = of_match_node(zynpu_of_match, dev_node);
	if (!of_id) {
		dev_err(dev, "[Probe 0/3] match node failed\n");
		return -EINVAL;
	}
	zynpu = (struct zynpu_priv *)of_id->data;

	if (zynpu->version == ZYNPU_VERSION_ZHOUYI_V1)
		dev_info(dev, "[Probe 0/3] ZYNPU version: zhouyi-v1\n");
	else if (zynpu->version == ZYNPU_VERSION_ZHOUYI_V2)
		dev_info(dev, "[Probe 0/3] ZYNPU version: zhouyi-v2\n");
	else
		dev_err(dev, "[Probe 0/3] Unrecognized ZYNPU version: 0x%x\n", zynpu->version);

	ret = init_zynpu_priv(zynpu, dev);
	if (ret)
		return ret;

	/* get ZYNPU IO */
	res = platform_get_resource(p_dev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "[Probe 1/3] get platform io region failed\n");
		ret = -EINVAL;
		goto probe_fail;
	}
	base = res->start;
	base_size = res->end - res->start + 1;
	dev_dbg(dev, "[Probe 1/3] get ZYNPU IO region: [0x%llx, 0x%llx]\n",
		base, res->end);

	/* get interrupt number */
	res = platform_get_resource(p_dev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "[Probe 1/3] get irqnum failed\n");
		ret = -EINVAL;
		goto probe_fail;
	}
	irqnum = res->start;
	dev_dbg(dev, "[Probe 1/3] get IRQ number: 0x%x\n", irqnum);

	ret = zynpu_priv_init_core(zynpu, irqnum, base, base_size);
	if (ret) {
		goto probe_fail;
	}
	dev_info(dev, "[Probe 1/3] Probe stage 1/3 done: create core\n");

	/* get CMA reserved buffer info */
	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (np) {
		if (of_address_to_resource(np, 0, &res_mem))
			goto probe_fail;
		dev_dbg(dev, "[Probe 2/3] get CMA region: [0x%llx, 0x%llx]\n",
		res_mem.start, res_mem.end);
		ret = zynpu_priv_add_mem_region(zynpu, res_mem.start,
					       res_mem.end - res_mem.start + 1,
					       ZYNPU_MEM_TYPE_CMA);
		if (ret) {
			dev_err(dev, "[Probe 2/3] add new region failed\n");
			goto probe_fail;
		}
		of_node_put(np);

		ret = of_property_read_u32(dev->of_node, "cma-reserved-bytes", &cma_reserve_size);
		if (ret) {
			dev_err(dev, "get cma reserved size property failed!");
			goto probe_fail;
		}

		ret = zynpu_priv_add_mem_region(zynpu, res_mem.start, cma_reserve_size, ZYNPU_MEM_TYPE_CMA);
		if (ret) {
			dev_err(dev, "[Probe 2/3] add new region failed\n");
			goto probe_fail;
		}
		dev_info(dev, "[Probe 2/3] get CMA size 0x%x\n", cma_reserve_size);
	} else {
		dev_info(dev, "[Probe 2/3] No %s specified\n", "memory-region");
	}


	/* get SRAM reserved buffer info, optional */
	np = of_parse_phandle(dev->of_node, "sram-region", 0);
	if (np) {
		if (of_address_to_resource(np, 0, &res_mem))
			goto probe_fail;
		dev_dbg(dev, "[Probe 2/3] get SRAM region: [0x%llx, 0x%llx]\n",
			res_mem.start, res_mem.end);
		ret = zynpu_priv_add_mem_region(zynpu, res_mem.start,
			res_mem.end - res_mem.start + 1, ZYNPU_MEM_TYPE_SRAM);
		if (ret) {
			dev_err(dev, "[Probe 2/3] add new region failed\n");
			goto probe_fail;
		}
		of_node_put(np);
		dev_info(dev, "[Probe 2/3] Stage 2/3 done: add memory region(s)\n");
	} else {
		dev_dbg(dev, "[Probe 2/3] No %s specified\n", "sram-region");
	}

	/* set clock enable */
	clk_zynpu = of_clk_get(dev_node, 0);
	if (IS_ERR_OR_NULL(clk_zynpu)) {
	    dev_err(dev, "clk_zynpu get failed\n");
	    ret = PTR_ERR(clk_zynpu);
	    goto probe_fail;
	}

	clk_pll_zynpu = of_clk_get(dev_node, 1);
	if (IS_ERR_OR_NULL(clk_pll_zynpu)) {
	    dev_err(dev, "clk_pll_zynpu get failed\n");
	    ret = PTR_ERR(clk_pll_zynpu);
	    goto probe_fail;
	}

	clk_zynpu_slv = of_clk_get(dev_node, 2);
	if (IS_ERR_OR_NULL(clk_zynpu_slv)) {
	    dev_err(dev, "clk_zynpu_slv get failed\n");
	    ret = PTR_ERR(clk_zynpu_slv);
	    goto probe_fail;
	}

	if (clk_set_rate(clk_zynpu, 600 * 1000000)) {
	    dev_err(dev, "set clk_zynpu rate fail\n");
	    ret = -EBUSY;
	    goto probe_fail;
	}

	if (clk_prepare_enable(clk_zynpu)) {
	    dev_err(dev, "clk_zynpu enable failed\n");
	    ret = -EBUSY;
	    goto probe_fail;
	}

	if (clk_prepare_enable(clk_pll_zynpu)) {
	    dev_err(dev, "clk_zynpu_slv enable failed\n");
	    ret = -EBUSY;
	    goto probe_fail;
	}
	if (clk_prepare_enable(clk_zynpu_slv)) {
	    dev_err(dev, "clk_zynpu_slv enable failed\n");
	    ret = -EBUSY;
	    goto probe_fail;
	}
	dev_info(dev, "set zynpu clock ok!");

	zynpu_priv_enable_interrupt(zynpu);
	zynpu_priv_print_hw_id_info(zynpu);
	dev_info(dev, "[Probe 3/3] Stage 3/3 done: IO read/write\n");

	/* success */
	platform_set_drvdata(p_dev, zynpu);
	dev_info(dev, "ZYNPU KMD probe done");
	goto finish;

	/* failed */
probe_fail:

	if(!IS_ERR_OR_NULL(clk_zynpu_slv)) {
	    clk_disable_unprepare(clk_zynpu_slv);
	}
	if(!IS_ERR_OR_NULL(clk_zynpu)) {
	    clk_disable_unprepare(clk_zynpu);
	}
	clk_zynpu = NULL;
	clk_zynpu_slv = NULL;
	clk_pll_zynpu = NULL;

	deinit_zynpu_priv(zynpu);

finish:
	return ret;
}

static int zynpu_suspend(struct platform_device *p_dev,pm_message_t state)
{
    struct device *dev = &p_dev->dev;
    struct zynpu_priv *zynpu = platform_get_drvdata(p_dev);

	if (zynpu && zynpu_priv_is_idle(zynpu)) {
		dev_info(dev, "zynpu in idle status !");
	} else {
		dev_err(dev,"zynpu in busy status !");
		return -1;
	}

	if (!IS_ERR_OR_NULL(clk_zynpu_slv)) {
		clk_disable_unprepare(clk_zynpu_slv);
		dev_info(dev, "disable clk_zynpu_slv ok");
	}
	if (!IS_ERR_OR_NULL(clk_zynpu)) {
		clk_disable_unprepare(clk_zynpu);
		dev_info(dev, "disable clk_zynpu ok");
	}
	dev_info(dev, "zynpu_suspend ok");

	return 0;
}

static int zynpu_resume(struct platform_device *p_dev)
{
    struct device *dev = &p_dev->dev;
    struct zynpu_priv *zynpu = platform_get_drvdata(p_dev);

	if(NULL == zynpu) {
		dev_err(dev, "zynpu is null ,resume fail...!");
		return -1;
	}

	if (clk_set_parent(clk_zynpu, clk_pll_zynpu)) {
		dev_err(dev, "set clk_zynpu parent fail\n");
	}
	if (clk_set_rate(clk_zynpu, 600 * 1000000)) {
		dev_err(dev, "set clk_zynpu rate fail\n");
	}
	if (clk_prepare_enable(clk_zynpu_slv)) {
		dev_err(dev, "clk_zynpu_slv enable failed\n");
	}
	if (clk_prepare_enable(clk_zynpu)) {
		dev_err(dev, "clk_zynpu enable failed\n");
	}

    zynpu_priv_enable_interrupt(zynpu);
    zynpu_priv_print_hw_id_info(zynpu);
	dev_info(dev, "zynpu_resume ok.");

	return 0;
}

static struct platform_driver zynpu_platform_driver = {
	.probe = zynpu_probe,
	.remove = zynpu_remove,
	.suspend = zynpu_suspend,
	.resume  = zynpu_resume,
	.driver = {
		.name = "armchina-zynpu",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(zynpu_of_match),
	},
};

module_platform_driver(zynpu_platform_driver);
MODULE_LICENSE("GPL");
