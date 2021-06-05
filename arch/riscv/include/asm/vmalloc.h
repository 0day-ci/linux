#ifndef _ASM_RISCV_VMALLOC_H
#define _ASM_RISCV_VMALLOC_H

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

#define IOREMAP_MAX_ORDER (PMD_SHIFT)

#define arch_vmap_pmd_supported	arch_vmap_pmd_supported
static inline bool __init arch_vmap_pmd_supported(pgprot_t prot)
{
	return true;
}

#endif

#endif /* _ASM_RISCV_VMALLOC_H */
