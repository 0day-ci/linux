// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * kernel/module/arch_strict_rwx.c - module arch strict rwx
 *
 * Copyright (C) 2015 Rusty Russell
 */

#include <linux/module.h>
#include <linux/set_memory.h>

/*
 * LKM RO/NX protection: protect module's text/ro-data
 * from modification and any data from execution.
 *
 * General layout of module is:
 *          [text] [read-only-data] [ro-after-init] [writable data]
 * text_size -----^                ^               ^               ^
 * ro_size ------------------------|               |               |
 * ro_after_init_size -----------------------------|               |
 * size -----------------------------------------------------------|
 *
 * These values are always page-aligned (as is base)
 */

/*
 * Since some arches are moving towards PAGE_KERNEL module allocations instead
 * of PAGE_KERNEL_EXEC, keep frob_text() and module_enable_x() outside of the
 * CONFIG_STRICT_MODULE_RWX block below because they are needed regardless of
 * whether we are strict.
 */
void frob_text(const struct module_layout *layout,
		      int (*set_memory)(unsigned long start, int num_pages))
{
	BUG_ON((unsigned long)layout->base & (PAGE_SIZE-1));
	BUG_ON((unsigned long)layout->text_size & (PAGE_SIZE-1));
	set_memory((unsigned long)layout->base,
		   layout->text_size >> PAGE_SHIFT);
}

void module_enable_x(const struct module *mod)
{
	frob_text(&mod->core_layout, set_memory_x);
	frob_text(&mod->init_layout, set_memory_x);
}
