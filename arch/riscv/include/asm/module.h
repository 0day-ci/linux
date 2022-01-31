/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

#ifndef _ASM_RISCV_MODULE_H
#define _ASM_RISCV_MODULE_H

#include <asm-generic/module.h>

struct module;
unsigned long module_emit_got_entry(struct module *mod, unsigned long val);
unsigned long module_emit_plt_entry(struct module *mod, unsigned long val);

#ifdef CONFIG_MODULE_SECTIONS
struct mod_section {
	Elf_Shdr *shdr;
	int num_entries;
	int max_entries;
};

struct mod_arch_specific {
	struct mod_section got;
	struct mod_section plt;
	struct mod_section got_plt;
};
#endif /* CONFIG_MODULE_SECTIONS */

#endif /* _ASM_RISCV_MODULE_H */
