/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2021 Hisilicon Limited. */

#ifndef __HCLGE_DEVLINK_H
#define __HCLGE_DEVLINK_H

#include "hclge_main.h"

enum hclge_devlink_param_id {
	HCLGE_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	HCLGE_DEVLINK_PARAM_ID_RX_BUF_LEN,
	HCLGE_DEVLINK_PARAM_ID_TX_BUF_SIZE,
};

struct hclge_devlink_priv {
	struct hclge_dev *hdev;
};

int hclge_devlink_init(struct hclge_dev *hdev);
void hclge_devlink_set_params_init_values(struct hclge_dev *hdev);
void hclge_devlink_uninit(struct hclge_dev *hdev);
#endif
