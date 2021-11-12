// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 * Copyright 2021 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 */

/*
 * Implementation of fsverity_operations for tmpfs.
 *
 * Like ext4, tmpfs stores the verity metadata (Merkle tree and
 * fsverity_descriptor) past the end of the file, starting at the first 64K
 * boundary beyond i_size.
 *
 * Using a 64K boundary rather than a 4K one keeps things ready for
 * architectures with 64K pages, and it doesn't necessarily waste space on-disk
 * since there can be a hole between i_size and the start of the Merkle tree.
 */

#include <linux/xattr.h>
#include <linux/fsverity.h>
#include <linux/shmem_fs.h>
#include <linux/quotaops.h>

#define SHMEM_VERIFY_VER	(1)

static inline loff_t shmem_verity_metadata_pos(const struct inode *inode)
{
	return round_up(inode->i_size, 65536);
}

/*
 * Read some verity metadata from the inode.  __vfs_read() can't be used because
 * we need to read beyond i_size.
 */
static int pagecache_read(struct inode *inode, void *buf, size_t count,
			  loff_t pos)
{
	while (count) {
		size_t n = min_t(size_t, count,
				 PAGE_SIZE - offset_in_page(pos));
		struct page *page;
		void *addr;

		page = shmem_read_mapping_page(inode->i_mapping,
					       pos >> PAGE_SHIFT);
		if (IS_ERR(page))
			return PTR_ERR(page);

		addr = kmap_atomic(page);
		memcpy(buf, addr + offset_in_page(pos), n);
		kunmap_atomic(addr);

		put_page(page);

		buf += n;
		pos += n;
		count -= n;
	}
	return 0;
}

/*
 * Write some verity metadata to the inode for FS_IOC_ENABLE_VERITY.
 * kernel_write() can't be used because the file descriptor is readonly.
 */
static int pagecache_write(struct inode *inode, const void *buf, size_t count,
			   loff_t pos)
{
	if (pos + count > inode->i_sb->s_maxbytes)
		return -EFBIG;

	while (count) {
		size_t n = min_t(size_t, count,
				 PAGE_SIZE - offset_in_page(pos));
		struct page *page;
		void *fsdata;
		void *addr;
		int res;

		res = pagecache_write_begin(NULL, inode->i_mapping, pos, n, 0,
					    &page, &fsdata);
		if (res)
			return res;

		addr = kmap_atomic(page);
		memcpy(addr + offset_in_page(pos), buf, n);
		kunmap_atomic(addr);

		res = pagecache_write_end(NULL, inode->i_mapping, pos, n, n,
					  page, fsdata);
		if (res < 0)
			return res;
		if (res != n)
			return -EIO;

		buf += n;
		pos += n;
		count -= n;
	}
	return 0;
}

/*
 * Format of tmpfs verity xattr.  This points to the location of the verity
 * descriptor within the file data rather than containing it (the code was taken
 * from fs/f2fs/verity.c).
 */
struct fsverity_descriptor_location {
	__le32 version;
	__le32 size;
	__le64 pos;
};

static int shmem_begin_enable_verity(struct file *filp)
{
	struct inode *inode = file_inode(filp);
	int err;

	if (shmem_verity_in_progress(inode))
		return -EBUSY;

	/*
	 * Since the file was opened readonly, we have to initialize the quotas
	 * here and not rely on ->open() doing it.
	 */
	err = dquot_initialize(inode);
	if (err)
		return err;

	shmem_verity_set_in_progress(inode);
	return 0;
}

static int shmem_end_enable_verity(struct file *filp, const void *desc,
				   size_t desc_size, u64 merkle_tree_size)
{
	struct inode *inode = file_inode(filp);
	struct shmem_inode_info *info = SHMEM_I(inode);
	u64 desc_pos = shmem_verity_metadata_pos(inode) + merkle_tree_size;
	struct fsverity_descriptor_location dloc = {
		.version = cpu_to_le32(SHMEM_VERIFY_VER),
		.size = cpu_to_le32(desc_size),
		.pos = cpu_to_le64(desc_pos),
	};
	int err = 0;

	/*
	 * If an error already occurred (which fs/verity/ signals by passing
	 * desc == NULL), then only clean-up is needed.
	 */
	if (desc == NULL)
		goto cleanup;

	/* Append the verity descriptor. */
	err = pagecache_write(inode, desc, desc_size, desc_pos);
	if (err)
		goto cleanup;

	/*
	 * Write all pages (both data and verity metadata).  Note that this must
	 * happen before clearing SHMEM_VERITY_IN_PROGRESS; otherwise pages
	 * beyond i_size won't be written properly.
	 */
	err = filemap_write_and_wait(inode->i_mapping);
	if (err)
		goto cleanup;

	/* Set the verity xattr. */
	err = simple_xattr_set(&info->xattrs, SHMEM_XATTR_NAME_VERITY, &dloc,
			       sizeof(dloc), XATTR_CREATE, NULL);
	if (err)
		goto cleanup;

	/* Finally, set the verity inode flag. */
	inode_set_flags(inode, S_VERITY, inode->i_flags | S_VERITY);
	mark_inode_dirty_sync(inode);

	shmem_verity_clear_in_progress(inode);
	return 0;

cleanup:
	/*
	 * Verity failed to be enabled, so clean up by truncating any verity
	 * metadata that was written beyond i_size (both from cache and from
	 * disk) and clearing FI_VERITY_IN_PROGRESS.
	 */
	shmem_truncate_range(inode, 0, inode->i_size);
	shmem_verity_clear_in_progress(inode);
	return err;
}

static int shmem_get_verity_descriptor(struct inode *inode, void *buf,
				       size_t buf_size)
{
	struct fsverity_descriptor_location dloc;
	struct shmem_inode_info *info = SHMEM_I(inode);
	int res;
	u32 size;
	u64 pos;

	/* Get the descriptor location */
	res = simple_xattr_get(&info->xattrs, SHMEM_XATTR_NAME_VERITY, &dloc,
			       sizeof(dloc));
	if (res < 0 && res != -ERANGE)
		return res;
	if (res != sizeof(dloc) ||
	    dloc.version != cpu_to_le32(SHMEM_VERIFY_VER)) {
		pr_err("Unknown verity xattr format inode %lu\n", inode->i_ino);
		return -EINVAL;
	}
	size = le32_to_cpu(dloc.size);
	pos = le64_to_cpu(dloc.pos);

	/* Get the descriptor */
	if (pos + size < pos || pos + size > inode->i_sb->s_maxbytes ||
	    pos < shmem_verity_metadata_pos(inode) || size > INT_MAX) {
		pr_err("Invalid verity xattr for inode %lu\n", inode->i_ino);
		return -EINVAL;
	}
	if (buf_size) {
		if (size > buf_size)
			return -ERANGE;
		res = pagecache_read(inode, buf, size, pos);
		if (res)
			return res;
	}
	return size;
}

static struct page *shmem_read_merkle_tree_page(struct inode *inode,
						pgoff_t index,
						unsigned long num_ra_pages)
{
	DEFINE_READAHEAD(ractl, NULL, NULL, inode->i_mapping, index);
	struct page *page;

	index += shmem_verity_metadata_pos(inode) >> PAGE_SHIFT;

	page = find_get_page_flags(inode->i_mapping, index, FGP_ACCESSED);
	if (!page || !PageUptodate(page)) {
		if (page)
			put_page(page);
		else if (num_ra_pages > 1)
			page_cache_ra_unbounded(&ractl, num_ra_pages, 0);
		page = shmem_read_mapping_page(inode->i_mapping, index);
	}
	return page;
}

static int shmem_write_merkle_tree_block(struct inode *inode, const void *buf,
					 u64 index, int log_blocksize)
{
	loff_t pos = shmem_verity_metadata_pos(inode) +
		     (index << log_blocksize);

	return pagecache_write(inode, buf, 1 << log_blocksize, pos);
}

const struct fsverity_operations shmem_verityops = {
	.begin_enable_verity	= shmem_begin_enable_verity,
	.end_enable_verity	= shmem_end_enable_verity,
	.get_verity_descriptor	= shmem_get_verity_descriptor,
	.read_merkle_tree_page	= shmem_read_merkle_tree_page,
	.write_merkle_tree_block = shmem_write_merkle_tree_block,
};
