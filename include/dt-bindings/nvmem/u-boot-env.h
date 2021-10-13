/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Macros for U-Boot environment NVMEM device tree bindings.
 *
 * Copyright (C) 2021 Marek Behún <kabel@kernel.org>
 */

#ifndef __DT_BINDINGS_NVMEM_U_BOOT_ENV_H
#define __DT_BINDINGS_NVMEM_U_BOOT_ENV_H

#define U_BOOT_ENV_TYPE_STRING		0
#define U_BOOT_ENV_TYPE_ULONG		1
#define U_BOOT_ENV_TYPE_BOOL		2
#define U_BOOT_ENV_TYPE_MAC_ADDRESS	3
#define U_BOOT_ENV_TYPE_ULONG_HEX	4
#define U_BOOT_ENV_TYPE_ULONG_DEC	5

#endif /* __DT_BINDINGS_NVMEM_U_BOOT_ENV_H */
