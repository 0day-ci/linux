// SPDX-License-Identifier: GPL-2.0
/*
 * Platform support and IPC debug console to linux-loader (on Starbuck)
 *
 * Nintendo Wii U bootwrapper support
 * Copyright (C) 2022 The linux-wiiu Team
 */

#include <stddef.h>
#include "string.h"
#include "stdio.h"
#include "types.h"
#include "io.h"
#include "ops.h"

BSS_STACK(8192);

// Volatile is used here since the io.h routines require it
#define LT_IPC_PPCMSG ((volatile u32 *)0x0d800000)
#define LT_IPC_PPCCTRL ((volatile u32 *)0x0d800004)
#define LT_IPC_PPCCTRL_X1 0x1

#define WIIU_LOADER_CMD_PRINT 0x01000000

static void wiiu_ipc_sendmsg(int msg)
{
	out_be32(LT_IPC_PPCMSG, msg);
	out_be32(LT_IPC_PPCCTRL, LT_IPC_PPCCTRL_X1);
	while (in_be32(LT_IPC_PPCCTRL) & LT_IPC_PPCCTRL_X1)
		barrier();
}

/*
 * Send logging string out over IPC to linux-loader for early printing.
 * Packs 3 chars at a time where possible.
 */
static void wiiu_write_ipc(const char *buf, int len)
{
	int i = 0;

	for (i = 0; i + 2 < len; i += 3) {
		int msg = WIIU_LOADER_CMD_PRINT | (buf[i + 0] << 16) |
			  (buf[i + 1] << 8) | buf[i + 2];

		wiiu_ipc_sendmsg(msg);
	}

	if (i < len) {
		for (; i < len; i++) {
			int msg = WIIU_LOADER_CMD_PRINT | (buf[i] << 16);

			wiiu_ipc_sendmsg(msg);
		}
	}
}

/*
 * Note 32MiB heap - not ideal but seems fine for the bootwrapper
 */
void platform_init(unsigned int r3, unsigned int r4, unsigned int r5)
{
	u32 heapsize;

	console_ops.write = wiiu_write_ipc;
	printf("wiiu: bootwrapper ok\n");

	heapsize = 32 * 1024 * 1024 - (u32)_end;
	simple_alloc_init(_end, heapsize, 32, 64);
	printf("wiiu: heap ok\n");

	fdt_init(_dtb_start);
	printf("wiiu: dtb ok\n");
}
