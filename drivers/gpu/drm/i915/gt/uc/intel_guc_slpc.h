/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */
#ifndef _INTEL_GUC_SLPC_H_
#define _INTEL_GUC_SLPC_H_

struct intel_guc_slpc {
};

int intel_guc_slpc_init(struct intel_guc_slpc *slpc);
int intel_guc_slpc_enable(struct intel_guc_slpc *slpc);
void intel_guc_slpc_fini(struct intel_guc_slpc *slpc);

#endif
