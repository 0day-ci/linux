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

extern struct module_attribute *modinfo_attrs[];
extern size_t modinfo_attrs_count;

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

struct symsearch {
	const struct kernel_symbol *start, *stop;
	const s32 *crcs;
	enum mod_license {
		NOT_GPL_ONLY,
		GPL_ONLY,
	} license;
};

struct find_symbol_arg {
	/* Input */
	const char *name;
	bool gplok;
	bool warn;

	/* Output */
	struct module *owner;
	const s32 *crc;
	const struct kernel_symbol *sym;
	enum mod_license license;
};

extern int mod_verify_sig(const void *mod, struct load_info *info);
extern int try_to_force_load(struct module *mod, const char *reason);
extern bool find_symbol(struct find_symbol_arg *fsa);
extern struct module *find_module_all(const char *name, size_t len, bool even_unformed);
extern unsigned long kernel_symbol_value(const struct kernel_symbol *sym);
extern int cmp_name(const void *name, const void *sym);
extern long get_offset(struct module *mod, unsigned int *size, Elf_Shdr *sechdr,
		       unsigned int section);
extern char *module_flags(struct module *mod, char *buf);

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

#ifdef CONFIG_SYSFS
extern int mod_sysfs_setup(struct module *mod, const struct load_info *info,
			   struct kernel_param *kparam, unsigned int num_params);
extern void mod_sysfs_fini(struct module *mod);
extern void module_remove_modinfo_attrs(struct module *mod, int end);
extern void del_usage_links(struct module *mod);
extern void init_param_lock(struct module *mod);
#else /* !CONFIG_SYSFS */
static int mod_sysfs_setup(struct module *mod,
			   const struct load_info *info,
			   struct kernel_param *kparam,
			   unsigned int num_params)
{
	return 0;
}
static inline void mod_sysfs_fini(struct module *mod) { }
static inline void module_remove_modinfo_attrs(struct module *mod, int end) { }
static inline void del_usage_links(struct module *mod) { }
static inline void init_param_lock(struct module *mod) { }
#endif /* CONFIG_SYSFS */

#ifdef CONFIG_MODVERSIONS
extern int check_version(const struct load_info *info,
			 const char *symname, struct module *mod, const s32 *crc);
extern int check_modstruct_version(const struct load_info *info, struct module *mod);
extern int same_magic(const char *amagic, const char *bmagic, bool has_crcs);
#else /* !CONFIG_MODVERSIONS */
static inline int check_version(const struct load_info *info,
				const char *symname,
				struct module *mod,
				const s32 *crc)
{
	return 1;
}

static inline int check_modstruct_version(const struct load_info *info,
					  struct module *mod)
{
	return 1;
}

static inline int same_magic(const char *amagic, const char *bmagic,
			    bool has_crcs)
{
	return strcmp(amagic, bmagic) == 0;
}
#endif /* CONFIG_MODVERSIONS */
