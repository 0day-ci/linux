/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _ICE_IDC_INT_H_
#define _ICE_IDC_INT_H_

#include <linux/net/intel/iidc.h>
#include "ice.h"

struct ice_pf;

int ice_cdev_info_update_vsi(struct iidc_core_dev_info *cdev_info, void *data);
int ice_unroll_cdev_info(struct iidc_core_dev_info *cdev_info, void *data);
struct iidc_core_dev_info *
ice_find_cdev_info_by_id(struct ice_pf *pf, int cdev_info_id);

#endif /* !_ICE_IDC_INT_H_ */
