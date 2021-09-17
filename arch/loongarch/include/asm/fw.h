/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef __ASM_FW_H_
#define __ASM_FW_H_

#include <asm/bootinfo.h>

extern int fw_argc;
extern long *_fw_argv, *_fw_envp;

#define fw_argv(index)		((char *)(long)_fw_argv[(index)])
#define fw_envp(index)		((char *)(long)_fw_envp[(index)])

extern void fw_init_cmdline(void);

#endif /* __ASM_FW_H_ */
