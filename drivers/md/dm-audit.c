// SPDX-License-Identifier: GPL-2.0
/*
 * Creating audit records for mapped devices.
 *
 * Copyright (C) 2021 Fraunhofer AISEC. All rights reserved.
 *
 * Authors: Michael Weiß <michael.weiss@aisec.fraunhofer.de>
 */

#include <linux/audit.h>
#include <linux/module.h>
#include <linux/device-mapper.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#include "dm-audit.h"
#include "dm-core.h"

void dm_audit_log_bio(const char *dm_msg_prefix, const char *op,
		      struct bio *bio, sector_t sector, int result)
{
	struct audit_buffer *ab;

	if (audit_enabled == AUDIT_OFF)
		return;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_DM);
	if (unlikely(!ab))
		return;

	audit_log_format(ab, "module=%s dev=%d:%d op=%s sector=%llu res=%d",
			 dm_msg_prefix, MAJOR(bio->bi_bdev->bd_dev),
			 MINOR(bio->bi_bdev->bd_dev), op, sector, result);
	audit_log_end(ab);
}
EXPORT_SYMBOL_GPL(dm_audit_log_bio);

void dm_audit_log_target(const char *dm_msg_prefix, const char *op,
			 struct dm_target *ti, int result)
{
	struct audit_buffer *ab;
	struct mapped_device *md = dm_table_get_md(ti->table);

	if (audit_enabled == AUDIT_OFF)
		return;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_DM);
	if (unlikely(!ab))
		return;

	audit_log_format(ab, "module=%s dev=%s op=%s",
			 dm_msg_prefix, dm_device_name(md), op);

	if (!result && !strcmp("ctr", op))
		audit_log_format(ab, " error_msg='%s'", ti->error);
	audit_log_format(ab, " res=%d", result);
	audit_log_end(ab);
}
EXPORT_SYMBOL_GPL(dm_audit_log_target);
