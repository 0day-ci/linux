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

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../devicetree.h"
#include "sppctl_pinctrl.h"
#include "sppctl_gpio_ops.h"

#ifdef CONFIG_PINCTRL_SPPCTL
#define SUPPORT_PINMUX
#endif

char const **unq_grps;
size_t unq_grpsSZ;
struct grp2fp_map_t *g2fp_maps;

int stpctl_c_p_get(struct pinctrl_dev *_pd, unsigned int _pin, unsigned long *_cfg)
{
	struct sppctl_pdata_t *pctrl = pinctrl_dev_get_drvdata(_pd);
	unsigned int param = pinconf_to_config_param(*_cfg);
	unsigned int arg = 0;

	KDBG(_pd->dev, "%s(%d)\n", __func__, _pin);
	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!sppctlgpio_u_isodr(&(pctrl->gpiod->chip), _pin))
			return -EINVAL;
		break;

	case PIN_CONFIG_OUTPUT:
		if (!sppctlgpio_u_gfrst(&(pctrl->gpiod->chip), _pin))
			return -EINVAL;
		if (!sppctlgpio_u_magpi(&(pctrl->gpiod->chip), _pin))
			return -EINVAL;
		if (sppctlgpio_f_gdi(&(pctrl->gpiod->chip), _pin) != 0)
			return -EINVAL;
		arg = sppctlgpio_f_get(&(pctrl->gpiod->chip), _pin);
		break;

	default:
		//KINF(_pd->dev, "%s(%d) skipping:x%X\n", __FUNCTION__, _pin, param);
		return -EOPNOTSUPP;
	}
	*_cfg = pinconf_to_config_packed(param, arg);

	return 0;
}

int stpctl_c_p_set(struct pinctrl_dev *_pd, unsigned int _pin, unsigned long *_ca,
		   unsigned int _clen)
{
	struct sppctl_pdata_t *pctrl = pinctrl_dev_get_drvdata(_pd);
	int i = 0;

	KDBG(_pd->dev, "%s(%d,%ld,%d)\n", __func__, _pin, *_ca, _clen);
	// special handling for IOP
	if (_ca[i] == 0xFF) {
		sppctlgpio_u_magpi_set(&(pctrl->gpiod->chip), _pin, muxF_G, muxM_I);
		return 0;
	}

	for (i = 0; i < _clen; i++) {
		if (_ca[i] & SPPCTL_PCTL_L_OUT) {
			KDBG(_pd->dev, "%d:OUT\n", i);
			sppctlgpio_f_sou(&(pctrl->gpiod->chip), _pin, 0);
		}
		if (_ca[i] & SPPCTL_PCTL_L_OU1) {
			KDBG(_pd->dev, "%d:OU1\n", i);
			sppctlgpio_f_sou(&(pctrl->gpiod->chip), _pin, 1);
		}
		if (_ca[i] & SPPCTL_PCTL_L_INV) {
			KDBG(_pd->dev, "%d:INV\n", i);
			sppctlgpio_u_siinv(&(pctrl->gpiod->chip), _pin);
		}
		if (_ca[i] & SPPCTL_PCTL_L_ONV) {
			KDBG(_pd->dev, "%d:ONV\n", i);
			sppctlgpio_u_soinv(&(pctrl->gpiod->chip), _pin);
		}
		if (_ca[i] & SPPCTL_PCTL_L_ODR) {
			KDBG(_pd->dev, "%d:ODR\n", i);
			sppctlgpio_u_seodr(&(pctrl->gpiod->chip), _pin, 1);
		}
		// FIXME: add pullup/pulldown, irq enable/disable
	}

	return 0;
}

int stpctl_c_g_get(struct pinctrl_dev *_pd, unsigned int _gid, unsigned long *_config)
{
	// KINF(_pd->dev, "%s(%d)\n", __FUNCTION__, _gid);
	// FIXME: add data
	return 0;
}

int stpctl_c_g_set(struct pinctrl_dev *_pd, unsigned int _gid, unsigned long *_configs,
		   unsigned int _num_configs)
{
	// KINF(_pd->dev, "%s(%d,,%d)\n", __FUNCTION__, _gid, _num_configs);
	// FIXME: delete ?
	return 0;
}

#ifdef CONFIG_DEBUG_FS
void stpctl_c_d_show(struct pinctrl_dev *_pd, struct seq_file *s, unsigned int _off)
{
	// KINF(_pd->dev, "%s(%d)\n", __FUNCTION__, _off);
	seq_printf(s, " %s", dev_name(_pd->dev));
}

void stpctl_c_d_group_show(struct pinctrl_dev *_pd, struct seq_file *s, unsigned int _gid)
{
	// group: freescale/pinctrl-imx.c, 448
	// KINF(_pd->dev, "%s(%d)\n", __FUNCTION__, _gid);
}

void stpctl_c_d_config_show(struct pinctrl_dev *_pd, struct seq_file *s, unsigned long _config)
{
	// KINF(_pd->dev, "%s(%ld)\n", __FUNCTION__, _config);
}
#else
#define stpctl_c_d_show NULL
#define stpctl_c_d_group_show NULL
#define stpctl_c_d_config_show NULL
#endif

static struct pinconf_ops sppctl_pconf_ops = {
	.is_generic                 = true,
	.pin_config_get             = stpctl_c_p_get,
	.pin_config_set             = stpctl_c_p_set,
	//.pin_config_group_get       = stpctl_c_g_get,
	//.pin_config_group_set       = stpctl_c_g_set,
	.pin_config_dbg_show        = stpctl_c_d_show,
	.pin_config_group_dbg_show  = stpctl_c_d_group_show,
	.pin_config_config_dbg_show = stpctl_c_d_config_show,
};

int stpctl_m_req(struct pinctrl_dev *_pd, unsigned int _pin)
{
	KDBG(_pd->dev, "%s(%d)\n", __func__, _pin);
	return 0;
}

int stpctl_m_fre(struct pinctrl_dev *_pd, unsigned int _pin)
{
	KDBG(_pd->dev, "%s(%d)\n", __func__, _pin);
	return 0;
}

int stpctl_m_f_cnt(struct pinctrl_dev *_pd)
{
	return list_funcsSZ;
}

const char *stpctl_m_f_nam(struct pinctrl_dev *_pd, unsigned int _fid)
{
	return list_funcs[_fid].name;
}

int stpctl_m_f_grp(struct pinctrl_dev *_pd, unsigned int _fid, const char * const **grps,
		   unsigned int *_gnum)
{
	struct func_t *f = &(list_funcs[_fid]);

	*_gnum = 0;
	switch (f->freg) {
	case fOFF_I:
	case fOFF_0:   // gen GPIO/IOP: all groups = all pins
		*_gnum = GPIS_listSZ;
		*grps = sppctlgpio_list_s;
		break;

	case fOFF_M:   // pin-mux
		*_gnum = PMUX_listSZ;
		*grps = sppctlpmux_list_s;
		break;

	case fOFF_G:   // pin-group
		if (!f->grps)
			break;
		*_gnum = f->gnum;
		*grps = (const char * const *)f->grps_sa;
		break;

	default:
		KERR(_pd->dev, "%s(_fid:%d) unknown fOFF %d\n", __func__, _fid, f->freg);
		break;
	}

	KDBG(_pd->dev, "%s(_fid:%d) %d\n", __func__, _fid, *_gnum);
	return 0;
}

int stpctl_m_mux(struct pinctrl_dev *_pd, unsigned int _fid, unsigned int _gid)
{
	int i = -1, j = -1;
	struct sppctl_pdata_t *pctrl = pinctrl_dev_get_drvdata(_pd);
	struct func_t *f = &(list_funcs[_fid]);

	struct grp2fp_map_t g2fpm = g2fp_maps[_gid];

	KDBG(_pd->dev, "%s(fun:%d,grp:%d)\n", __func__, _fid, _gid);
	switch (f->freg) {
	case fOFF_0:   // GPIO. detouch from all funcs - ?
		for (i = 0; i < list_funcsSZ; i++) {
			if (list_funcs[i].freg != fOFF_M)
				continue;
			j++;
			if (sppctl_fun_get(pctrl, j) != _gid)
				continue;
			sppctl_pin_set(pctrl, 0, j);
		}
		break;

	case fOFF_M:   // MUX :
		sppctlgpio_u_magpi_set(&(pctrl->gpiod->chip), _gid, muxF_M, muxMKEEP);
		sppctl_pin_set(pctrl, (_gid == 0 ? _gid : _gid - 7), _fid - 2);    // pin, fun FIXME
		break;

	case fOFF_G:   // GROUP
		for (i = 0; i < f->grps[g2fpm.g_idx].pnum; i++)
			sppctlgpio_u_magpi_set(&(pctrl->gpiod->chip), f->grps[g2fpm.g_idx].pins[i],
					       muxF_M, muxMKEEP);
		sppctl_gmx_set(pctrl, f->roff, f->boff, f->blen, f->grps[g2fpm.g_idx].gval);
		break;

	case fOFF_I:   // IOP
		sppctlgpio_u_magpi_set(&(pctrl->gpiod->chip), _gid, muxF_G, muxM_I);
		break;

	default:
		KERR(_pd->dev, "%s(_fid:%d) unknown fOFF %d\n", __func__, _fid, f->freg);
		break;
	}

	return 0;
}

int stpctl_m_gpio_req(struct pinctrl_dev *_pd, struct pinctrl_gpio_range *range, unsigned int _pin)
{
	struct sppctl_pdata_t *pctrl = pinctrl_dev_get_drvdata(_pd);
	struct pin_desc *pdesc;
	int g_f, g_m;

	KDBG(_pd->dev, "%s(%d)\n", __func__, _pin);
	g_f = sppctlgpio_u_gfrst(&(pctrl->gpiod->chip), _pin);
	g_m = sppctlgpio_u_magpi(&(pctrl->gpiod->chip), _pin);
	if (g_f == muxF_G && g_m == muxM_G)
		return 0;

	pdesc = pin_desc_get(_pd, _pin);
	// in non-gpio state: is it claimed already?
	if (pdesc->mux_owner)
		return -EACCES;

	sppctlgpio_u_magpi_set(&(pctrl->gpiod->chip), _pin, muxF_G, muxM_G);
	return 0;
}

void stpctl_m_gpio_fre(struct pinctrl_dev *_pd, struct pinctrl_gpio_range *range,
		       unsigned int _pin)
{
	KDBG(_pd->dev, "%s(%d)\n", __func__, _pin);
}
int stpctl_m_gpio_sdir(struct pinctrl_dev *_pd, struct pinctrl_gpio_range *range,
		       unsigned int _pin, bool _in)
{
	KDBG(_pd->dev, "%s(%d,%d)\n", __func__, _pin, _in);
	return 0;
}

static const struct pinmux_ops sppctl_pinmux_ops = {
	.request             = stpctl_m_req,
	.free                = stpctl_m_fre,
	.get_functions_count = stpctl_m_f_cnt,
	.get_function_name   = stpctl_m_f_nam,
	.get_function_groups = stpctl_m_f_grp,
	.set_mux             = stpctl_m_mux,
	.gpio_request_enable = stpctl_m_gpio_req,
	.gpio_disable_free   = stpctl_m_gpio_fre,
	.gpio_set_direction  = stpctl_m_gpio_sdir,
	.strict              = 1
};

// all groups
int stpctl_o_g_cnt(struct pinctrl_dev *_pd)
{
	return unq_grpsSZ;
}

const char *stpctl_o_g_nam(struct pinctrl_dev *_pd, unsigned int _gid)
{
	return unq_grps[_gid];
}

int stpctl_o_g_pins(struct pinctrl_dev *_pd, unsigned int _gid, const unsigned int **pins,
		    unsigned int *num_pins)
{
	struct grp2fp_map_t g2fpm = g2fp_maps[_gid];
	struct func_t *f = &(list_funcs[g2fpm.f_idx]);

	KDBG(_pd->dev, "grp-pins g:%d f_idx:%d,g_idx:%d freg:%d...\n", _gid, g2fpm.f_idx,
	     g2fpm.g_idx, f->freg);
	*num_pins = 0;

	// MUX | GPIO | IOP: 1 pin -> 1 group
	if (f->freg != fOFF_G) {
		*num_pins = 1;
		*pins = &sppctlpins_G[_gid];
		return 0;
	}

	// IOP (several pins at once in a group)
	if (!f->grps)
		return 0;
	if (f->gnum < 1)
		return 0;
	*num_pins = f->grps[g2fpm.g_idx].pnum;
	*pins = f->grps[g2fpm.g_idx].pins;

	return 0;
}

// /sys/kernel/debug/pinctrl/sppctl/pins add: gpio_first and ctrl_sel
#ifdef CONFIG_DEBUG_FS
void stpctl_o_show(struct pinctrl_dev *_pd, struct seq_file *_s, unsigned int _n)
{
	struct sppctl_pdata_t *p = pinctrl_dev_get_drvdata(_pd);
	const char *tmpp;
	uint8_t g_f, g_m;

	seq_printf(_s, "%s", dev_name(_pd->dev));
	g_f = sppctlgpio_u_gfrst(&(p->gpiod->chip), _n);
	g_m = sppctlgpio_u_magpi(&(p->gpiod->chip), _n);

	tmpp = "?";
	if (g_f &&  g_m)
		tmpp = "GPIO";
	if (g_f && !g_m)
		tmpp = " IOP";
	if (!g_f)
		tmpp = " MUX";
	seq_printf(_s, " %s", tmpp);
}
#else
#define stpctl_ops_show NULL
#endif

int stpctl_o_n2map(struct pinctrl_dev *_pd, struct device_node *_dn, struct pinctrl_map **_map,
		   unsigned int *_nm)
{
	struct sppctl_pdata_t *pctrl = pinctrl_dev_get_drvdata(_pd);
	struct device_node *parent;
	u32 dt_pin, dt_fun;
	u8 p_p, p_g, p_f, p_l;
	unsigned long *configs;
	int i, size = 0;
	const __be32 *list = of_get_property(_dn, "pins", &size);
	struct property *prop;
	const char *s_f, *s_g;
	int nmG = of_property_count_strings(_dn, "groups");
	struct func_t *f = NULL;

	//print_device_tree_node(_dn, 0);
	if (nmG <= 0)
		nmG = 0;

	parent = of_get_parent(_dn);
	*_nm = size/sizeof(*list);

	// Check if out of range or invalid?
	for (i = 0; i < (*_nm); i++) {
		dt_pin = be32_to_cpu(list[i]);
		p_p = SPPCTL_PCTLD_P(dt_pin);
		p_g = SPPCTL_PCTLD_G(dt_pin);

		if ((p_p >= sppctlpins_allSZ)
#ifndef SUPPORT_PINMUX
			|| (p_g == SPPCTL_PCTL_G_PMUX)
#endif
		) {
			KDBG(_pd->dev, "Invalid pin property at index %d (0x%08x)\n", i, dt_pin);
			return -EINVAL;
		}
	}

	*_map = kcalloc(*_nm + nmG, sizeof(**_map), GFP_KERNEL);
	for (i = 0; i < (*_nm); i++) {
		dt_pin = be32_to_cpu(list[i]);
		p_p = SPPCTL_PCTLD_P(dt_pin);
		p_g = SPPCTL_PCTLD_G(dt_pin);
		p_f = SPPCTL_PCTLD_F(dt_pin);
		p_l = SPPCTL_PCTLD_L(dt_pin);
		(*_map)[i].name = parent->name;
		KDBG(_pd->dev, "map [%d]=%08x p=%d g=%d f=%d l=%d\n", i, dt_pin, p_p, p_g,
		     p_f, p_l);

		if (p_g == SPPCTL_PCTL_G_GPIO) {
			// look into parse_dt_cfg(),
			(*_map)[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
			(*_map)[i].data.configs.num_configs = 1;
			(*_map)[i].data.configs.group_or_pin = pin_get_name(_pd, p_p);
			configs = kcalloc(1, sizeof(*configs), GFP_KERNEL);
			*configs = p_l;
			(*_map)[i].data.configs.configs = configs;

			KDBG(_pd->dev, "%s(%d) = x%X\n", (*_map)[i].data.configs.group_or_pin,
			     p_p, p_l);
		} else if (p_g == SPPCTL_PCTL_G_IOPP) {
			(*_map)[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
			(*_map)[i].data.configs.num_configs = 1;
			(*_map)[i].data.configs.group_or_pin = pin_get_name(_pd, p_p);
			configs = kcalloc(1, sizeof(*configs), GFP_KERNEL);
			*configs = 0xFF;
			(*_map)[i].data.configs.configs = configs;

			KDBG(_pd->dev, "%s(%d) = x%X\n", (*_map)[i].data.configs.group_or_pin,
			     p_p, p_l);
		} else {
			(*_map)[i].type = PIN_MAP_TYPE_MUX_GROUP;
			(*_map)[i].data.mux.function = list_funcs[p_f].name;
			(*_map)[i].data.mux.group = pin_get_name(_pd, p_p);

			KDBG(_pd->dev, "f->p: %s(%d)->%s(%d)\n", (*_map)[i].data.mux.function,
			     p_f, (*_map)[i].data.mux.group, p_p);
		}
	}

	// handle pin-group function
	if (nmG > 0 && of_property_read_string(_dn, "function", &s_f) == 0) {
		KDBG(_pd->dev, "found func: %s\n", s_f);
		of_property_for_each_string(_dn, "groups", prop, s_g) {
			KDBG(_pd->dev, " %s: %s\n", s_f, s_g);
			(*_map)[*_nm].type = PIN_MAP_TYPE_MUX_GROUP;
			(*_map)[*_nm].data.mux.function = s_f;
			(*_map)[*_nm].data.mux.group = s_g;
			KDBG(_pd->dev, "f->g: %s->%s\n", (*_map)[*_nm].data.mux.function,
			     (*_map)[*_nm].data.mux.group);
			(*_nm)++;
		}
	}

	// handle zero function
	list = of_get_property(_dn, "zero_func", &size);
	if (list) {
		for (i = 0; i < size/sizeof(*list); i++) {
			dt_fun = be32_to_cpu(list[i]);
			if (dt_fun >= list_funcsSZ) {
				KERR(_pd->dev, "zero func %d out of range\n", dt_fun);
				continue;
			}

			f = &(list_funcs[dt_fun]);
			switch (f->freg) {
			case fOFF_M:
				KDBG(_pd->dev, "zero func: %d (%s)\n", dt_fun, f->name);
				sppctl_pin_set(pctrl, 0, dt_fun - 2);
				break;

			case fOFF_G:
				KDBG(_pd->dev, "zero group: %d (%s)\n", dt_fun, f->name);
				sppctl_gmx_set(pctrl, f->roff, f->boff, f->blen, 0);
				break;

			default:
				KERR(_pd->dev, "wrong zero group: %d (%s)\n", dt_fun, f->name);
				break;
			}
		}
	}

	of_node_put(parent);
	KDBG(_pd->dev, "%d pins mapped\n", *_nm);
	return 0;
}

void stpctl_o_mfre(struct pinctrl_dev *_pd, struct pinctrl_map *_map, unsigned int num_maps)
{
	//KINF(_pd->dev, "%s(%d)\n", __FUNCTION__, num_maps);
	// FIXME: test
	pinctrl_utils_free_map(_pd, _map, num_maps);
}

static const struct pinctrl_ops sppctl_pctl_ops = {
	.get_groups_count = stpctl_o_g_cnt,
	.get_group_name   = stpctl_o_g_nam,
	.get_group_pins   = stpctl_o_g_pins,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show     = stpctl_o_show,
#endif
	.dt_node_to_map   = stpctl_o_n2map,
	.dt_free_map      = stpctl_o_mfre,
};

// creates unq_grps[] uniq group names array char *
// sets unq_grpsSZ
// creates XXX[group_idx]{func_idx, pins_idx}
void group_groups(struct platform_device *_pd)
{
	int i, k, j = 0;

	// fill array of all groups
	unq_grps = NULL;
	unq_grpsSZ = GPIS_listSZ;

	// calc unique group names array size
	for (i = 0; i < list_funcsSZ; i++) {
		if (list_funcs[i].freg != fOFF_G)
			continue;
		unq_grpsSZ += list_funcs[i].gnum;
	}

	// fill up unique group names array
	unq_grps = devm_kzalloc(&(_pd->dev), (unq_grpsSZ + 1)*sizeof(char *), GFP_KERNEL);
	g2fp_maps = devm_kzalloc(&(_pd->dev), (unq_grpsSZ + 1)*sizeof(struct grp2fp_map_t),
				 GFP_KERNEL);

	// groups == pins
	j = 0;
	for (i = 0; i < GPIS_listSZ; i++) {
		unq_grps[i] = sppctlgpio_list_s[i];
		g2fp_maps[i].f_idx = 0;
		g2fp_maps[i].g_idx = i;
	}
	j = GPIS_listSZ;

	// +IOP groups
	for (i = 0; i < list_funcsSZ; i++) {
		if (list_funcs[i].freg != fOFF_G)
			continue;

		for (k = 0; k < list_funcs[i].gnum; k++) {
			list_funcs[i].grps_sa[k] = (char *)list_funcs[i].grps[k].name;
			unq_grps[j] = list_funcs[i].grps[k].name;
			g2fp_maps[j].f_idx = i;
			g2fp_maps[j].g_idx = k;
			j++;
		}
	}
	KINF(&(_pd->dev), "funcs: %zd unq_grps: %zd\n", list_funcsSZ, unq_grpsSZ);
}

// ---------- main (exported) functions
int sppctl_pinctrl_init(struct platform_device *_pd)
{
	int err;
	struct device *dev = &_pd->dev;
	struct device_node *np = of_node_get(dev->of_node);
	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_pd->dev.platform_data;

	// init pdesc
	_p->pdesc.owner = THIS_MODULE;
	_p->pdesc.name = dev_name(&(_pd->dev));
	_p->pdesc.pins = &(sppctlpins_all[0]);
	_p->pdesc.npins = sppctlpins_allSZ;
	_p->pdesc.pctlops = &sppctl_pctl_ops;
	_p->pdesc.confops = &sppctl_pconf_ops;
	_p->pdesc.pmxops = &sppctl_pinmux_ops;

	group_groups(_pd);

	err = devm_pinctrl_register_and_init(&(_pd->dev), &(_p->pdesc), _p, &(_p->pcdp));
	if (err) {
		KERR(&(_pd->dev), "Failed to register\n");
		of_node_put(np);
		return err;
	}

	pinctrl_enable(_p->pcdp);
	return 0;
}

void sppctl_pinctrl_clea(struct platform_device *_pd)
{
	struct sppctl_pdata_t *_p = (struct sppctl_pdata_t *)_pd->dev.platform_data;

	devm_pinctrl_unregister(&(_pd->dev), _p->pcdp);
}
