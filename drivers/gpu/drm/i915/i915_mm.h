/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_MM_H__
#define __I915_MM_H__

#if IS_ENABLED(CONFIG_X86)
int remap_io_mapping(struct vm_area_struct *vma,
                     unsigned long addr, unsigned long pfn, unsigned long size,
                     struct io_mapping *iomap);
int remap_io_sg(struct vm_area_struct *vma,
                unsigned long addr, unsigned long size,
                struct scatterlist *sgl, resource_size_t iobase);
#else
static inline int remap_io_mapping(struct vm_area_struct *vma,
                     unsigned long addr, unsigned long pfn, unsigned long size,
                     struct io_mapping *iomap)
{
        return 0;
}
static inline int remap_io_sg(struct vm_area_struct *vma,
                unsigned long addr, unsigned long size,
                struct scatterlist *sgl, resource_size_t iobase)
{
        return 0;
}
#endif

#endif /* __I915_MM_H__ */
