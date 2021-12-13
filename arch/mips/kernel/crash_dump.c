// SPDX-License-Identifier: GPL-2.0
#include <linux/highmem.h>
#include <linux/crash_dump.h>

/**
 * copy_oldmem_page - copy one page from "oldmem"
 * @pfn: page frame number to be copied
 * @buf: target memory address for the copy; this can be in kernel address
 *	space or user address space (see @userbuf)
 * @csize: number of bytes to copy
 * @offset: offset in bytes into the page (based on pfn) to begin the copy
 * @userbuf: if set, @buf is in user address space, use copy_to_user(),
 *	otherwise @buf is in kernel address space, use memcpy().
 *
 * Copy a page from "oldmem". For this page, there is no pte mapped
 * in the current kernel.
 */
ssize_t copy_oldmem_page(struct iov_iter *iter, unsigned long pfn,
			 size_t csize, unsigned long offset)
{
	void  *vaddr;

	if (!csize)
		return 0;

	vaddr = kmap_local_pfn(pfn);
	csize = copy_to_iter(vaddr + offset, csize, iter);
	kunmap_local(vaddr);

	return csize;
}
