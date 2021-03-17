/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_GENERIC_MODULE_LDS_H
#define __ASM_GENERIC_MODULE_LDS_H

/*
 * <asm/module.lds.h> can specify arch-specific sections for linking modules.
 * Empty for the asm-generic header.
 */

/* implement dynamic printk debug section packing */
#if defined(CONFIG_DYNAMIC_DEBUG) ||					\
	(defined(CONFIG_DYNAMIC_DEBUG_CORE)				\
	 && defined(DYNAMIC_DEBUG_MODULE))
#define DYNAMIC_DEBUG_DATA()						\
	. = ALIGN(8);							\
	KEEP(*(__dyndbg_sites .gnu.linkonce.dyndbg_site))		\
	KEEP(*(__dyndbg .gnu.linkonce.dyndbg))
#else
#define DYNAMIC_DEBUG_DATA()
#endif

SECTIONS {
__dyndbg	: { (*(__dyndbg .gnu.linkonce.dyndbg)) }
__dyndbg_sites	: { (*(__dyndbg_sites .gnu.linkonce.dyndbg_site)) }

	//.data.dyndbg : { DYNAMIC_DEBUG_DATA() } // syntax ok
	//: { DYNAMIC_DEBUG_DATA() }
	//DYNAMIC_DEBUG_DATA()
}

#endif /* __ASM_GENERIC_MODULE_LDS_H */
