/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_SECTIONS_H
#define _PARISC_SECTIONS_H

#ifdef CONFIG_64BIT
#define HAVE_DEREFERENCE_FUNCTION_DESCRIPTOR 1
typedef Elf64_Fdesc funct_descr_t;
#endif

/* nothing to see, move along */
#include <asm-generic/sections.h>

extern char __alt_instructions[], __alt_instructions_end[];

#endif
