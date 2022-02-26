// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/string.h>

#include <asm/addrspace.h>
#include <asm/early_ioremap.h>
#include <asm/fw.h>

int fw_argc;
long *_fw_argv, *_fw_envp;

void __init fw_init_cmdline(void)
{
	int i;

	fw_argc = fw_arg0;
	_fw_argv = (long *)early_memremap_ro(fw_arg1, SZ_16K);
	_fw_envp = (long *)early_memremap_ro(fw_arg2, SZ_64K);

	arcs_cmdline[0] = '\0';
	for (i = 1; i < fw_argc; i++) {
		strlcat(arcs_cmdline, fw_argv(i), COMMAND_LINE_SIZE);
		if (i < (fw_argc - 1))
			strlcat(arcs_cmdline, " ", COMMAND_LINE_SIZE);
	}
}
