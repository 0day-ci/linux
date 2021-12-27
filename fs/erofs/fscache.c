// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "internal.h"

static struct fscache_volume *volume;

static int erofs_begin_cache_operation(struct netfs_read_request *rreq)
{
	return fscache_begin_read_operation(&rreq->cache_resources,
					    rreq->netfs_priv);
}

static void erofs_priv_cleanup(struct address_space *mapping, void *netfs_priv)
{
}

static void erofs_issue_op(struct netfs_read_subrequest *subreq)
{
	/*
	 * TODO: implement demand-read logic later.
	 * We rely on user daemon to prepare blob files under corresponding
	 * directory, and we can reach here if blob files don't exist.
	 */

	netfs_subreq_terminated(subreq, -EOPNOTSUPP, false);
}

const struct netfs_read_request_ops erofs_req_ops = {
	.begin_cache_operation  = erofs_begin_cache_operation,
	.cleanup		= erofs_priv_cleanup,
	.issue_op		= erofs_issue_op,
};

struct page *erofs_readpage_from_fscache(struct erofs_cookie_ctx *ctx,
					 pgoff_t index)
{
	struct folio *folio;
	struct page *page;
	struct super_block *sb = ctx->inode->i_sb;
	int ret;

	page = find_or_create_page(ctx->inode->i_mapping, index, GFP_KERNEL);
	if (unlikely(!page)) {
		erofs_err(sb, "failed to allocate page");
		return ERR_PTR(-ENOMEM);
	}

	/* The content is already buffered in the address space */
	if (PageUptodate(page)) {
		unlock_page(page);
		return page;
	}

	/* Or a new page cache is created, then read the content from fscache */
	folio = page_folio(page);

	ret = netfs_readpage(NULL, folio, &erofs_req_ops, ctx->cookie);
	if (unlikely(ret || !PageUptodate(page))) {
		erofs_err(sb, "failed to read from fscache");
		return ERR_PTR(-EINVAL);
	}

	return page;
}

static inline void do_copy_page(struct page *from, struct page *to,
				size_t offset, size_t len)
{
	char *vfrom, *vto;

	vfrom = kmap_atomic(from);
	vto = kmap_atomic(to);
	memcpy(vto, vfrom + offset, len);
	kunmap_atomic(vto);
	kunmap_atomic(vfrom);
}

static int erofs_fscache_do_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct erofs_inode *vi = EROFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct erofs_map_blocks map;
	erofs_off_t o_la, pa;
	size_t offset, len;
	struct page *ipage;
	int ret;

	if (erofs_inode_is_data_compressed(vi->datalayout)) {
		erofs_info(sb, "compressed layout not supported yet");
		return -EOPNOTSUPP;
	}

	o_la = page_offset(page);
	map.m_la = o_la;

	ret = erofs_map_blocks(inode, &map, EROFS_GET_BLOCKS_RAW);
	if (ret)
		return ret;

	if (!(map.m_flags & EROFS_MAP_MAPPED)) {
		zero_user(page, 0, PAGE_SIZE);
		return 0;
	}

	/*
	 * 1) For FLAT_PLAIN/FLAT_INLINE layout, the output map.m_la shall be
	 * equal to o_la, and the output map.m_pa is exactly the physical
	 * address of o_la.
	 * 2) For CHUNK_BASED layout, the output map.m_la is rounded down to the
	 * nearest chunk boundary, and the output map.m_pa is actually the
	 * physical address of this chunk boundary. So we need to recalculate
	 * the actual physical address of o_la.
	 */
	pa = map.m_pa + o_la - map.m_la;

	ipage = erofs_get_meta_page(sb, erofs_blknr(pa));
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	/*
	 * @offset refers to the page offset inside @ipage.
	 * 1) Except for the inline layout, the offset shall all be 0, and @pa
	 * shall be aligned with EROFS_BLKSIZ in this case. Thus we can
	 * conveniently get the offset from @pa.
	 * 2) While for the inline layout, the offset may be non-zero. Since
	 * currently only flat layout supports inline, we can calculate the
	 * offset from the corresponding physical address.
	 */
	offset = erofs_blkoff(pa);
	len = min_t(u64, map.m_llen, PAGE_SIZE);

	do_copy_page(ipage, page, offset, len);

	unlock_page(ipage);
	return 0;
}

static int erofs_fscache_readpage(struct file *file, struct page *page)
{
	int ret;

	ret = erofs_fscache_do_readpage(file, page);
	if (ret)
		SetPageError(page);
	else
		SetPageUptodate(page);

	unlock_page(page);
	return ret;
}

const struct address_space_operations erofs_fscache_access_aops = {
	.readpage = erofs_fscache_readpage,
};

static int erofs_fscache_init_cookie(struct erofs_cookie_ctx *ctx, char *path)
{
	struct fscache_cookie *cookie;

	/*
	 * @object_size shall be non-zero to avoid
	 * FSCACHE_COOKIE_NO_DATA_TO_READ.
	 */
	cookie = fscache_acquire_cookie(volume, 0,
					path, strlen(path),
					NULL, 0, -1);
	if (!cookie)
		return -EINVAL;

	fscache_use_cookie(cookie, false);
	ctx->cookie = cookie;
	return 0;
}

static void erofs_fscache_cleanup_cookie(struct erofs_cookie_ctx *ctx)
{
	struct fscache_cookie *cookie = ctx->cookie;

	fscache_unuse_cookie(cookie, NULL, NULL);
	fscache_relinquish_cookie(cookie, false);
	ctx->cookie = NULL;
}

static const struct address_space_operations erofs_fscache_aops = {
};

static int erofs_fscache_get_inode(struct erofs_cookie_ctx *ctx,
				   struct super_block *sb)
{
	struct inode *const inode = new_inode(sb);

	if (!inode)
		return -ENOMEM;

	set_nlink(inode, 1);
	inode->i_size = OFFSET_MAX;

	inode->i_mapping->a_ops = &erofs_fscache_aops;
	mapping_set_gfp_mask(inode->i_mapping,
			GFP_NOFS | __GFP_HIGHMEM | __GFP_MOVABLE);
	ctx->inode = inode;
	return 0;
}

static void erofs_fscache_put_inode(struct erofs_cookie_ctx *ctx)
{
	iput(ctx->inode);
	ctx->inode = NULL;
}

static int erofs_fscahce_init_ctx(struct erofs_cookie_ctx *ctx,
				  struct super_block *sb, char *path)
{
	int ret;

	ret = erofs_fscache_init_cookie(ctx, path);
	if (ret) {
		erofs_err(sb, "failed to init cookie\n");
		return ret;
	}

	ret = erofs_fscache_get_inode(ctx, sb);
	if (ret) {
		erofs_err(sb, "failed to get anonymous inode\n");
		erofs_fscache_cleanup_cookie(ctx);
		return ret;
	}

	return 0;
}

static void erofs_fscache_cleanup_ctx(struct erofs_cookie_ctx *ctx)
{
	erofs_fscache_cleanup_cookie(ctx);
	erofs_fscache_put_inode(ctx);
}

struct erofs_cookie_ctx *erofs_fscache_get_ctx(struct super_block *sb,
					       char *path)
{
	struct erofs_cookie_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ret = erofs_fscahce_init_ctx(ctx, sb, path);
	if (ret) {
		kfree(ctx);
		return ERR_PTR(ret);
	}

	return ctx;
}

void erofs_fscache_put_ctx(struct erofs_cookie_ctx *ctx)
{
	if (!ctx)
		return;

	erofs_fscache_cleanup_ctx(ctx);
	kfree(ctx);
}

int __init erofs_fscache_init(void)
{
	volume = fscache_acquire_volume("erofs", NULL, NULL, 0);
	if (!volume)
		return -EINVAL;

	return 0;
}

void erofs_fscache_cleanup(void)
{
	fscache_relinquish_volume(volume, NULL, false);
}
