/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Creating audit records for mapped devices.
 *
 * Copyright (C) 2021 Fraunhofer AISEC. All rights reserved.
 *
 * Authors: Michael Weiß <michael.weiss@aisec.fraunhofer.de>
 */

#ifndef DM_AUDIT_H
#define DM_AUDIT_H

#include <linux/device-mapper.h>

#ifdef CONFIG_DM_AUDIT
void dm_audit_log_bio(const char *dm_msg_prefix, const char *op,
		      struct bio *bio, sector_t sector, int result);
void dm_audit_log_target(const char *dm_msg_prefix, const char *op,
			 struct dm_target *ti, int result);
#else
static inline void dm_audit_log_bio(const char *dm_msg_prefix, const char *op,
				    struct bio *bio, sector_t sector,
				    int result);
{
}
static inline void dm_audit_log_target(const char *dm_msg_prefix,
				       const char *op, struct dm_target *ti,
				       int result);
{
}
#endif

#endif
