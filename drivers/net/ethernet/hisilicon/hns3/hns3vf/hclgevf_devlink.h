/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2021 Hisilicon Limited. */

#ifndef __HCLGEVF_DEVLINK_H
#define __HCLGEVF_DEVLINK_H

#include "hclgevf_main.h"

enum hclgevf_devlink_param_id {
	HCLGEVF_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	HCLGEVF_DEVLINK_PARAM_ID_RX_BUF_LEN,
	HCLGEVF_DEVLINK_PARAM_ID_TX_BUF_SIZE,
};

struct hclgevf_devlink_priv {
	struct hclgevf_dev *hdev;
};

void hclgevf_devlink_set_params_init_values(struct hclgevf_dev *hdev);
int hclgevf_devlink_init(struct hclgevf_dev *hdev);
void hclgevf_devlink_uninit(struct hclgevf_dev *hdev);
#endif
