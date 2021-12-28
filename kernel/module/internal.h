/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Module internals
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/elf.h>
#include <linux/compiler.h>
#include <asm/module.h>
#include <linux/mutex.h>

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG-1))
/* Maximum number of characters written by module_flags() */
#define MODULE_FLAGS_BUF_SIZE (TAINT_FLAGS_COUNT + 4)
#define MODULE_SECT_READ_SIZE (3 /* "0x", "\n" */ + (BITS_PER_LONG / 4))

extern struct mutex module_mutex;
extern struct list_head modules;

/* Provided by the linker */
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___ksymtab_gpl[];
extern const struct kernel_symbol __stop___ksymtab_gpl[];
extern const s32 __start___kcrctab[];
extern const s32 __start___kcrctab_gpl[];

struct load_info {
	const char *name;
	/* pointer to module in temporary copy, freed at end of load_module() */
	struct module *mod;
	Elf_Ehdr *hdr;
	unsigned long len;
	Elf_Shdr *sechdrs;
	char *secstrings, *strtab;
	unsigned long symoffs, stroffs, init_typeoffs, core_typeoffs;
	struct _ddebug *debug;
	unsigned int num_debug;
	bool sig_ok;
#ifdef CONFIG_KALLSYMS
	unsigned long mod_kallsyms_init_off;
#endif
	struct {
		unsigned int sym, str, mod, vers, info, pcpu;
	} index;
};

extern int mod_verify_sig(const void *mod, struct load_info *info);

#ifdef CONFIG_LIVEPATCH
extern int copy_module_elf(struct module *mod, struct load_info *info);
extern void free_module_elf(struct module *mod);
extern int check_modinfo_livepatch(struct module *mod, struct load_info *info);
#else /* !CONFIG_LIVEPATCH */
static inline int copy_module_elf(struct module *mod, struct load_info *info)
{
	return 0;
}
static inline void free_module_elf(struct module *mod) { }
#endif /* CONFIG_LIVEPATCH */

#ifdef CONFIG_MODULE_SIG
extern int module_sig_check(struct load_info *info, int flags);
#else /* !CONFIG_MODULE_SIG */
static int module_sig_check(struct load_info *info, int flags)
{
	return 0;
}
#endif /* !CONFIG_MODULE_SIG */

#ifdef CONFIG_DEBUG_KMEMLEAK
extern void kmemleak_load_module(const struct module *mod, const struct load_info *info);
#else /* !CONFIG_DEBUG_KMEMLEAK */
static inline void __maybe_unused kmemleak_load_module(const struct module *mod, const struct load_info *info) { }
#endif /* CONFIG_DEBUG_KMEMLEAK */
