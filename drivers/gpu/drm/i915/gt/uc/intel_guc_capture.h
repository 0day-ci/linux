/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2021 Intel Corporation
 */

#ifndef _INTEL_GUC_CAPTURE_H
#define _INTEL_GUC_CAPTURE_H

#include <linux/types.h>

struct intel_guc;
struct guc_ads;
struct guc_gt_system_info;

int intel_guc_capture_prep_lists(struct intel_guc *guc, struct guc_ads *blob, u32 blob_ggtt,
				 u32 capture_offset, struct guc_gt_system_info *sysinfo);
void intel_guc_capture_destroy(struct intel_guc *guc);
int intel_guc_capture_init(struct intel_guc *guc);

#endif /* _INTEL_GUC_CAPTURE_H */
