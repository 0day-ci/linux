// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Facebook.  All rights reserved.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include <linux/posix_acl_xattr.h>
#include <linux/iversion.h>
#include <linux/fsverity.h>
#include <linux/sched/mm.h>
#include "ctree.h"
#include "btrfs_inode.h"
#include "transaction.h"
#include "disk-io.h"
#include "locking.h"

/*
 * Just like ext4, we cache the merkle tree in pages after EOF in the page
 * cache.  Unlike ext4, we're storing these in dedicated btree items and
 * not just shoving them after EOF in the file.  This means we'll need to
 * do extra work to encrypt them once encryption is supported in btrfs,
 * but btrfs has a lot of careful code around i_size and it seems better
 * to make a new key type than try and adjust all of our expectations
 * for i_size.
 *
 * fs verity items are stored under two different key types on disk.
 *
 * The descriptor items:
 * [ inode objectid, BTRFS_VERITY_DESC_ITEM_KEY, offset ]
 *
 * At offset 0, we store a btrfs_verity_descriptor_item which tracks the
 * size of the descriptor item and some extra data for encryption.
 * Starting at offset 1, these hold the generic fs verity descriptor.
 * These are opaque to btrfs, we just read and write them as a blob for
 * the higher level verity code.  The most common size for this is 256 bytes.
 *
 * The merkle tree items:
 * [ inode objectid, BTRFS_VERITY_MERKLE_ITEM_KEY, offset ]
 *
 * These also start at offset 0, and correspond to the merkle tree bytes.
 * So when fsverity asks for page 0 of the merkle tree, we pull up one page
 * starting at offset 0 for this key type.  These are also opaque to btrfs,
 * we're blindly storing whatever fsverity sends down.
 *
 * This file is just reading and writing the various items whenever
 * fsverity needs us to.
 */

/*
 * Helper function for computing cache index for Merkle tree pages
 * @inode: verity file whose Merkle items we want.
 * @merkle_index: index of the page in the Merkle tree (as in
 *                read_merkle_tree_page).
 * @ret_index: returned index in the inode's mapping
 *
 * Returns: 0 on success, -EFBIG if the location in the file would be beyond
 * sb->s_maxbytes.
 */
static int get_verity_mapping_index(struct inode *inode,
				    pgoff_t merkle_index,
				    pgoff_t *ret_index)
{
	/*
	 * the file is readonly, so i_size can't change here.  We jump
	 * some pages past the last page to cache our merkles.  The goal
	 * is just to jump past any hugepages that might be mapped in.
	 */
	pgoff_t merkle_offset = 2048;
	u64 index = (i_size_read(inode) >> PAGE_SHIFT) + merkle_offset + merkle_index;

	if (index > inode->i_sb->s_maxbytes >> PAGE_SHIFT)
		return -EFBIG;

	*ret_index = index;
	return 0;
}


/*
 * Drop all the items for this inode with this key_type.
 * @inode: The inode to drop items for
 * @key_type: The type of items to drop (VERITY_DESC_ITEM or
 *            VERITY_MERKLE_ITEM)
 *
 * Before doing a verity enable we cleanup any existing verity items.
 *
 * This is also used to clean up if a verity enable failed half way
 * through.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int drop_verity_items(struct btrfs_inode *inode, u8 key_type)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = inode->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}

		/*
		 * walk backwards through all the items until we find one
		 * that isn't from our key type or objectid
		 */
		key.objectid = btrfs_ino(inode);
		key.offset = (u64)-1;
		key.type = key_type;

		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0) {
			ret = 0;
			/* no more keys of this type, we're done */
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		} else if (ret < 0) {
			break;
		}

		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		/* no more keys of this type, we're done */
		if (key.objectid != btrfs_ino(inode) || key.type != key_type)
			break;

		/*
		 * this shouldn't be a performance sensitive function because
		 * it's not used as part of truncate.  If it ever becomes
		 * perf sensitive, change this to walk forward and bulk delete
		 * items
		 */
		ret = btrfs_del_items(trans, root, path,
				      path->slots[0], 1);
		btrfs_release_path(path);
		btrfs_end_transaction(trans);

		if (ret)
			goto out;
	}

	btrfs_end_transaction(trans);
out:
	btrfs_free_path(path);
	return ret;

}

/*
 * Insert and write inode items with a given key type and offset.
 * @inode: The inode to insert for.
 * @key_type: The key type to insert.
 * @offset: The item offset to insert at.
 * @src: Source data to write.
 * @len: Length of source data to write.
 *
 * Write len bytes from src into items of up to 1k length.
 * The inserted items will have key <ino, key_type, offset + off> where
 * off is consecutively increasing from 0 up to the last item ending at
 * offset + len.
 *
 * Returns 0 on success and a negative error code on failure.
 */
static int write_key_bytes(struct btrfs_inode *inode, u8 key_type, u64 offset,
			   const char *src, u64 len)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_root *root = inode->root;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 orig_len = len;
	u64 copied = 0;
	unsigned long copy_bytes;
	unsigned long src_offset = 0;
	void *data;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (len > 0) {
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}

		key.objectid = btrfs_ino(inode);
		key.offset = offset;
		key.type = key_type;

		/*
		 * insert 1K at a time mostly to be friendly for smaller
		 * leaf size filesystems
		 */
		copy_bytes = min_t(u64, len, 1024);

		ret = btrfs_insert_empty_item(trans, root, path, &key, copy_bytes);
		if (ret) {
			btrfs_end_transaction(trans);
			break;
		}

		leaf = path->nodes[0];

		data = btrfs_item_ptr(leaf, path->slots[0], void);
		write_extent_buffer(leaf, src + src_offset,
				    (unsigned long)data, copy_bytes);
		offset += copy_bytes;
		src_offset += copy_bytes;
		len -= copy_bytes;
		copied += copy_bytes;

		btrfs_release_path(path);
		btrfs_end_transaction(trans);
	}

	btrfs_free_path(path);

	if (!ret && copied != orig_len)
		ret = -EIO;
	return ret;
}

/*
 * Read inode items of the given key type and offset from the btree.
 * @inode: The inode to read items of.
 * @key_type: The key type to read.
 * @offset: The item offset to read from.
 * @dest: The buffer to read into. This parameter has slightly tricky
 *        semantics.  If it is NULL, the function will not do any copying
 *        and will just return the size of all the items up to len bytes.
 *        If dest_page is passed, then the function will kmap_atomic the
 *        page and ignore dest, but it must still be non-NULL to avoid the
 *        counting-only behavior.
 * @len: Length in bytes to read.
 * @dest_page: Copy into this page instead of the dest buffer.
 *
 * Helper function to read items from the btree.  This returns the number
 * of bytes read or < 0 for errors.  We can return short reads if the
 * items don't exist on disk or aren't big enough to fill the desired length.
 *
 * Supports reading into a provided buffer (dest) or into the page cache
 *
 * Returns number of bytes read or a negative error code on failure.
 */
static ssize_t read_key_bytes(struct btrfs_inode *inode, u8 key_type, u64 offset,
			  char *dest, u64 len, struct page *dest_page)
{
	struct btrfs_path *path;
	struct btrfs_root *root = inode->root;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 item_end;
	u64 copy_end;
	u64 copied = 0;
	u32 copy_offset;
	unsigned long copy_bytes;
	unsigned long dest_offset = 0;
	void *data;
	char *kaddr = dest;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (dest_page)
		path->reada = READA_FORWARD;

	key.objectid = btrfs_ino(inode);
	key.offset = offset;
	key.type = key_type;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		ret = 0;
		if (path->slots[0] == 0)
			goto out;
		path->slots[0]--;
	}

	while (len > 0) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

		if (key.objectid != btrfs_ino(inode) ||
		    key.type != key_type)
			break;

		item_end = btrfs_item_size_nr(leaf, path->slots[0]) + key.offset;

		if (copied > 0) {
			/*
			 * once we've copied something, we want all of the items
			 * to be sequential
			 */
			if (key.offset != offset)
				break;
		} else {
			/*
			 * our initial offset might be in the middle of an
			 * item.  Make sure it all makes sense
			 */
			if (key.offset > offset)
				break;
			if (item_end <= offset)
				break;
		}

		/* desc = NULL to just sum all the item lengths */
		if (!dest)
			copy_end = item_end;
		else
			copy_end = min(offset + len, item_end);

		/* number of bytes in this item we want to copy */
		copy_bytes = copy_end - offset;

		/* offset from the start of item for copying */
		copy_offset = offset - key.offset;

		if (dest) {
			if (dest_page)
				kaddr = kmap_atomic(dest_page);

			data = btrfs_item_ptr(leaf, path->slots[0], void);
			read_extent_buffer(leaf, kaddr + dest_offset,
					   (unsigned long)data + copy_offset,
					   copy_bytes);

			if (dest_page)
				kunmap_atomic(kaddr);
		}

		offset += copy_bytes;
		dest_offset += copy_bytes;
		len -= copy_bytes;
		copied += copy_bytes;

		path->slots[0]++;
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			/*
			 * we've reached the last slot in this leaf and we need
			 * to go to the next leaf.
			 */
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				break;
			} else if (ret > 0) {
				ret = 0;
				break;
			}
		}
	}
out:
	btrfs_free_path(path);
	if (!ret)
		ret = copied;
	return ret;
}

/*
 * fsverity op that begins enabling verity.
 * fsverity calls this to ask us to setup the inode for enabling.  We
 * drop any existing verity items and set the in progress bit.
 */
static int btrfs_begin_enable_verity(struct file *filp)
{
	struct inode *inode = file_inode(filp);
	int ret;

	if (test_bit(BTRFS_INODE_VERITY_IN_PROGRESS, &BTRFS_I(inode)->runtime_flags))
		return -EBUSY;

	/*
	 * ext4 adds the inode to the orphan list here, presumably because the
	 * truncate done at orphan processing time will delete partial
	 * measurements.  TODO: setup orphans
	 */
	set_bit(BTRFS_INODE_VERITY_IN_PROGRESS, &BTRFS_I(inode)->runtime_flags);
	ret = drop_verity_items(BTRFS_I(inode), BTRFS_VERITY_DESC_ITEM_KEY);
	if (ret)
		goto err;

	ret = drop_verity_items(BTRFS_I(inode), BTRFS_VERITY_MERKLE_ITEM_KEY);
	if (ret)
		goto err;

	return 0;

err:
	clear_bit(BTRFS_INODE_VERITY_IN_PROGRESS, &BTRFS_I(inode)->runtime_flags);
	return ret;

}

/*
 * fsverity op that ends enabling verity.
 * fsverity calls this when it's done with all of the pages in the file
 * and all of the merkle items have been inserted.  We write the
 * descriptor and update the inode in the btree to reflect its new life
 * as a verity file.
 */
static int btrfs_end_enable_verity(struct file *filp, const void *desc,
				  size_t desc_size, u64 merkle_tree_size)
{
	struct btrfs_trans_handle *trans;
	struct inode *inode = file_inode(filp);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_verity_descriptor_item item;
	int ret;

	if (desc != NULL) {
		/* write out the descriptor item */
		memset(&item, 0, sizeof(item));
		btrfs_set_stack_verity_descriptor_size(&item, desc_size);
		ret = write_key_bytes(BTRFS_I(inode),
				      BTRFS_VERITY_DESC_ITEM_KEY, 0,
				      (const char *)&item, sizeof(item));
		if (ret)
			goto out;
		/* write out the descriptor itself */
		ret = write_key_bytes(BTRFS_I(inode),
				      BTRFS_VERITY_DESC_ITEM_KEY, 1,
				      desc, desc_size);
		if (ret)
			goto out;

		/* update our inode flags to include fs verity */
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		BTRFS_I(inode)->compat_flags |= BTRFS_INODE_VERITY;
		btrfs_sync_inode_flags_to_i_flags(inode);
		ret = btrfs_update_inode(trans, root, BTRFS_I(inode));
		btrfs_end_transaction(trans);
	}

out:
	if (desc == NULL || ret) {
		/* If we failed, drop all the verity items */
		drop_verity_items(BTRFS_I(inode), BTRFS_VERITY_DESC_ITEM_KEY);
		drop_verity_items(BTRFS_I(inode), BTRFS_VERITY_MERKLE_ITEM_KEY);
	} else
		btrfs_set_fs_compat_ro(root->fs_info, VERITY);
	clear_bit(BTRFS_INODE_VERITY_IN_PROGRESS, &BTRFS_I(inode)->runtime_flags);
	return ret;
}

/*
 * fsverity op that gets the struct fsverity_descriptor.
 * fsverity does a two pass setup for reading the descriptor, in the first pass
 * it calls with buf_size = 0 to query the size of the descriptor,
 * and then in the second pass it actually reads the descriptor off
 * disk.
 */
static int btrfs_get_verity_descriptor(struct inode *inode, void *buf,
				       size_t buf_size)
{
	size_t true_size;
	ssize_t ret = 0;
	struct btrfs_verity_descriptor_item item;

	memset(&item, 0, sizeof(item));
	ret = read_key_bytes(BTRFS_I(inode), BTRFS_VERITY_DESC_ITEM_KEY,
			     0, (char *)&item, sizeof(item), NULL);
	if (ret < 0)
		return ret;

	true_size = btrfs_stack_verity_descriptor_size(&item);
	if (true_size > INT_MAX)
		return -EUCLEAN;
	if (!buf_size)
		return true_size;
	if (buf_size < true_size)
		return -ERANGE;

	ret = read_key_bytes(BTRFS_I(inode),
			     BTRFS_VERITY_DESC_ITEM_KEY, 1,
			     buf, buf_size, NULL);
	if (ret < 0)
		return ret;
	if (ret != true_size)
		return -EIO;

	return true_size;
}

/*
 * fsverity op that reads and caches a merkle tree page.  These are stored
 * in the btree, but we cache them in the inode's address space after EOF.
 */
static struct page *btrfs_read_merkle_tree_page(struct inode *inode,
					       pgoff_t index,
					       unsigned long num_ra_pages)
{
	struct page *p;
	u64 start = index << PAGE_SHIFT;
	pgoff_t mapping_index;
	ssize_t ret;
	int err;

	err = get_verity_mapping_index(inode, index, &mapping_index);
	if (err < 0)
		return ERR_PTR(err);
again:
	p = find_get_page_flags(inode->i_mapping, mapping_index, FGP_ACCESSED);
	if (p) {
		if (PageUptodate(p))
			return p;

		lock_page(p);
		/*
		 * we only insert uptodate pages, so !Uptodate has to be
		 * an error
		 */
		if (!PageUptodate(p)) {
			unlock_page(p);
			put_page(p);
			return ERR_PTR(-EIO);
		}
		unlock_page(p);
		return p;
	}

	p = page_cache_alloc(inode->i_mapping);
	if (!p)
		return ERR_PTR(-ENOMEM);

	/*
	 * merkle item keys are indexed from byte 0 in the merkle tree.
	 * they have the form:
	 *
	 * [ inode objectid, BTRFS_MERKLE_ITEM_KEY, offset in bytes ]
	 */
	ret = read_key_bytes(BTRFS_I(inode),
			     BTRFS_VERITY_MERKLE_ITEM_KEY, start,
			     page_address(p), PAGE_SIZE, p);
	if (ret < 0) {
		put_page(p);
		return ERR_PTR(ret);
	}

	/* zero fill any bytes we didn't write into the page */
	if (ret < PAGE_SIZE) {
		char *kaddr = kmap_atomic(p);

		memset(kaddr + ret, 0, PAGE_SIZE - ret);
		kunmap_atomic(kaddr);
	}
	SetPageUptodate(p);
	err = add_to_page_cache_lru(p, inode->i_mapping, mapping_index,
				    mapping_gfp_mask(inode->i_mapping));

	if (!err) {
		/* inserted and ready for fsverity */
		unlock_page(p);
	} else {
		put_page(p);
		/* did someone race us into inserting this page? */
		if (err == -EEXIST)
			goto again;
		p = ERR_PTR(err);
	}
	return p;
}

/*
 * fsverity op that writes a merkle tree block into the btree in 1k chunks.
 */
static int btrfs_write_merkle_tree_block(struct inode *inode, const void *buf,
					u64 index, int log_blocksize)
{
	u64 start = index << log_blocksize;
	u64 len = 1 << log_blocksize;
	int ret;
	pgoff_t mapping_index;

	ret = get_verity_mapping_index(inode, index, &mapping_index);
	if (ret < 0)
		return ret;

	return write_key_bytes(BTRFS_I(inode), BTRFS_VERITY_MERKLE_ITEM_KEY,
			       start, buf, len);
}

const struct fsverity_operations btrfs_verityops = {
	.begin_enable_verity	= btrfs_begin_enable_verity,
	.end_enable_verity	= btrfs_end_enable_verity,
	.get_verity_descriptor	= btrfs_get_verity_descriptor,
	.read_merkle_tree_page	= btrfs_read_merkle_tree_page,
	.write_merkle_tree_block = btrfs_write_merkle_tree_block,
};
