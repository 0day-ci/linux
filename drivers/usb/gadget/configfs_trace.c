// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/usb/composite.h>
#include "configfs.h"

#define MAX_CONFIGURAITON_STR_LEN	512

static char *config_to_string(struct gadget_info *gi)
{
	struct usb_configuration *uc;
	struct config_usb_cfg *cfg;
	struct config_usb_function *cf;
	static char cfg_str[MAX_CONFIGURAITON_STR_LEN];
	size_t len = MAX_CONFIGURAITON_STR_LEN;
	int n = 0;

	cfg_str[0] = '\0';

	list_for_each_entry(uc, &gi->cdev.configs, list) {
		cfg = container_of(uc, struct config_usb_cfg, c);

		n += scnprintf(cfg_str + n, len - n,
			"group:%s,bConfigurationValue:%d,bmAttributes:%d,"
			"MaxPower:%d,",
			config_item_name(&cfg->group.cg_item),
			uc->bConfigurationValue,
			uc->bmAttributes,
			uc->MaxPower);

		n += scnprintf(cfg_str + n, len - n, "function:[");
		list_for_each_entry(cf, &cfg->func_list, list)
			n += scnprintf(cfg_str + n, len - n, "%s", cf->f->name);
		n += scnprintf(cfg_str + n, len - n, "},");
	}

	return cfg_str;
}

#define CREATE_TRACE_POINTS
#include "configfs_trace.h"
