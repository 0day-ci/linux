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

#include "sppctl_sysfs.h"
#include "sppctl_gpio_ops.h"
#include "sppctl_pinctrl.h"


static ssize_t sppctl_sop_name_R(struct device *_d, struct device_attribute *_a, char *_b)
{
	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_d->platform_data;

	return sprintf(_b, "%s\n", _p->name);
}

static ssize_t sppctl_sop_dbgi_R(struct device *_d, struct device_attribute *_a, char *_b)
{
	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_d->platform_data;

	return sprintf(_b, "%d\n", _p->debug);
}

static ssize_t sppctl_sop_dbgi_W(struct device *_d, struct device_attribute *_a, const char *_b,
				 size_t _c)
{
	int x;

	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_d->platform_data;

	if (kstrtoint(_b, 10, &x) < 0)
		return -EIO;
	_p->debug = x;

	return _c;
}

static ssize_t sppctl_sop_fwname_R(struct device *_d, struct device_attribute *_a, char *_b)
{
	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_d->platform_data;

	return sprintf(_b, "%s", _p->fwname);
}

static ssize_t sppctl_sop_fwname_W(struct device *_d, struct device_attribute *_a, const char *_b,
				   size_t _c)
{
	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_d->platform_data;

	strcpy(_p->fwname, _b);
	if (_p->fwname[strlen(_p->fwname)-1] == 0x0A)
		_p->fwname[strlen(_p->fwname)-1] = 0;
	sppctl_loadfw(_d, _p->fwname);

	return _c;
}

static ssize_t sppctl_sop_list_muxes_R(struct file *filp, struct kobject *_k,
	struct bin_attribute *_a, char *_b, loff_t off, size_t count)
{
	int i = -1, ret = 0, pos = off;
	const char *tmpp;
	struct sppctl_pdata_t *_p = NULL;
	struct device *_pdev = container_of(_k, struct device, kobj);

	if (!_pdev)
		return -ENXIO;

	_p = (struct sppctl_pdata_t *)_pdev->platform_data;
	if (!_p)
		return -ENXIO;

	for (i = 0; i < list_funcsSZ; i++) {
		if (list_funcs[i].freg == fOFF_0)
			continue;
		if (list_funcs[i].freg == fOFF_I)
			continue;
		tmpp = list_funcs[i].name;
		if (pos > 0) {
			pos -= (strlen(tmpp) + 1);
			continue;
		}
		sprintf(_b + ret, "%s\n", tmpp);
		ret += strlen(tmpp) + 1;
		if (ret > SPPCTL_MAX_BUF - SPPCTL_MAX_NAM)
			break;
	}

	return ret;
}

static ssize_t sppctl_sop_txt_map_R(struct file *filp, struct kobject *_k,
	struct bin_attribute *_a, char *_b, loff_t off, size_t count)
{
	int i = -1, j = 0, ret = 0, pos = off;
	char tmps[SPPCTL_MAX_NAM + 3];
	uint8_t pin = 0;
	struct sppctl_pdata_t *_p = NULL;
	struct func_t *f;
	struct device *_pdev = container_of(_k, struct device, kobj);

	if (!_pdev)
		return -ENXIO;

	_p = (struct sppctl_pdata_t *)_pdev->platform_data;
	if (!_p)
		return -ENXIO;

	for (i = 0; i < list_funcsSZ; i++) {
		f = &(list_funcs[i]);
		pin = 0;
		if (f->freg == fOFF_0)
			continue;
		if (f->freg == fOFF_I)
			continue;
		memset(tmps, 0, SPPCTL_MAX_NAM + 3);

		// muxable pins are P1_xx, stored -7, absolute idx = +7
		pin = sppctl_fun_get(_p, j++);
		if (f->freg == fOFF_M && pin > 0)
			pin += 7;
		if (f->freg == fOFF_G)
			pin = sppctl_gmx_get(_p, f->roff, f->boff, f->blen);
		sprintf(tmps, "%03d %s", pin, f->name);

		if (pos > 0) {
			pos -= (strlen(tmps) + 1);
			continue;
		}
		sprintf(_b + ret, "%s\n", tmps);
		ret += strlen(tmps) + 1;
		if (ret > SPPCTL_MAX_BUF - SPPCTL_MAX_NAM)
			break;
	}

	return ret;
}

static ssize_t sppctl_sop_func_R(struct file *_filp, struct kobject *_k,
	struct bin_attribute *_a, char *_b, loff_t _off, size_t _count)
{
	struct device *_pdev = NULL;
	struct sppctl_sdata_t *sdp = NULL;
	struct sppctl_pdata_t *_p = NULL;
	struct func_t *f;

	if (_off > 0)
		return 0;

	_pdev = container_of(_k, struct device, kobj);
	if (!_pdev)
		return -ENXIO;

	_p = (struct sppctl_pdata_t *)_pdev->platform_data;
	if (!_p)
		return -ENXIO;

	sdp = (struct sppctl_sdata_t *)_a->private;
	if (!sdp)
		return -ENXIO;

	f = &(list_funcs[sdp->i]);
	if (f->freg == fOFF_M)
		_b[0] = sppctl_fun_get(_p, sdp->ridx);
	if (f->freg == fOFF_G)
		_b[0] = sppctl_gmx_get(_p, f->roff, f->boff, f->blen);
	_b[1] = 0x00;
	if (_p->debug)
		KDBG(_pdev, "%s(%s,i:%d) _b:%d\n", __func__, _a->attr.name, sdp->ridx, _b[0]);

	return 1;
}

static ssize_t sppctl_sop_func_W(struct file *_filp, struct kobject *_k,
	struct bin_attribute *_a, char *_b, loff_t _off, size_t _count)
{
	struct device *_pdev = NULL;
	struct sppctl_sdata_t *sdp = NULL;
	struct sppctl_pdata_t *_p = NULL;
	struct func_t *f;

	if (_off > 0)
		return 0;

	_pdev = container_of(_k, struct device, kobj);
	if (!_pdev)
		return -ENXIO;

	_p = (struct sppctl_pdata_t *)_pdev->platform_data;
	if (!_p)
		return -ENXIO;

	sdp = (struct sppctl_sdata_t *)_a->private;
	if (!sdp)
		return -ENXIO;

	f = &(list_funcs[sdp->i]);
	// for mux it should be PIN-7, case muxable pins start from 8'th
	if (f->freg == fOFF_M)
		sppctl_pin_set(_p, (_b[0] < 8 ? 0 : _b[0] - 7), sdp->ridx);
	if (f->freg == fOFF_G)
		sppctl_gmx_set(_p, f->roff, f->boff, f->blen, _b[0]);
	if (_p->debug)
		KDBG(_pdev, "%s(%s,i:%d) _b:%d\n", __func__, _a->attr.name, sdp->ridx, _b[0]);

	return _count;
}

static ssize_t sppctl_sop_fw_R(struct file *filp, struct kobject *_k,
	struct bin_attribute *_a, char *_b, loff_t _off, size_t _count)
{
	int i = 0, j = 0, ret = 0, pos = _off;
	uint8_t pin = 0;
	struct sppctl_pdata_t *_p = NULL;
	struct func_t *f;
	struct device *_pdev = container_of(_k, struct device, kobj);

	if (!_pdev)
		return -ENXIO;

	_p = (struct sppctl_pdata_t *)_pdev->platform_data;
	if (!_p)
		return -ENXIO;

	for (i = 0; i < list_funcsSZ && ret < _count; i++) {
		f = &(list_funcs[i]);
		if (f->freg == fOFF_0)
			continue;
		if (f->freg == fOFF_I)
			continue;
		if (f->freg == fOFF_M)
			pin = sppctl_fun_get(_p, j++);
		if (f->freg == fOFF_G)
			pin = sppctl_gmx_get(_p, f->roff, f->boff, f->blen);
		if (pos > 0) {
			pos -= sizeof(pin);
			continue;
		}
		_b[ret] = pin;
		ret += sizeof(pin);
		if (ret > SPPCTL_MAX_BUF - SPPCTL_MAX_NAM)
			break;
	}

	return ret;
}

static ssize_t sppctl_sop_fw_W(struct file *filp, struct kobject *_k,
	struct bin_attribute *_a, char *_b, loff_t _off, size_t _count)
{
	int i = 0, j = 0, pos = 0;
	struct sppctl_pdata_t *_p = NULL;
	struct func_t *f;
	struct device *_pdev = container_of(_k, struct device, kobj);

	if (_off + _count < (list_funcsSZ - 2))
		KINF(_pdev, "%s() fw size %zd < %zd\n", __func__, _count, list_funcsSZ);

	if (!_pdev)
		return -ENXIO;

	_p = (struct sppctl_pdata_t *)_pdev->platform_data;
	if (!_p)
		return -ENXIO;

	for (; i < list_funcsSZ && pos < _count; i++) {
		f = &(list_funcs[i]);
		if (f->freg == fOFF_0)
			continue;
		if (f->freg == fOFF_I)
			continue;
		if (j < _off) {
			j++;
			continue;
		}

		if (f->freg == fOFF_M)
			sppctl_pin_set(_p, _b[pos], j++);
		if (f->freg == fOFF_G)
			sppctl_gmx_set(_p, f->roff, f->boff, f->blen, _b[pos]);

		pos++;
	}

	return pos;
}

static struct device_attribute sppctl_sysfs_attrsD[] = {
	__ATTR(name,   0444, sppctl_sop_name_R,   NULL),
	__ATTR(dbgi,   0644, sppctl_sop_dbgi_R,   sppctl_sop_dbgi_W),
	__ATTR(fwname, 0644, sppctl_sop_fwname_R, sppctl_sop_fwname_W),
};

static struct bin_attribute sppctl_sysfs_attrsB[] = {
	__BIN_ATTR(list_muxes, 0444, sppctl_sop_list_muxes_R, NULL,            SPPCTL_MAX_BUF),
	__BIN_ATTR(txt_map,    0444, sppctl_sop_txt_map_R,    NULL,            SPPCTL_MAX_BUF),
	__BIN_ATTR(fw,         0644, sppctl_sop_fw_R,         sppctl_sop_fw_W, SPPCTL_MAX_BUF),
};

struct bin_attribute *sppctl_sysfs_Fap;

// ---------- main (exported) functions
void sppctl_sysfs_init(struct platform_device *_pd)
{
	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_pd->dev.platform_data;
	struct sppctl_sdata_t *sdp = NULL;
	int i, ret, ridx = 0;
	const char *tmpp;

	for (i = 0; i < ARRAY_SIZE(sppctl_sysfs_attrsD); i++) {
		ret = device_create_file(&(_pd->dev), &sppctl_sysfs_attrsD[i]);
		if (ret)
			KERR(&(_pd->dev), "createD[%d] error\n", i);
	}

	for (i = 0; i < ARRAY_SIZE(sppctl_sysfs_attrsB); i++) {
		ret = device_create_bin_file(&(_pd->dev), &sppctl_sysfs_attrsB[i]);
		if (ret)
			KERR(&(_pd->dev), "createB[%d] error\n", i);
	}

	i = -1;
	sppctl_sysfs_Fap = kcalloc(list_funcsSZ, sizeof(struct bin_attribute), GFP_KERNEL);
	sdp = kcalloc(list_funcsSZ, sizeof(struct sppctl_sdata_t), GFP_KERNEL);
	for (i = 0; i < list_funcsSZ; i++) {
		if (list_funcs[i].freg == fOFF_0)
			continue;
		if (list_funcs[i].freg == fOFF_I)
			continue;

		tmpp = list_funcs[i].name;
		sdp[i].i = i;
		sdp[i].ridx = ridx++;
		sdp[i].pdata = _p;

		sysfs_bin_attr_init(sppctl_sysfs_Fap[i]);
		sppctl_sysfs_Fap[i].attr.name = tmpp;
		sppctl_sysfs_Fap[i].attr.mode = 0644;
		sppctl_sysfs_Fap[i].read  = sppctl_sop_func_R;
		sppctl_sysfs_Fap[i].write = sppctl_sop_func_W;
		sppctl_sysfs_Fap[i].size = SPPCTL_MAX_BUF;
		sppctl_sysfs_Fap[i].private = &(sdp[i]);
		ret = device_create_bin_file(&(_pd->dev), &(sppctl_sysfs_Fap[i]));

		if (ret)
			KERR(&(_pd->dev), "createF[%d,%s] error\n", i, tmpp);
	}
	_p->sysfs_sdp = sdp;
}

void sppctl_sysfs_clean(struct platform_device *_pd)
{
	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_pd->dev.platform_data;
	int i;

	for (i = 0; i < ARRAY_SIZE(sppctl_sysfs_attrsD); i++)
		device_remove_file(&(_pd->dev), &sppctl_sysfs_attrsD[i]);
	for (i = 0; i < ARRAY_SIZE(sppctl_sysfs_attrsB); i++)
		device_remove_bin_file(&(_pd->dev), &sppctl_sysfs_attrsB[i]);

	i = -1;
	for (i = 0; i < list_funcsSZ; i++) {
		if (list_funcs[i].freg == fOFF_0)
			continue;
		if (list_funcs[i].freg == fOFF_I)
			continue;
		device_remove_bin_file(&(_pd->dev), &(sppctl_sysfs_Fap[i]));
	}

	kfree(sppctl_sysfs_Fap);
	kfree(_p->sysfs_sdp);
}
