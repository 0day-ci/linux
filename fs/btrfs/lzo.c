// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/lzo.h>
#include <linux/refcount.h>
#include "compression.h"
#include "ctree.h"

#define LZO_LEN	4

/*
 * Btrfs LZO compression format
 *
 * Regular and inlined LZO compressed data extents consist of:
 *
 * 1.  Header
 *     Fixed size. LZO_LEN (4) bytes long, LE32.
 *     Records the total size (including the header) of compressed data.
 *
 * 2.  Segment(s)
 *     Variable size. Each segment includes one segment header, followed by data
 *     payload.
 *     One regular LZO compressed extent can have one or more segments.
 *     For inlined LZO compressed extent, only one segment is allowed.
 *     One segment represents at most one sector of uncompressed data.
 *
 * 2.1 Segment header
 *     Fixed size. LZO_LEN (4) bytes long, LE32.
 *     Records the total size of the segment (not including the header).
 *     Segment header never crosses sector boundary, thus it's possible to
 *     have at most 3 padding zeros at the end of the sector.
 *
 * 2.2 Data Payload
 *     Variable size. Size up limit should be lzo1x_worst_compress(sectorsize)
 *     which is 4419 for a 4KiB sectorsize.
 *
 * Example with 4K sectorsize:
 * Page 1:
 *          0     0x2   0x4   0x6   0x8   0xa   0xc   0xe     0x10
 * 0x0000   |  Header   | SegHdr 01 | Data payload 01 ...     |
 * ...
 * 0x0ff0   | SegHdr  N | Data payload  N     ...          |00|
 *                                                          ^^ padding zeros
 * Page 2:
 * 0x1000   | SegHdr N+1| Data payload N+1 ...                |
 */

struct workspace {
	void *mem;
	void *buf;	/* where decompressed data goes */
	void *cbuf;	/* where compressed data goes */
	struct list_head list;
};

static struct workspace_manager wsm;

void lzo_free_workspace(struct list_head *ws)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);

	kvfree(workspace->buf);
	kvfree(workspace->cbuf);
	kvfree(workspace->mem);
	kfree(workspace);
}

struct list_head *lzo_alloc_workspace(unsigned int level)
{
	struct workspace *workspace;

	workspace = kzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace)
		return ERR_PTR(-ENOMEM);

	workspace->mem = kvmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	workspace->buf = kvmalloc(lzo1x_worst_compress(PAGE_SIZE), GFP_KERNEL);
	workspace->cbuf = kvmalloc(lzo1x_worst_compress(PAGE_SIZE), GFP_KERNEL);
	if (!workspace->mem || !workspace->buf || !workspace->cbuf)
		goto fail;

	INIT_LIST_HEAD(&workspace->list);

	return &workspace->list;
fail:
	lzo_free_workspace(&workspace->list);
	return ERR_PTR(-ENOMEM);
}

static inline void write_compress_length(char *buf, size_t len)
{
	__le32 dlen;

	dlen = cpu_to_le32(len);
	memcpy(buf, &dlen, LZO_LEN);
}

static inline size_t read_compress_length(const char *buf)
{
	__le32 dlen;

	memcpy(&dlen, buf, LZO_LEN);
	return le32_to_cpu(dlen);
}

/*
 * Will do:
 *
 * - Write a segment header into the destination
 * - Copy the compressed buffer into the destination
 * - Make sure we have enough space in the last sector to fit a segment header
 *   If not, we will pad at most (LZO_LEN (4)) - 1 bytes of zeros.
 *
 * Will allocate new pages when needed.
 */
static int copy_compressed_data_to_page(char *compressed_data,
					size_t compressed_size,
					struct page **out_pages,
					u32 *cur_out,
					const u32 sectorsize)
{
	u32 sector_bytes_left;
	u32 orig_out;
	struct page *cur_page;

	/*
	 * We never allow a segment header crossing sector boundary, previous
	 * run should ensure we have enough space left inside the sector.
	 */
	ASSERT((*cur_out / sectorsize) ==
	       (*cur_out + LZO_LEN - 1) / sectorsize);

	cur_page = out_pages[*cur_out / PAGE_SIZE];
	/* Allocate a new page */
	if (!cur_page) {
		cur_page = alloc_page(GFP_NOFS);
		if (!cur_page)
			return -ENOMEM;
		out_pages[*cur_out / PAGE_SIZE] = cur_page;
	}

	write_compress_length(page_address(cur_page) + offset_in_page(*cur_out),
			      compressed_size);

	*cur_out += LZO_LEN;
	orig_out = *cur_out;
	/* *cur_out is increased, let the main loop to grab a proper page */
	cur_page = NULL;

	/* Copy compressed data */
	while (*cur_out - orig_out < compressed_size) {
		u32 copy_len = min_t(u32, sectorsize - *cur_out % sectorsize,
				     orig_out + compressed_size - *cur_out);

		/* Grab a page or allocate a new one */
		if (!cur_page) {
			cur_page = out_pages[*cur_out / PAGE_SIZE];
			if (!cur_page) {
				cur_page = alloc_page(GFP_NOFS);
				if (!cur_page)
					return -ENOMEM;
				out_pages[*cur_out / PAGE_SIZE] = cur_page;
			}
		}

		memcpy(page_address(cur_page) + offset_in_page(*cur_out),
		       compressed_data + *cur_out - orig_out, copy_len);

		*cur_out += copy_len;

		/* If we reached page boudnary, go to next page */
		if (IS_ALIGNED(*cur_out, PAGE_SIZE)) {
			/* Let next iteration to grab a page */
			cur_page = NULL;
		}
	}

	/*
	 * Check if we can fit the next segment header into the remaining space
	 * of the sector.
	 */
	sector_bytes_left = round_up(*cur_out, sectorsize) - *cur_out;
	if (sector_bytes_left >= LZO_LEN)
		return 0;

	/* The remaining size is not enough, pad it with zeros */
	memset(page_address(cur_page) + offset_in_page(*cur_out), 0,
	       sector_bytes_left);
	*cur_out += sector_bytes_left;
	return 0;
}

int lzo_compress_pages(struct list_head *ws, struct address_space *mapping,
		u64 start, struct page **pages, unsigned long *out_pages,
		unsigned long *total_in, unsigned long *total_out)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	const u32 sectorsize = btrfs_sb(mapping->host->i_sb)->sectorsize;
	struct page *page_in = NULL;
	int ret = 0;
	u64 cur_in = start;	/* Points to the file offset of input data */
	u32 cur_out = 0;	/* Points to the current output byte */
	u32 len = *total_out;

	*out_pages = 0;
	*total_out = 0;
	*total_in = 0;

	/*
	 * Skip the header for now, we will later come back and write the total
	 * compressed size
	 */
	cur_out += LZO_LEN;
	while (cur_in < start + len) {
		u32 sector_off = (cur_in - start) % sectorsize;
		u32 in_len;
		size_t out_len;

		/* Get the input page first */
		if (!page_in) {
			page_in = find_get_page(mapping, cur_in >> PAGE_SHIFT);
			ASSERT(page_in);
		}

		/* Compress at most one sector of data each time */
		in_len = min_t(u32, start + len - cur_in,
			       sectorsize - sector_off);
		ASSERT(in_len);
		ret = lzo1x_1_compress(page_address(page_in) +
				       offset_in_page(cur_in), in_len,
				       workspace->cbuf, &out_len,
				       workspace->mem);
		if (ret < 0) {
			pr_debug("BTRFS: lzo in loop returned %d\n", ret);
			ret = -EIO;
			goto out;
		}

		ret = copy_compressed_data_to_page(workspace->cbuf, out_len,
						   pages, &cur_out, sectorsize);
		if (ret < 0)
			goto out;

		cur_in += in_len;

		/*
		 * Check if we're making it bigger after two sectors.
		 * And if we're making it bigger, give up.
		 */
		if (cur_in - start > sectorsize * 2 &&
		    cur_in - start < cur_out) {
			ret = -E2BIG;
			goto out;
		}

		/* Check if we have reached page boundary */
		if (IS_ALIGNED(cur_in, PAGE_SIZE))
			page_in = NULL;
	}

	/* Store the size of all chunks of compressed data */
	write_compress_length(page_address(pages[0]), cur_out);

	ret = 0;
	*total_out = cur_out;
	*total_in = cur_in - start;
out:
	*out_pages = DIV_ROUND_UP(cur_out, PAGE_SIZE);
	return ret;
}

int lzo_decompress_bio(struct list_head *ws, struct compressed_bio *cb)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	int ret = 0, ret2;
	char *data_in;
	unsigned long page_in_index = 0;
	size_t srclen = cb->compressed_len;
	unsigned long total_pages_in = DIV_ROUND_UP(srclen, PAGE_SIZE);
	unsigned long buf_start;
	unsigned long buf_offset = 0;
	unsigned long bytes;
	unsigned long working_bytes;
	size_t in_len;
	size_t out_len;
	const size_t max_segment_len = lzo1x_worst_compress(PAGE_SIZE);
	unsigned long in_offset;
	unsigned long in_page_bytes_left;
	unsigned long tot_in;
	unsigned long tot_out;
	unsigned long tot_len;
	char *buf;
	bool may_late_unmap, need_unmap;
	struct page **pages_in = cb->compressed_pages;
	u64 disk_start = cb->start;
	struct bio *orig_bio = cb->orig_bio;

	data_in = kmap(pages_in[0]);
	tot_len = read_compress_length(data_in);
	/*
	 * Compressed data header check.
	 *
	 * The real compressed size can't exceed the maximum extent length, and
	 * all pages should be used (whole unused page with just the segment
	 * header is not possible).  If this happens it means the compressed
	 * extent is corrupted.
	 */
	if (tot_len > min_t(size_t, BTRFS_MAX_COMPRESSED, srclen) ||
	    tot_len < srclen - PAGE_SIZE) {
		ret = -EUCLEAN;
		goto done;
	}

	tot_in = LZO_LEN;
	in_offset = LZO_LEN;
	in_page_bytes_left = PAGE_SIZE - LZO_LEN;

	tot_out = 0;

	while (tot_in < tot_len) {
		in_len = read_compress_length(data_in + in_offset);
		in_page_bytes_left -= LZO_LEN;
		in_offset += LZO_LEN;
		tot_in += LZO_LEN;

		/*
		 * Segment header check.
		 *
		 * The segment length must not exceed the maximum LZO
		 * compression size, nor the total compressed size.
		 */
		if (in_len > max_segment_len || tot_in + in_len > tot_len) {
			ret = -EUCLEAN;
			goto done;
		}

		tot_in += in_len;
		working_bytes = in_len;
		may_late_unmap = need_unmap = false;

		/* fast path: avoid using the working buffer */
		if (in_page_bytes_left >= in_len) {
			buf = data_in + in_offset;
			bytes = in_len;
			may_late_unmap = true;
			goto cont;
		}

		/* copy bytes from the pages into the working buffer */
		buf = workspace->cbuf;
		buf_offset = 0;
		while (working_bytes) {
			bytes = min(working_bytes, in_page_bytes_left);

			memcpy(buf + buf_offset, data_in + in_offset, bytes);
			buf_offset += bytes;
cont:
			working_bytes -= bytes;
			in_page_bytes_left -= bytes;
			in_offset += bytes;

			/* check if we need to pick another page */
			if ((working_bytes == 0 && in_page_bytes_left < LZO_LEN)
			    || in_page_bytes_left == 0) {
				tot_in += in_page_bytes_left;

				if (working_bytes == 0 && tot_in >= tot_len)
					break;

				if (page_in_index + 1 >= total_pages_in) {
					ret = -EIO;
					goto done;
				}

				if (may_late_unmap)
					need_unmap = true;
				else
					kunmap(pages_in[page_in_index]);

				data_in = kmap(pages_in[++page_in_index]);

				in_page_bytes_left = PAGE_SIZE;
				in_offset = 0;
			}
		}

		out_len = max_segment_len;
		ret = lzo1x_decompress_safe(buf, in_len, workspace->buf,
					    &out_len);
		if (need_unmap)
			kunmap(pages_in[page_in_index - 1]);
		if (ret != LZO_E_OK) {
			pr_warn("BTRFS: decompress failed\n");
			ret = -EIO;
			break;
		}

		buf_start = tot_out;
		tot_out += out_len;

		ret2 = btrfs_decompress_buf2page(workspace->buf, buf_start,
						 tot_out, disk_start, orig_bio);
		if (ret2 == 0)
			break;
	}
done:
	kunmap(pages_in[page_in_index]);
	if (!ret)
		zero_fill_bio(orig_bio);
	return ret;
}

int lzo_decompress(struct list_head *ws, unsigned char *data_in,
		struct page *dest_page, unsigned long start_byte, size_t srclen,
		size_t destlen)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	size_t in_len;
	size_t out_len;
	size_t max_segment_len = lzo1x_worst_compress(PAGE_SIZE);
	int ret = 0;
	char *kaddr;
	unsigned long bytes;

	if (srclen < LZO_LEN || srclen > max_segment_len + LZO_LEN * 2)
		return -EUCLEAN;

	in_len = read_compress_length(data_in);
	if (in_len != srclen)
		return -EUCLEAN;
	data_in += LZO_LEN;

	in_len = read_compress_length(data_in);
	if (in_len != srclen - LZO_LEN * 2) {
		ret = -EUCLEAN;
		goto out;
	}
	data_in += LZO_LEN;

	out_len = PAGE_SIZE;
	ret = lzo1x_decompress_safe(data_in, in_len, workspace->buf, &out_len);
	if (ret != LZO_E_OK) {
		pr_warn("BTRFS: decompress failed!\n");
		ret = -EIO;
		goto out;
	}

	if (out_len < start_byte) {
		ret = -EIO;
		goto out;
	}

	/*
	 * the caller is already checking against PAGE_SIZE, but lets
	 * move this check closer to the memcpy/memset
	 */
	destlen = min_t(unsigned long, destlen, PAGE_SIZE);
	bytes = min_t(unsigned long, destlen, out_len - start_byte);

	kaddr = kmap_local_page(dest_page);
	memcpy(kaddr, workspace->buf + start_byte, bytes);

	/*
	 * btrfs_getblock is doing a zero on the tail of the page too,
	 * but this will cover anything missing from the decompressed
	 * data.
	 */
	if (bytes < destlen)
		memset(kaddr+bytes, 0, destlen-bytes);
	kunmap_local(kaddr);
out:
	return ret;
}

const struct btrfs_compress_op btrfs_lzo_compress = {
	.workspace_manager	= &wsm,
	.max_level		= 1,
	.default_level		= 1,
};
