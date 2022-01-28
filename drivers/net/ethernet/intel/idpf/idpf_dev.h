/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Intel Corporation */

#ifndef _IDPF_DEV_H_
#define _IDPF_DEV_H_

#include "iecm.h"

int idpf_intr_reg_init(struct iecm_vport *vport);
void idpf_mb_intr_reg_init(struct iecm_adapter *adapter);
void idpf_reset_reg_init(struct iecm_reset_reg *reset_reg);
void idpf_trigger_reset(struct iecm_adapter *adapter,
			enum iecm_flags trig_cause);
void idpf_vportq_reg_init(struct iecm_vport *vport);
void idpf_ctlq_reg_init(struct iecm_ctlq_create_info *cq);

#endif /* _IDPF_DEV_H_ */
