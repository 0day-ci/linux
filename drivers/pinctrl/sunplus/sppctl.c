// SPDX-License-Identifier: GPL-2.0
/*
 * SP7021 pinmux controller driver.
 * Copyright (C) Sunplus Tech/Tibbo Tech. 2020
 * Author: Dvorkin Dmitry <dvorkin@tibbo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/io.h>

#include "sppctl.h"
#include "../core.h"


void print_device_tree_node(struct device_node *node, int depth)
{
	int i = 0;
	struct device_node *child;
	struct property    *properties;
	char                indent[255] = "";

	for (i = 0; i < depth * 3; i++)
		indent[i] = ' ';
	indent[i] = '\0';

	++depth;
	if (depth == 1) {
		pr_info("%s{ name = %s\n", indent, node->name);
		for (properties = node->properties; properties != NULL;
			properties = properties->next)
			pr_info("%s  %s (%d)\n", indent, properties->name, properties->length);
		pr_info("%s}\n", indent);
	}

	for_each_child_of_node(node, child) {
		pr_info("%s{ name = %s\n", indent, child->name);
		for (properties = child->properties; properties != NULL;
			properties = properties->next)
			pr_info("%s  %s (%d)\n", indent, properties->name, properties->length);
		print_device_tree_node(child, depth);
		pr_info("%s}\n", indent);
	}
}

void sppctl_gmx_set(struct sppctl_pdata_t *_p, uint8_t _roff, uint8_t _boff, uint8_t _bsiz,
		    uint8_t _rval)
{
	uint32_t *r;
	struct sppctl_reg_t x = { .m = (~(~0 << _bsiz)) << _boff,
				  .v = ((uint16_t)_rval) << _boff };

	if (_p->debug > 1)
		KDBG(_p->pcdp->dev, "%s(x%X,x%X,x%X,x%X) m:x%X v:x%X\n",
		     __func__, _roff, _boff, _bsiz, _rval, x.m, x.v);
	r = (uint32_t *)&x;
	writel(*r, _p->baseI + (_roff << 2));
}

uint8_t sppctl_gmx_get(struct sppctl_pdata_t *_p, uint8_t _roff, uint8_t _boff, uint8_t _bsiz)
{
	uint8_t rval;
	struct sppctl_reg_t *x;
	uint32_t r = readl(_p->baseI + (_roff << 2));

	x = (struct sppctl_reg_t *)&r;
	rval = (x->v >> _boff) & (~(~0 << _bsiz));

	if (_p->debug > 1)
		KDBG(_p->pcdp->dev, "%s(x%X,x%X,x%X) v:x%X rval:x%X\n",
		     __func__, _roff, _boff, _bsiz, x->v, rval);

	return rval;
}

void sppctl_pin_set(struct sppctl_pdata_t *_p, uint8_t _pin, uint8_t _fun)
{
	uint32_t *r;
	struct sppctl_reg_t x = { .m = 0x007F, .v = (uint16_t)_pin };
	uint8_t func = (_fun >> 1) << 2;

	if (_fun % 2 == 0)
		;
	else {
		x.v <<= 8;
		x.m <<= 8;
	}

	if (_p->debug > 1)
		KDBG(_p->pcdp->dev, "%s(x%X,x%X) off:x%X m:x%X v:x%X\n",
		     __func__, _pin, _fun, func, x.m, x.v);

	r = (uint32_t *)&x;
	writel(*r, _p->baseF + func);
}

uint8_t sppctl_fun_get(struct sppctl_pdata_t *_p,  uint8_t _fun)
{
	uint8_t pin = 0x00;
	uint8_t func = (_fun >> 1) << 2;
	struct sppctl_reg_t *x;
	uint32_t r = readl(_p->baseF + func);

	x = (struct sppctl_reg_t *)&r;
	if (_fun % 2 == 0)
		pin = x->v & 0x00FF;
	else
		pin = x->v >> 8;

	if (_p->debug > 1)
		KDBG(_p->pcdp->dev, "%s(x%X) off:x%X m:x%X v:x%X pin:x%X\n",
		     __func__, _fun, func, x->m, x->v, pin);

	return pin;
}

static void sppctl_fwload_cb(const struct firmware *_fw, void *_ctx)
{
	int i = -1, j = 0;
	struct sppctl_pdata_t *p = (struct sppctl_pdata_t *)_ctx;

	if (!_fw) {
		KERR(p->pcdp->dev, "Firmware not found\n");
		return;
	}
	if (_fw->size < list_funcsSZ-2) {
		KERR(p->pcdp->dev, " fw size %zd < %zd\n", _fw->size, list_funcsSZ);
		goto out;
	}

	for (i = 0; i < list_funcsSZ && i < _fw->size; i++) {
		if (list_funcs[i].freg != fOFF_M)
			continue;
		sppctl_pin_set(p, _fw->data[i], i);
		j++;
	}

out:
	release_firmware(_fw);
}

void sppctl_loadfw(struct device *_dev, const char *_fwname)
{
	int ret;
	struct sppctl_pdata_t *p = (struct sppctl_pdata_t *)_dev->platform_data;

	if (!_fwname)
		return;
	if (strlen(_fwname) < 1)
		return;
	KINF(_dev, "fw:%s", _fwname);

	ret = request_firmware_nowait(THIS_MODULE, true, _fwname, _dev, GFP_KERNEL, p,
				      sppctl_fwload_cb);
	if (ret)
		KERR(_dev, "Can't load '%s'\n", _fwname);
}

int sppctl_pctl_resmap(struct platform_device *_pd, struct sppctl_pdata_t *_pc)
{
	struct resource *rp;

	// resF
	rp = platform_get_resource(_pd, IORESOURCE_MEM, 0);
	if (IS_ERR(rp)) {
		KERR(&(_pd->dev), "%s get res#F ERR\n", __func__);
		return PTR_ERR(rp);
	}
	KDBG(&(_pd->dev), "mres #F:%p\n", rp);
	if (!rp)
		return -EFAULT;
	KDBG(&(_pd->dev), "mapping [%pa-%pa]\n", &rp->start, &rp->end);

	_pc->baseF = devm_ioremap_resource(&(_pd->dev), rp);
	if (IS_ERR(_pc->baseF)) {
		KERR(&(_pd->dev), "%s map res#F ERR\n", __func__);
		return PTR_ERR(_pc->baseF);
	}

	// res0
	rp = platform_get_resource(_pd, IORESOURCE_MEM, 1);
	if (IS_ERR(rp)) {
		KERR(&(_pd->dev), "%s get res#0 ERR\n", __func__);
		return PTR_ERR(rp);
	}
	KDBG(&(_pd->dev), "mres #0:%p\n", rp);
	if (!rp)
		return -EFAULT;
	KDBG(&(_pd->dev), "mapping [%pa-%pa]\n", &rp->start, &rp->end);

	_pc->base0 = devm_ioremap_resource(&(_pd->dev), rp);
	if (IS_ERR(_pc->base0)) {
		KERR(&(_pd->dev), "%s map res#0 ERR\n", __func__);
		return PTR_ERR(_pc->base0);
	}

	// res1
	rp = platform_get_resource(_pd, IORESOURCE_MEM, 2);
	if (IS_ERR(rp)) {
		KERR(&(_pd->dev), "%s get res#1 ERR\n", __func__);
		return PTR_ERR(rp);
	}
	KDBG(&(_pd->dev), "mres #1:%p\n", rp);
	if (!rp)
		return -EFAULT;
	KDBG(&(_pd->dev), "mapping [%pa-%pa]\n", &rp->start, &rp->end);

	_pc->base1 = devm_ioremap_resource(&(_pd->dev), rp);
	if (IS_ERR(_pc->base1)) {
		KERR(&(_pd->dev), "%s map res#1 ERR\n", __func__);
		return PTR_ERR(_pc->base1);
	}

	// res2
	rp = platform_get_resource(_pd, IORESOURCE_MEM, 3);
	if (IS_ERR(rp)) {
		KERR(&(_pd->dev), "%s get res#2 ERR\n", __func__);
		return PTR_ERR(rp);
	}
	KDBG(&(_pd->dev), "mres #2:%p\n", rp);
	if (!rp)
		return -EFAULT;
	KDBG(&(_pd->dev), "mapping [%pa-%pa]\n", &rp->start, &rp->end);

	_pc->base2 = devm_ioremap_resource(&(_pd->dev), rp);
	if (IS_ERR(_pc->base2)) {
		KERR(&(_pd->dev), "%s map res#2 ERR\n", __func__);
		return PTR_ERR(_pc->base2);
	}

	// iop
	rp = platform_get_resource(_pd, IORESOURCE_MEM, 4);
	if (IS_ERR(rp)) {
		KERR(&(_pd->dev), "%s get res#I ERR\n", __func__);
		return PTR_ERR(rp);
	}
	KDBG(&(_pd->dev), "mres #I:%p\n", rp);
	if (!rp)
		return -EFAULT;
	KDBG(&(_pd->dev), "mapping [%pa-%pa]\n", &rp->start, &rp->end);

	_pc->baseI = devm_ioremap_resource(&(_pd->dev), rp);
	if (IS_ERR(_pc->baseI)) {
		KERR(&(_pd->dev), "%s map res#I ERR\n", __func__);
		return PTR_ERR(_pc->baseI);
	}

	return 0;
}

static int sppctl_dnew(struct platform_device *_pd)
{
	int ret = -ENODEV;
	struct device_node *np = _pd->dev.of_node;
	struct sppctl_pdata_t *p = NULL;
	const char *fwfname = FW_DEFNAME;

	if (!np) {
		KERR(&(_pd->dev), "Invalid dtb node\n");
		return -EINVAL;
	}
	if (!of_device_is_available(np)) {
		KERR(&(_pd->dev), "dtb is not available\n");
		return -ENODEV;
	}

	// print_device_tree_node(np, 0);

	p = devm_kzalloc(&(_pd->dev), sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	memset(p->name, 0, SPPCTL_MAX_NAM);
	if (np)
		strcpy(p->name, np->name);
	else
		strcpy(p->name, MNAME);
	dev_set_name(&(_pd->dev), "%s", p->name);

	ret = sppctl_pctl_resmap(_pd, p);
	if (ret != 0)
		return ret;

	// set gpio_chip
	_pd->dev.platform_data = p;
	sppctl_sysfs_init(_pd);
	of_property_read_string(np, "fwname", &fwfname);
	if (fwfname)
		strcpy(p->fwname, fwfname);
	sppctl_loadfw(&(_pd->dev), p->fwname);

	ret = sppctl_gpio_new(_pd, p);
	if (ret != 0)
		return ret;

	ret = sppctl_pinctrl_init(_pd);
	if (ret != 0)
		return ret;

	pinctrl_add_gpio_range(p->pcdp, &(p->gpio_range));
	pr_info(M_NAM " by " M_ORG "" M_CPR);

	return 0;
}

static int sppctl_ddel(struct platform_device *_pd)
{
	struct sppctl_pdata_t *p = (struct sppctl_pdata_t *)_pd->dev.platform_data;

	sppctl_gpio_del(_pd, p);
	sppctl_sysfs_clean(_pd);
	sppctl_pinctrl_clea(_pd);
	return 0;
}

static const struct of_device_id sppctl_dt_ids[] = {
	{ .compatible = "sunplus,sp7021-pctl" },
	{ /* zero */ }
};

MODULE_DEVICE_TABLE(of, sppctl_dt_ids);
MODULE_ALIAS("platform:" MNAME);

static struct platform_driver sppctl_driver = {
	.driver = {
		.name           = MNAME,
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(sppctl_dt_ids),
	},
	.probe  = sppctl_dnew,
	.remove = sppctl_ddel,
};

static int __init sppctl_drv_reg(void)
{
	return platform_driver_register(&sppctl_driver);
}
postcore_initcall(sppctl_drv_reg);

static void __exit sppctl_drv_exit(void)
{
	platform_driver_unregister(&sppctl_driver);
}
module_exit(sppctl_drv_exit);

MODULE_AUTHOR(M_AUT1);
MODULE_AUTHOR(M_AUT2);
MODULE_DESCRIPTION(M_NAM);
MODULE_LICENSE(M_LIC);
