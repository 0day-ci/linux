// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IOMAP_H__
#define __XFS_IOMAP_H__

#include <linux/iomap.h>

struct xfs_inode;
struct xfs_bmbt_irec;

int xfs_iomap_write_direct(struct xfs_inode *ip, xfs_fileoff_t offset_fsb,
		xfs_fileoff_t count_fsb, struct xfs_bmbt_irec *imap);
int xfs_iomap_write_unwritten(struct xfs_inode *, xfs_off_t, xfs_off_t, bool);
xfs_fileoff_t xfs_iomap_eof_align_last_fsb(struct xfs_inode *ip,
		xfs_fileoff_t end_fsb);

int xfs_bmbt_to_iomap(struct xfs_inode *, struct iomap *,
		struct xfs_bmbt_irec *, u16);

static inline xfs_filblks_t
xfs_aligned_fsb_count(
	xfs_fileoff_t		offset_fsb,
	xfs_filblks_t		count_fsb,
	xfs_extlen_t		extsz)
{
	if (extsz) {
		xfs_extlen_t	align;

		div_u64_rem(offset_fsb, extsz, &align);
		if (align)
			count_fsb += align;
		div_u64_rem(count_fsb, extsz, &align);
		if (align)
			count_fsb += extsz - align;
	}

	return count_fsb;
}

extern const struct iomap_ops xfs_buffered_write_iomap_ops;
extern const struct iomap_ops xfs_direct_write_iomap_ops;
extern const struct iomap_ops xfs_read_iomap_ops;
extern const struct iomap_ops xfs_seek_iomap_ops;
extern const struct iomap_ops xfs_xattr_iomap_ops;
extern const struct iomap_ops xfs_dax_write_iomap_ops;

static inline int
xfs_iomap_zero_range(
	struct xfs_inode	*ip,
	loff_t			pos,
	loff_t			len,
	bool			*did_zero)
{
	struct inode		*inode = VFS_I(ip);

	return iomap_zero_range(inode, pos, len, did_zero,
			IS_DAX(inode) ?
				&xfs_dax_write_iomap_ops :
				&xfs_buffered_write_iomap_ops);
}

static inline int
xfs_iomap_truncate_page(
	struct xfs_inode	*ip,
	loff_t			pos,
	bool			*did_zero)
{
	struct inode		*inode = VFS_I(ip);

	return iomap_truncate_page(inode, pos, did_zero,
			IS_DAX(inode)?
				&xfs_dax_write_iomap_ops :
				&xfs_buffered_write_iomap_ops);
}

#endif /* __XFS_IOMAP_H__*/
