/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DEVLINK_H_
#define _ICE_DEVLINK_H_

enum ice_devlink_param_id {
	ICE_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
};

struct ice_pf *ice_allocate_pf(struct device *dev);

int ice_devlink_register(struct ice_pf *pf);
void ice_devlink_unregister(struct ice_pf *pf);
void ice_devlink_params_publish(struct ice_pf *pf);
int ice_devlink_create_port(struct ice_vsi *vsi);
void ice_devlink_destroy_port(struct ice_vsi *vsi);

void ice_devlink_init_regions(struct ice_pf *pf);
void ice_devlink_destroy_regions(struct ice_pf *pf);

#endif /* _ICE_DEVLINK_H_ */
