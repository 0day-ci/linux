// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2020 Linaro Limited. All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include "coresight-cfg-preload.h"
#include "coresight-config.h"
#include "coresight-syscfg.h"

/* Basic features and configurations pre-loaded on initialisation */

static struct cscfg_feature_desc *preload_feats[] = {
	&strobe,
	0
};

static struct cscfg_config_desc *preload_cfgs[] = {
	&afdo,
	0
};

/* preload called on initialisation */
int cscfg_preload(void)
{
	return cscfg_load_config_sets(preload_cfgs, preload_feats);
}
