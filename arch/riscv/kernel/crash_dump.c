// SPDX-License-Identifier: GPL-2.0
/*
 * This code comes from arch/arm64/kernel/crash_dump.c
 * Created by: AKASHI Takahiro <takahiro.akashi@linaro.org>
 * Copyright (C) 2017 Linaro Limited
 */

#include <linux/crash_dump.h>
#include <linux/io.h>

/**
 * copy_oldmem_page() - copy one page from old kernel memory
 * @pfn: page frame number to be copied
 * @buf: buffer where the copied page is placed
 * @csize: number of bytes to copy
 * @offset: offset in bytes into the page
 * @userbuf: if set, @buf is in a user address space
 *
 * This function copies one page from old kernel memory into buffer pointed by
 * @buf. If @buf is in userspace, set @userbuf to %1. Returns number of bytes
 * copied or negative error in case of failure.
 */
ssize_t copy_oldmem_page(struct iov_iter *iter, unsigned long pfn,
			 size_t csize, unsigned long offset)
{
	void *vaddr;

	if (!csize)
		return 0;

	vaddr = memremap(__pfn_to_phys(pfn), PAGE_SIZE, MEMREMAP_WB);
	if (!vaddr)
		return -ENOMEM;

	csize = copy_to_iter(vaddr + offset, csize, iter);

	memunmap(vaddr);
	return csize;
}
