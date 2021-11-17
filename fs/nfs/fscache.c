// SPDX-License-Identifier: GPL-2.0-or-later
/* NFS filesystem cache interface
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/in6.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/iversion.h>

#include "internal.h"
#include "iostat.h"
#include "fscache.h"
#include "nfstrace.h"

#define NFSDBG_FACILITY                NFSDBG_FSCACHE

static struct rb_root nfs_fscache_keys = RB_ROOT;
static DEFINE_SPINLOCK(nfs_fscache_keys_lock);

/*
 * Layout of the key for an NFS server cache object.
 */
struct nfs_server_key {
	struct {
		uint16_t	nfsversion;		/* NFS protocol version */
		uint32_t	minorversion;		/* NFSv4 minor version */
		uint16_t	family;			/* address family */
		__be16		port;			/* IP port */
	} hdr;
	union {
		struct in_addr	ipv4_addr;	/* IPv4 address */
		struct in6_addr ipv6_addr;	/* IPv6 address */
	};
} __packed;

/*
 * Get the per-client index cookie for an NFS client if the appropriate mount
 * flag was set
 * - We always try and get an index cookie for the client, but get filehandle
 *   cookies on a per-superblock basis, depending on the mount flags
 */
void nfs_fscache_get_client_cookie(struct nfs_client *clp)
{
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &clp->cl_addr;
	const struct sockaddr_in *sin = (struct sockaddr_in *) &clp->cl_addr;
	struct nfs_server_key key;
	uint16_t len = sizeof(key.hdr);

	memset(&key, 0, sizeof(key));
	key.hdr.nfsversion = clp->rpc_ops->version;
	key.hdr.minorversion = clp->cl_minorversion;
	key.hdr.family = clp->cl_addr.ss_family;

	switch (clp->cl_addr.ss_family) {
	case AF_INET:
		key.hdr.port = sin->sin_port;
		key.ipv4_addr = sin->sin_addr;
		len += sizeof(key.ipv4_addr);
		break;

	case AF_INET6:
		key.hdr.port = sin6->sin6_port;
		key.ipv6_addr = sin6->sin6_addr;
		len += sizeof(key.ipv6_addr);
		break;

	default:
		printk(KERN_WARNING "NFS: Unknown network family '%d'\n",
		       clp->cl_addr.ss_family);
		clp->fscache = NULL;
		return;
	}

	/* create a cache index for looking up filehandles */
	clp->fscache = fscache_acquire_cookie(nfs_fscache_netfs.primary_index,
					      &nfs_fscache_server_index_def,
					      &key, len,
					      NULL, 0,
					      clp, 0, true);
}

/*
 * Dispose of a per-client cookie
 */
void nfs_fscache_release_client_cookie(struct nfs_client *clp)
{
	fscache_relinquish_cookie(clp->fscache, NULL, false);
	clp->fscache = NULL;
}

/*
 * Get the cache cookie for an NFS superblock.  We have to handle
 * uniquification here because the cache doesn't do it for us.
 *
 * The default uniquifier is just an empty string, but it may be overridden
 * either by the 'fsc=xxx' option to mount, or by inheriting it from the parent
 * superblock across an automount point of some nature.
 */
void nfs_fscache_get_super_cookie(struct super_block *sb, const char *uniq, int ulen)
{
	struct nfs_fscache_key *key, *xkey;
	struct nfs_server *nfss = NFS_SB(sb);
	struct rb_node **p, *parent;
	int diff;

	nfss->fscache_key = NULL;
	nfss->fscache = NULL;
	if (!uniq) {
		uniq = "";
		ulen = 1;
	}

	key = kzalloc(sizeof(*key) + ulen, GFP_KERNEL);
	if (!key)
		return;

	key->nfs_client = nfss->nfs_client;
	key->key.super.s_flags = sb->s_flags & NFS_SB_MASK;
	key->key.nfs_server.flags = nfss->flags;
	key->key.nfs_server.rsize = nfss->rsize;
	key->key.nfs_server.wsize = nfss->wsize;
	key->key.nfs_server.acregmin = nfss->acregmin;
	key->key.nfs_server.acregmax = nfss->acregmax;
	key->key.nfs_server.acdirmin = nfss->acdirmin;
	key->key.nfs_server.acdirmax = nfss->acdirmax;
	key->key.nfs_server.fsid = nfss->fsid;
	key->key.rpc_auth.au_flavor = nfss->client->cl_auth->au_flavor;

	key->key.uniq_len = ulen;
	memcpy(key->key.uniquifier, uniq, ulen);

	spin_lock(&nfs_fscache_keys_lock);
	p = &nfs_fscache_keys.rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;
		xkey = rb_entry(parent, struct nfs_fscache_key, node);

		if (key->nfs_client < xkey->nfs_client)
			goto go_left;
		if (key->nfs_client > xkey->nfs_client)
			goto go_right;

		diff = memcmp(&key->key, &xkey->key, sizeof(key->key));
		if (diff < 0)
			goto go_left;
		if (diff > 0)
			goto go_right;

		if (key->key.uniq_len == 0)
			goto non_unique;
		diff = memcmp(key->key.uniquifier,
			      xkey->key.uniquifier,
			      key->key.uniq_len);
		if (diff < 0)
			goto go_left;
		if (diff > 0)
			goto go_right;
		goto non_unique;

	go_left:
		p = &(*p)->rb_left;
		continue;
	go_right:
		p = &(*p)->rb_right;
	}

	rb_link_node(&key->node, parent, p);
	rb_insert_color(&key->node, &nfs_fscache_keys);
	spin_unlock(&nfs_fscache_keys_lock);
	nfss->fscache_key = key;

	/* create a cache index for looking up filehandles */
	nfss->fscache = fscache_acquire_cookie(nfss->nfs_client->fscache,
					       &nfs_fscache_super_index_def,
					       &key->key,
					       sizeof(key->key) + ulen,
					       NULL, 0,
					       nfss, 0, true);
	return;

non_unique:
	spin_unlock(&nfs_fscache_keys_lock);
	kfree(key);
	nfss->fscache_key = NULL;
	nfss->fscache = NULL;
	printk(KERN_WARNING "NFS:"
	       " Cache request denied due to non-unique superblock keys\n");
}

/*
 * release a per-superblock cookie
 */
void nfs_fscache_release_super_cookie(struct super_block *sb)
{
	struct nfs_server *nfss = NFS_SB(sb);

	fscache_relinquish_cookie(nfss->fscache, NULL, false);
	nfss->fscache = NULL;

	if (nfss->fscache_key) {
		spin_lock(&nfs_fscache_keys_lock);
		rb_erase(&nfss->fscache_key->node, &nfs_fscache_keys);
		spin_unlock(&nfs_fscache_keys_lock);
		kfree(nfss->fscache_key);
		nfss->fscache_key = NULL;
	}
}

static void nfs_fscache_update_auxdata(struct nfs_fscache_inode_auxdata *auxdata,
				  struct inode *inode)
{
	memset(auxdata, 0, sizeof(*auxdata));
	auxdata->mtime_sec  = inode->i_mtime.tv_sec;
	auxdata->mtime_nsec = inode->i_mtime.tv_nsec;
	auxdata->ctime_sec  = inode->i_ctime.tv_sec;
	auxdata->ctime_nsec = inode->i_ctime.tv_nsec;

	if (NFS_SERVER(inode)->nfs_client->rpc_ops->version == 4)
		auxdata->change_attr = inode_peek_iversion_raw(inode);
}

/*
 * Initialise the per-inode cache cookie pointer for an NFS inode.
 */
void nfs_fscache_init_inode(struct inode *inode)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);

	nfsi->fscache = NULL;
	if (!(nfss->fscache && S_ISREG(inode->i_mode)))
		return;

	nfs_fscache_update_auxdata(&auxdata, inode);

	nfsi->fscache = fscache_acquire_cookie(NFS_SB(inode->i_sb)->fscache,
					       &nfs_fscache_inode_object_def,
					       nfsi->fh.data, nfsi->fh.size,
					       &auxdata, sizeof(auxdata),
					       nfsi, i_size_read(inode), false);
}

/*
 * Release a per-inode cookie.
 */
void nfs_fscache_clear_inode(struct inode *inode)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct fscache_cookie *cookie = nfs_i_fscache(inode);

	nfs_fscache_update_auxdata(&auxdata, inode);
	fscache_relinquish_cookie(cookie, &auxdata, false);
	nfsi->fscache = NULL;
}

static bool nfs_fscache_can_enable(void *data)
{
	struct inode *inode = data;

	return !inode_is_open_for_write(inode);
}

/*
 * Enable or disable caching for a file that is being opened as appropriate.
 * The cookie is allocated when the inode is initialised, but is not enabled at
 * that time.  Enablement is deferred to file-open time to avoid stat() and
 * access() thrashing the cache.
 *
 * For now, with NFS, only regular files that are open read-only will be able
 * to use the cache.
 *
 * We enable the cache for an inode if we open it read-only and it isn't
 * currently open for writing.  We disable the cache if the inode is open
 * write-only.
 *
 * The caller uses the file struct to pin i_writecount on the inode before
 * calling us when a file is opened for writing, so we can make use of that.
 *
 * Note that this may be invoked multiple times in parallel by parallel
 * nfs_open() functions.
 */
void nfs_fscache_open_file(struct inode *inode, struct file *filp)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct fscache_cookie *cookie = nfs_i_fscache(inode);

	if (!fscache_cookie_valid(cookie))
		return;

	nfs_fscache_update_auxdata(&auxdata, inode);

	if (inode_is_open_for_write(inode)) {
		trace_nfs_fscache_disable_inode(inode);
		clear_bit(NFS_INO_FSCACHE, &nfsi->flags);
		fscache_disable_cookie(cookie, &auxdata, true);
		fscache_uncache_all_inode_pages(cookie, inode);
	} else {
		trace_nfs_fscache_enable_inode(inode);
		fscache_enable_cookie(cookie, &auxdata, i_size_read(inode),
				      nfs_fscache_can_enable, inode);
		if (fscache_cookie_enabled(cookie))
			set_bit(NFS_INO_FSCACHE, &NFS_I(inode)->flags);
	}
}
EXPORT_SYMBOL_GPL(nfs_fscache_open_file);

/*
 * Release the caching state associated with a page, if the page isn't busy
 * interacting with the cache.
 * - Returns true (can release page) or false (page busy).
 */
int nfs_fscache_release_page(struct page *page, gfp_t gfp)
{
	if (PageFsCache(page)) {
		struct fscache_cookie *cookie = nfs_i_fscache(page->mapping->host);

		BUG_ON(!cookie);
		dfprintk(FSCACHE, "NFS: fscache releasepage (0x%p/0x%p/0x%p)\n",
			 cookie, page, NFS_I(page->mapping->host));

		if (!fscache_maybe_release_page(cookie, page, gfp))
			return 0;

		nfs_inc_fscache_stats(page->mapping->host,
				      NFSIOS_FSCACHE_PAGES_UNCACHED);
	}

	return 1;
}

/*
 * Release the caching state associated with a page if undergoing complete page
 * invalidation.
 */
void __nfs_fscache_invalidate_page(struct page *page, struct inode *inode)
{
	struct fscache_cookie *cookie = nfs_i_fscache(inode);

	BUG_ON(!cookie);

	dfprintk(FSCACHE, "NFS: fscache invalidatepage (0x%p/0x%p/0x%p)\n",
		 cookie, page, NFS_I(inode));

	fscache_wait_on_page_write(cookie, page);

	BUG_ON(!PageLocked(page));
	fscache_uncache_page(cookie, page);
	nfs_inc_fscache_stats(page->mapping->host,
			      NFSIOS_FSCACHE_PAGES_UNCACHED);
}

/*
 * Handle completion of a page being read from the cache.
 * - Called in process (keventd) context.
 */
static void nfs_fscache_read_page_complete(struct page *page,
					       void *context,
					       int error)
{
	trace_nfs_fscache_read_page_complete(page->mapping->host, page, 1, error);

	/*
	 * If the read completes with an error, mark the page with PG_checked,
	 * unlock the page, and let the VM reissue the readpage.
	 */
	if (!error)
		SetPageUptodate(page);
	else
		SetPageChecked(page);
	unlock_page(page);
}

/*
 * Retrieve a page from fscache
 */
int __nfs_fscache_read_page(struct nfs_open_context *ctx,
				struct inode *inode, struct page *page)
{
	int ret;

	trace_nfs_fscache_read_pages(inode, page, 1);
	if (PageChecked(page)) {
		ClearPageChecked(page);
		ret = 1;
		goto out;
	}

	ret = fscache_read_or_alloc_page(nfs_i_fscache(inode),
					 page,
					 nfs_fscache_read_page_complete,
					 ctx,
					 GFP_KERNEL);

	switch (ret) {
	case 0: /* read BIO submitted (page in fscache) */
		nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_READ_OK);
		break;

	case -ENOBUFS: /* inode not in cache */
	case -ENODATA: /* page not in cache */
		nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_READ_FAIL);
		ret = 1;
		break;

	default:
		nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_READ_FAIL);
	}

out:
	trace_nfs_fscache_read_pages_exit(inode, page, 1, ret);
	return ret;
}

/*
 * Retrieve a set of pages from fscache
 */
int __nfs_fscache_read_pages(struct nfs_open_context *ctx,
				 struct inode *inode,
				 struct address_space *mapping,
				 struct list_head *pages,
				 unsigned *nr_pages)
{
	unsigned npages = *nr_pages;
	int ret;

	trace_nfs_fscache_read_pages(inode, lru_to_page(pages), *nr_pages);

	ret = fscache_read_or_alloc_pages(nfs_i_fscache(inode),
					  mapping, pages, nr_pages,
					  nfs_fscache_read_page_complete,
					  ctx,
					  mapping_gfp_mask(mapping));
	if (*nr_pages < npages)
		nfs_add_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_READ_OK,
				      npages);
	if (*nr_pages > 0)
		nfs_add_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_READ_FAIL,
				      *nr_pages);

	switch (ret) {
	case 0: /* read submitted to the cache for all pages */
		BUG_ON(!list_empty(pages));
		BUG_ON(*nr_pages != 0);
		break;

	case -ENOBUFS: /* some pages aren't cached and can't be */
	case -ENODATA: /* some pages aren't cached */
		ret = 1;
		break;

	default:
		;
	}

	trace_nfs_fscache_read_pages_exit(inode, lru_to_page(pages), *nr_pages, ret);
	return ret;
}

/*
 * Store a newly fetched page in fscache
 * - PG_fscache must be set on the page
 */
void __nfs_fscache_write_page(struct inode *inode, struct page *page, int sync)
{
	int ret;

	trace_nfs_fscache_write_page(inode, page, 1);

	ret = fscache_write_page(nfs_i_fscache(inode), page,
				 inode->i_size, GFP_KERNEL);
	if (ret != 0) {
		fscache_uncache_page(nfs_i_fscache(inode), page);
		nfs_inc_fscache_stats(inode,
				      NFSIOS_FSCACHE_PAGES_WRITTEN_FAIL);
		nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_UNCACHED);
	} else {
		nfs_inc_fscache_stats(inode,
				      NFSIOS_FSCACHE_PAGES_WRITTEN_OK);
	}
	trace_nfs_fscache_write_page_exit(inode, page, 1, ret);
}
