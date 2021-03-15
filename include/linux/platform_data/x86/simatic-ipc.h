/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Siemens SIMATIC IPC drivers
 *
 * Copyright (c) Siemens AG, 2018-2021
 *
 * Authors:
 *  Henning Schild <henning.schild@siemens.com>
 *  Gerd Haeussler <gerd.haeussler.ext@siemens.com>
 */

#ifndef __PLATFORM_DATA_X86_SIMATIC_IPC_H
#define __PLATFORM_DATA_X86_SIMATIC_IPC_H

#include <linux/dmi.h>
#include <linux/platform_data/x86/simatic-ipc-base.h>

#define DMI_ENTRY_OEM	129

enum ipc_station_ids {
	SIMATIC_IPC_INVALID_STATION_ID = 0,
	SIMATIC_IPC_IPC227D = 0x00000501,
	SIMATIC_IPC_IPC427D = 0x00000701,
	SIMATIC_IPC_IPC227E = 0x00000901,
	SIMATIC_IPC_IPC277E = 0x00000902,
	SIMATIC_IPC_IPC427E = 0x00000A01,
	SIMATIC_IPC_IPC477E = 0x00000A02,
	SIMATIC_IPC_IPC127E = 0x00000D01,
};

static inline u32 simatic_ipc_get_station_id(u8 *data, int max_len)
{
	u32 station_id = SIMATIC_IPC_INVALID_STATION_ID;
	int i;
	struct {
		u8	type;		/* type (0xff = binary) */
		u8	len;		/* len of data entry */
		u8	reserved[3];
		u32	station_id;	/* station id (LE) */
	} __packed
	*data_entry = (void *)data + sizeof(struct dmi_header);

	/* find 4th entry in OEM data */
	for (i = 0; i < 3; i++)
		data_entry = (void *)((u8 *)(data_entry) + data_entry->len);

	/* decode station id */
	if (data_entry && (u8 *)data_entry < data + max_len &&
	    data_entry->type == 0xff && data_entry->len == 9)
		station_id = le32_to_cpu(data_entry->station_id);

	return station_id;
}

static inline void
simatic_ipc_find_dmi_entry_helper(const struct dmi_header *dh, void *_data)
{
	u32 *id = _data;

	if (dh->type != DMI_ENTRY_OEM)
		return;

	*id = simatic_ipc_get_station_id((u8 *)dh, dh->length);
}

#endif /* __PLATFORM_DATA_X86_SIMATIC_IPC_H */
