/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nintendo Wii U udbg support (to Starbuck coprocessor, via chipset IPC)
 *
 * Copyright (C) 2022 The linux-wiiu Team
 *
 * Based on arch/powerpc/platforms/embedded6xx/udbgecko_udbg.h
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008-2009 Albert Herranz
 */

#ifndef __LATTEIPC_UDBG_H
#define __LATTEIPC_UDBG_H

#ifdef CONFIG_LATTEIPC_UDBG

void __init latteipc_udbg_init(void);

#else

static inline void __init latteipc_udbg_init(void)
{
}

#endif /* CONFIG_LATTEIPC_UDBG */

#endif /* __LATTEIPC_UDBG_H */
