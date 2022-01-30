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
#ifdef CONFIG_MODULE_DECOMPRESS
	struct page **pages;
	unsigned int max_pages;
	unsigned int used_pages;
#endif
	struct {
		unsigned int sym, str, mod, vers, info, pcpu;
	} index;
};

extern int mod_verify_sig(const void *mod, struct load_info *info);
extern struct module *find_module_all(const char *name, size_t len, bool even_unformed);
extern unsigned long kernel_symbol_value(const struct kernel_symbol *sym);
extern int cmp_name(const void *name, const void *sym);
extern long get_offset(struct module *mod, unsigned int *size, Elf_Shdr *sechdr,
		       unsigned int section);

#ifdef CONFIG_LIVEPATCH
extern int copy_module_elf(struct module *mod, struct load_info *info);
extern void free_module_elf(struct module *mod);
#else /* !CONFIG_LIVEPATCH */
static inline int copy_module_elf(struct module *mod, struct load_info *info)
{
	return 0;
}
static inline void free_module_elf(struct module *mod) { }
#endif /* CONFIG_LIVEPATCH */

#ifdef CONFIG_MODULE_DECOMPRESS
int module_decompress(struct load_info *info, const void *buf, size_t size);
void module_decompress_cleanup(struct load_info *info);
#else
static inline int module_decompress(struct load_info *info,
				    const void *buf, size_t size)
{
	return -EOPNOTSUPP;
}
static inline void module_decompress_cleanup(struct load_info *info)
{
}
#endif

#ifdef CONFIG_MODULES_TREE_LOOKUP
struct mod_tree_root {
	struct latch_tree_root root;
	unsigned long addr_min;
	unsigned long addr_max;
};

extern struct mod_tree_root mod_tree;

extern void mod_tree_insert(struct module *mod);
extern void mod_tree_remove_init(struct module *mod);
extern void mod_tree_remove(struct module *mod);
extern struct module *mod_find(unsigned long addr);
#else /* !CONFIG_MODULES_TREE_LOOKUP */
static unsigned long module_addr_min = -1UL, module_addr_max = 0;

static void mod_tree_insert(struct module *mod) { }
static void mod_tree_remove_init(struct module *mod) { }
static void mod_tree_remove(struct module *mod) { }
static struct module *mod_find(unsigned long addr)
{
	struct module *mod;

	list_for_each_entry_rcu(mod, &modules, list,
				lockdep_is_held(&module_mutex)) {
		if (within_module(addr, mod))
			return mod;
	}

	return NULL;
}
#endif /* CONFIG_MODULES_TREE_LOOKUP */

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
static inline void __maybe_unused kmemleak_load_module(const struct module *mod,
						       const struct load_info *info) { }
#endif /* CONFIG_DEBUG_KMEMLEAK */

#ifdef CONFIG_KALLSYMS
#ifdef CONFIG_STACKTRACE_BUILD_ID
extern void init_build_id(struct module *mod, const struct load_info *info);
#else /* !CONFIG_STACKTRACE_BUILD_ID */
static inline void init_build_id(struct module *mod, const struct load_info *info) { }

#endif
extern void layout_symtab(struct module *mod, struct load_info *info);
extern void add_kallsyms(struct module *mod, const struct load_info *info);
extern bool sect_empty(const Elf_Shdr *sect);
extern const char *find_kallsyms_symbol(struct module *mod, unsigned long addr,
					unsigned long *size, unsigned long *offset);
#else /* !CONFIG_KALLSYMS */
static inline void layout_symtab(struct module *mod, struct load_info *info) { }
static inline void add_kallsyms(struct module *mod, const struct load_info *info) { }
static inline char *find_kallsyms_symbol(struct module *mod, unsigned long addr,
					 unsigned long *size, unsigned long *offset)
{
	return NULL;
}
#endif /* CONFIG_KALLSYMS */
