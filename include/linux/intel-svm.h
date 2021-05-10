/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2015 Intel Corporation.
 *
 * Authors: David Woodhouse <David.Woodhouse@intel.com>
 */

#ifndef __INTEL_SVM_H__
#define __INTEL_SVM_H__

/* Values for rxwp in fault_cb callback */
#define SVM_REQ_READ	(1<<3)
#define SVM_REQ_WRITE	(1<<2)
#define SVM_REQ_EXEC	(1<<1)
#define SVM_REQ_PRIV	(1<<0)

/*
 * The SVM_FLAG_GUEST_MODE flag is used when a PASID bind is for guest
 * processes. Compared to the host bind, the primary differences are:
 * 1. mm life cycle management
 * 2. fault reporting
 */
#define SVM_FLAG_GUEST_MODE		BIT(1)
/*
 * The SVM_FLAG_GUEST_PASID flag is used when a guest has its own PASID space,
 * which requires guest and host PASID translation at both directions.
 */
#define SVM_FLAG_GUEST_PASID		BIT(2)

#endif /* __INTEL_SVM_H__ */
