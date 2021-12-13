// SPDX-License-Identifier: GPL-2.0
/*
 *	Memory preserving reboot related code.
 *
 *	Created by: Hariprasad Nellitheertha (hari@in.ibm.com)
 *	Copyright (C) IBM Corporation, 2004. All rights reserved
 */

#include <linux/errno.h>
#include <linux/crash_dump.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cc_platform.h>

static ssize_t __copy_oldmem_page(struct iov_iter *iter, unsigned long pfn,
				  size_t csize, unsigned long offset, 
				  bool encrypted)
{
	void  *vaddr;

	if (!csize)
		return 0;

	if (encrypted)
		vaddr = (__force void *)ioremap_encrypted(pfn << PAGE_SHIFT, PAGE_SIZE);
	else
		vaddr = (__force void *)ioremap_cache(pfn << PAGE_SHIFT, PAGE_SIZE);

	if (!vaddr)
		return -ENOMEM;

	csize = copy_to_iter(vaddr + offset, csize, iter);

	set_iounmap_nonlazy();
	iounmap((void __iomem *)vaddr);
	return csize;
}

/**
 * copy_oldmem_page - copy one page of memory
 * @pfn: page frame number to be copied
 * @buf: target memory address for the copy; this can be in kernel address
 *	space or user address space (see @userbuf)
 * @csize: number of bytes to copy
 * @offset: offset in bytes into the page (based on pfn) to begin the copy
 * @userbuf: if set, @buf is in user address space, use copy_to_user(),
 *	otherwise @buf is in kernel address space, use memcpy().
 *
 * Copy a page from the old kernel's memory. For this page, there is no pte
 * mapped in the current kernel. We stitch up a pte, similar to kmap_atomic.
 */
ssize_t copy_oldmem_page(struct iov_iter *iter, unsigned long pfn, size_t csize,
			 unsigned long offset)
{
	return __copy_oldmem_page(iter, pfn, csize, offset, false);
}

/**
 * copy_oldmem_page_encrypted - same as copy_oldmem_page() above but ioremap the
 * memory with the encryption mask set to accommodate kdump on SME-enabled
 * machines.
 */
ssize_t copy_oldmem_page_encrypted(struct iov_iter *iter, unsigned long pfn,
				   size_t csize, unsigned long offset)
{
	return __copy_oldmem_page(iter, pfn, csize, offset, true);
}

ssize_t elfcorehdr_read(char *buf, size_t count, u64 *ppos)
{
	struct kvec kvec = { .iov_base = buf, .iov_len = count };
	struct iov_iter iter;

	iov_iter_kvec(&iter, READ, &kvec, 1, count);

	return read_from_oldmem(&iter, count, ppos,
				cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT));
}
