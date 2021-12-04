/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ANON_VMA_H
#define _LINUX_ANON_VMA_H

#include <linux/mm_types.h>

#ifdef CONFIG_ANON_VMA_NAME
/*
 * mmap_lock should be read-locked when calling vma_anon_name() and while using
 * the returned pointer.
 */
extern const char *vma_anon_name(struct vm_area_struct *vma);

/*
 * mmap_lock should be read-locked for orig_vma->vm_mm.
 * mmap_lock should be write-locked for new_vma->vm_mm or new_vma should be
 * isolated.
 */
extern void dup_vma_anon_name(struct vm_area_struct *orig_vma,
			      struct vm_area_struct *new_vma);

/*
 * mmap_lock should be write-locked or vma should have been isolated under
 * write-locked mmap_lock protection.
 */
extern void free_vma_anon_name(struct vm_area_struct *vma);

/* mmap_lock should be read-locked */
static inline bool is_same_vma_anon_name(struct vm_area_struct *vma,
					 const char *name)
{
	const char *vma_name = vma_anon_name(vma);

	/* either both NULL, or pointers to same string */
	if (vma_name == name)
		return true;

	return name && vma_name && !strcmp(name, vma_name);
}
#else /* CONFIG_ANON_VMA_NAME */
static inline const char *vma_anon_name(struct vm_area_struct *vma)
{
	return NULL;
}
static inline void dup_vma_anon_name(struct vm_area_struct *orig_vma,
			      struct vm_area_struct *new_vma) {}
static inline void free_vma_anon_name(struct vm_area_struct *vma) {}
static inline bool is_same_vma_anon_name(struct vm_area_struct *vma,
					 const char *name)
{
	return true;
}
#endif  /* CONFIG_ANON_VMA_NAME */

#endif /* _LINUX_ANON_VMA_H */
