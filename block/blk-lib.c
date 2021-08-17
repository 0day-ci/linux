// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to generic helpers functions
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>

#include "blk.h"

struct bio *blk_next_bio(struct bio *bio, unsigned int nr_pages, gfp_t gfp)
{
	struct bio *new = bio_alloc(gfp, nr_pages);

	if (bio) {
		bio_chain(bio, new);
		submit_bio(bio);
	}

	return new;
}
EXPORT_SYMBOL_GPL(blk_next_bio);

int __blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, int flags,
		struct bio **biop)
{
	struct request_queue *q = bdev_get_queue(bdev);
	struct bio *bio = *biop;
	unsigned int op;
	sector_t bs_mask, part_offset = 0;

	if (!q)
		return -ENXIO;

	if (bdev_read_only(bdev))
		return -EPERM;

	if (flags & BLKDEV_DISCARD_SECURE) {
		if (!blk_queue_secure_erase(q))
			return -EOPNOTSUPP;
		op = REQ_OP_SECURE_ERASE;
	} else {
		if (!blk_queue_discard(q))
			return -EOPNOTSUPP;
		op = REQ_OP_DISCARD;
	}

	/* In case the discard granularity isn't set by buggy device driver */
	if (WARN_ON_ONCE(!q->limits.discard_granularity)) {
		char dev_name[BDEVNAME_SIZE];

		bdevname(bdev, dev_name);
		pr_err_ratelimited("%s: Error: discard_granularity is 0.\n", dev_name);
		return -EOPNOTSUPP;
	}

	bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;

	if (!nr_sects)
		return -EINVAL;

	/* In case the discard request is in a partition */
	if (bdev_is_partition(bdev))
		part_offset = bdev->bd_start_sect;

	while (nr_sects) {
		sector_t granularity_aligned_lba, req_sects;
		sector_t sector_mapped = sector + part_offset;

		granularity_aligned_lba = round_up(sector_mapped,
				q->limits.discard_granularity >> SECTOR_SHIFT);

		/*
		 * Check whether the discard bio starts at a discard_granularity
		 * aligned LBA,
		 * - If no: set (granularity_aligned_lba - sector_mapped) to
		 *   bi_size of the first split bio, then the second bio will
		 *   start at a discard_granularity aligned LBA on the device.
		 * - If yes: use bio_aligned_discard_max_sectors() as the max
		 *   possible bi_size of the first split bio. Then when this bio
		 *   is split in device drive, the split ones are very probably
		 *   to be aligned to discard_granularity of the device's queue.
		 */
		if (granularity_aligned_lba == sector_mapped)
			req_sects = min_t(sector_t, nr_sects,
					  bio_aligned_discard_max_sectors(q));
		else
			req_sects = min_t(sector_t, nr_sects,
					  granularity_aligned_lba - sector_mapped);

		WARN_ON_ONCE((req_sects << 9) > UINT_MAX);

		bio = blk_next_bio(bio, 0, gfp_mask);
		bio->bi_iter.bi_sector = sector;
		bio_set_dev(bio, bdev);
		bio_set_op_attrs(bio, op, 0);

		bio->bi_iter.bi_size = req_sects << 9;
		sector += req_sects;
		nr_sects -= req_sects;

		/*
		 * We can loop for a long time in here, if someone does
		 * full device discards (like mkfs). Be nice and allow
		 * us to schedule out to avoid softlocking if preempt
		 * is disabled.
		 */
		cond_resched();
	}

	*biop = bio;
	return 0;
}
EXPORT_SYMBOL(__blkdev_issue_discard);

/**
 * blkdev_issue_discard - queue a discard
 * @bdev:	blockdev to issue discard for
 * @sector:	start sector
 * @nr_sects:	number of sectors to discard
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @flags:	BLKDEV_DISCARD_* flags to control behaviour
 *
 * Description:
 *    Issue a discard request for the sectors in question.
 */
int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, unsigned long flags)
{
	struct bio *bio = NULL;
	struct blk_plug plug;
	int ret;

	blk_start_plug(&plug);
	ret = __blkdev_issue_discard(bdev, sector, nr_sects, gfp_mask, flags,
			&bio);
	if (!ret && bio) {
		ret = submit_bio_wait(bio);
		if (ret == -EOPNOTSUPP)
			ret = 0;
		bio_put(bio);
	}
	blk_finish_plug(&plug);

	return ret;
}
EXPORT_SYMBOL(blkdev_issue_discard);

/*
 * Wait on and process all in-flight BIOs.  This must only be called once
 * all bios have been issued so that the refcount can only decrease.
 * This just waits for all bios to make it through cio_bio_end_io.  IO
 * errors are propagated through cio->io_error.
 */
static int cio_await_completion(struct cio *cio)
{
	int ret = 0;

	while (atomic_read(&cio->refcount)) {
		cio->waiter = current;
		__set_current_state(TASK_UNINTERRUPTIBLE);
		blk_io_schedule();
		/* wake up sets us TASK_RUNNING */
		cio->waiter = NULL;
		ret = cio->io_err;
	}
	kvfree(cio);

	return ret;
}

/*
 * The BIO completion handler simply decrements refcount.
 * Also wake up process, if this is the last bio to be completed.
 *
 * During I/O bi_private points at the cio.
 */
static void cio_bio_end_io(struct bio *bio)
{
	struct cio *cio = bio->bi_private;

	if (bio->bi_status)
		cio->io_err = bio->bi_status;
	kvfree(page_address(bio_first_bvec_all(bio)->bv_page) +
			bio_first_bvec_all(bio)->bv_offset);
	bio_put(bio);

	if (atomic_dec_and_test(&cio->refcount) && cio->waiter)
		wake_up_process(cio->waiter);
}

int blk_copy_offload_submit_bio(struct block_device *bdev,
		struct blk_copy_payload *payload, int payload_size,
		struct cio *cio, gfp_t gfp_mask)
{
	struct request_queue *q = bdev_get_queue(bdev);
	struct bio *bio;

	bio = bio_map_kern(q, payload, payload_size, gfp_mask);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio_set_dev(bio, bdev);
	bio->bi_opf = REQ_OP_COPY | REQ_NOMERGE;
	bio->bi_iter.bi_sector = payload->dest;
	bio->bi_end_io = cio_bio_end_io;
	bio->bi_private = cio;
	atomic_inc(&cio->refcount);
	submit_bio(bio);

	return 0;
}

/* Go through all the enrties inside user provided payload, and determine the
 * maximum number of entries in a payload, based on device's scc-limits.
 */
static inline int blk_max_payload_entries(int nr_srcs, struct range_entry *rlist,
		int max_nr_srcs, sector_t max_copy_range_sectors, sector_t max_copy_len)
{
	sector_t range_len, copy_len = 0, remaining = 0;
	int ri = 0, pi = 1, max_pi = 0;

	for (ri = 0; ri < nr_srcs; ri++) {
		for (remaining = rlist[ri].len; remaining > 0; remaining -= range_len) {
			range_len = min3(remaining, max_copy_range_sectors,
								max_copy_len - copy_len);
			pi++;
			copy_len += range_len;

			if ((pi == max_nr_srcs) || (copy_len == max_copy_len)) {
				max_pi = max(max_pi, pi);
				pi = 1;
				copy_len = 0;
			}
		}
	}

	return max(max_pi, pi);
}

/*
 * blk_copy_offload_scc	- Use device's native copy offload feature
 * Go through user provide payload, prepare new payload based on device's copy offload limits.
 */
int blk_copy_offload_scc(struct block_device *src_bdev, int nr_srcs,
		struct range_entry *rlist, struct block_device *dest_bdev,
		sector_t dest, gfp_t gfp_mask)
{
	struct request_queue *q = bdev_get_queue(dest_bdev);
	struct cio *cio = NULL;
	struct blk_copy_payload *payload;
	sector_t range_len, copy_len = 0, remaining = 0;
	sector_t src_blk, cdest = dest;
	sector_t max_copy_range_sectors, max_copy_len;
	int ri = 0, pi = 0, ret = 0, payload_size, max_pi, max_nr_srcs;

	cio = kzalloc(sizeof(struct cio), GFP_KERNEL);
	if (!cio)
		return -ENOMEM;
	atomic_set(&cio->refcount, 0);

	max_nr_srcs = q->limits.max_copy_nr_ranges;
	max_copy_range_sectors = q->limits.max_copy_range_sectors;
	max_copy_len = q->limits.max_copy_sectors;

	max_pi = blk_max_payload_entries(nr_srcs, rlist, max_nr_srcs,
					max_copy_range_sectors, max_copy_len);
	payload_size = struct_size(payload, range, max_pi);

	payload = kvmalloc(payload_size, gfp_mask);
	if (!payload) {
		ret = -ENOMEM;
		goto free_cio;
	}
	payload->src_bdev = src_bdev;

	for (ri = 0; ri < nr_srcs; ri++) {
		for (remaining = rlist[ri].len, src_blk = rlist[ri].src; remaining > 0;
						remaining -= range_len, src_blk += range_len) {

			range_len = min3(remaining, max_copy_range_sectors,
								max_copy_len - copy_len);
			payload->range[pi].len = range_len;
			payload->range[pi].src = src_blk;
			pi++;
			copy_len += range_len;

			/* Submit current payload, if crossing device copy limits */
			if ((pi == max_nr_srcs) || (copy_len == max_copy_len)) {
				payload->dest = cdest;
				payload->copy_nr_ranges = pi;
				ret = blk_copy_offload_submit_bio(dest_bdev, payload,
								payload_size, cio, gfp_mask);
				if (ret)
					goto free_payload;

				/* reset index, length and allocate new payload */
				pi = 0;
				cdest += copy_len;
				copy_len = 0;
				payload = kvmalloc(payload_size, gfp_mask);
				if (!payload) {
					ret = -ENOMEM;
					goto free_cio;
				}
				payload->src_bdev = src_bdev;
			}
		}
	}

	if (pi) {
		payload->dest = cdest;
		payload->copy_nr_ranges = pi;
		ret = blk_copy_offload_submit_bio(dest_bdev, payload, payload_size, cio, gfp_mask);
		if (ret)
			goto free_payload;
	}

	/* Wait for completion of all IO's*/
	ret = cio_await_completion(cio);

	return ret;

free_payload:
	kvfree(payload);
free_cio:
	cio_await_completion(cio);
	return ret;
}

static inline sector_t blk_copy_len(struct range_entry *rlist, int nr_srcs)
{
	int i;
	sector_t len = 0;

	for (i = 0; i < nr_srcs; i++) {
		if (rlist[i].len)
			len += rlist[i].len;
		else
			return 0;
	}

	return len;
}

static inline bool blk_check_offload_scc(struct request_queue *src_q,
		struct request_queue *dest_q)
{
	if (src_q == dest_q && src_q->limits.copy_offload == BLK_COPY_OFFLOAD_SCC)
		return true;

	return false;
}

/*
 * blkdev_issue_copy - queue a copy
 * @src_bdev:	source block device
 * @nr_srcs:	number of source ranges to copy
 * @src_rlist:	array of source ranges
 * @dest_bdev:	destination block device
 * @dest:	destination in sector
 * @gfp_mask:   memory allocation flags (for bio_alloc)
 * @flags:	BLKDEV_COPY_* flags to control behaviour
 *
 * Description:
 *	Copy source ranges from source block device to destination block device.
 *	length of a source range cannot be zero.
 */
int blkdev_issue_copy(struct block_device *src_bdev, int nr_srcs,
		struct range_entry *src_rlist, struct block_device *dest_bdev,
		sector_t dest, gfp_t gfp_mask, int flags)
{
	struct request_queue *src_q = bdev_get_queue(src_bdev);
	struct request_queue *dest_q = bdev_get_queue(dest_bdev);
	sector_t copy_len;
	int ret = -EINVAL;

	if (!src_q || !dest_q)
		return -ENXIO;

	if (!nr_srcs)
		return -EINVAL;

	if (nr_srcs >= MAX_COPY_NR_RANGE)
		return -EINVAL;

	copy_len = blk_copy_len(src_rlist, nr_srcs);
	if (!copy_len && copy_len >= MAX_COPY_TOTAL_LENGTH)
		return -EINVAL;

	if (bdev_read_only(dest_bdev))
		return -EPERM;

	if (blk_check_offload_scc(src_q, dest_q))
		ret = blk_copy_offload_scc(src_bdev, nr_srcs, src_rlist, dest_bdev, dest, gfp_mask);

	return ret;
}
EXPORT_SYMBOL(blkdev_issue_copy);

/**
 * __blkdev_issue_write_same - generate number of bios with same page
 * @bdev:	target blockdev
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @page:	page containing data to write
 * @biop:	pointer to anchor bio
 *
 * Description:
 *  Generate and issue number of bios(REQ_OP_WRITE_SAME) with same page.
 */
static int __blkdev_issue_write_same(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, struct page *page,
		struct bio **biop)
{
	struct request_queue *q = bdev_get_queue(bdev);
	unsigned int max_write_same_sectors;
	struct bio *bio = *biop;
	sector_t bs_mask;

	if (!q)
		return -ENXIO;

	if (bdev_read_only(bdev))
		return -EPERM;

	bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;

	if (!bdev_write_same(bdev))
		return -EOPNOTSUPP;

	/* Ensure that max_write_same_sectors doesn't overflow bi_size */
	max_write_same_sectors = bio_allowed_max_sectors(q);

	while (nr_sects) {
		bio = blk_next_bio(bio, 1, gfp_mask);
		bio->bi_iter.bi_sector = sector;
		bio_set_dev(bio, bdev);
		bio->bi_vcnt = 1;
		bio->bi_io_vec->bv_page = page;
		bio->bi_io_vec->bv_offset = 0;
		bio->bi_io_vec->bv_len = bdev_logical_block_size(bdev);
		bio_set_op_attrs(bio, REQ_OP_WRITE_SAME, 0);

		if (nr_sects > max_write_same_sectors) {
			bio->bi_iter.bi_size = max_write_same_sectors << 9;
			nr_sects -= max_write_same_sectors;
			sector += max_write_same_sectors;
		} else {
			bio->bi_iter.bi_size = nr_sects << 9;
			nr_sects = 0;
		}
		cond_resched();
	}

	*biop = bio;
	return 0;
}

/**
 * blkdev_issue_write_same - queue a write same operation
 * @bdev:	target blockdev
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @page:	page containing data
 *
 * Description:
 *    Issue a write same request for the sectors in question.
 */
int blkdev_issue_write_same(struct block_device *bdev, sector_t sector,
				sector_t nr_sects, gfp_t gfp_mask,
				struct page *page)
{
	struct bio *bio = NULL;
	struct blk_plug plug;
	int ret;

	blk_start_plug(&plug);
	ret = __blkdev_issue_write_same(bdev, sector, nr_sects, gfp_mask, page,
			&bio);
	if (ret == 0 && bio) {
		ret = submit_bio_wait(bio);
		bio_put(bio);
	}
	blk_finish_plug(&plug);
	return ret;
}
EXPORT_SYMBOL(blkdev_issue_write_same);

static int __blkdev_issue_write_zeroes(struct block_device *bdev,
		sector_t sector, sector_t nr_sects, gfp_t gfp_mask,
		struct bio **biop, unsigned flags)
{
	struct bio *bio = *biop;
	unsigned int max_write_zeroes_sectors;
	struct request_queue *q = bdev_get_queue(bdev);

	if (!q)
		return -ENXIO;

	if (bdev_read_only(bdev))
		return -EPERM;

	/* Ensure that max_write_zeroes_sectors doesn't overflow bi_size */
	max_write_zeroes_sectors = bdev_write_zeroes_sectors(bdev);

	if (max_write_zeroes_sectors == 0)
		return -EOPNOTSUPP;

	while (nr_sects) {
		bio = blk_next_bio(bio, 0, gfp_mask);
		bio->bi_iter.bi_sector = sector;
		bio_set_dev(bio, bdev);
		bio->bi_opf = REQ_OP_WRITE_ZEROES;
		if (flags & BLKDEV_ZERO_NOUNMAP)
			bio->bi_opf |= REQ_NOUNMAP;

		if (nr_sects > max_write_zeroes_sectors) {
			bio->bi_iter.bi_size = max_write_zeroes_sectors << 9;
			nr_sects -= max_write_zeroes_sectors;
			sector += max_write_zeroes_sectors;
		} else {
			bio->bi_iter.bi_size = nr_sects << 9;
			nr_sects = 0;
		}
		cond_resched();
	}

	*biop = bio;
	return 0;
}

/*
 * Convert a number of 512B sectors to a number of pages.
 * The result is limited to a number of pages that can fit into a BIO.
 * Also make sure that the result is always at least 1 (page) for the cases
 * where nr_sects is lower than the number of sectors in a page.
 */
static unsigned int __blkdev_sectors_to_bio_pages(sector_t nr_sects)
{
	sector_t pages = DIV_ROUND_UP_SECTOR_T(nr_sects, PAGE_SIZE / 512);

	return min(pages, (sector_t)BIO_MAX_VECS);
}

static int __blkdev_issue_zero_pages(struct block_device *bdev,
		sector_t sector, sector_t nr_sects, gfp_t gfp_mask,
		struct bio **biop)
{
	struct request_queue *q = bdev_get_queue(bdev);
	struct bio *bio = *biop;
	int bi_size = 0;
	unsigned int sz;

	if (!q)
		return -ENXIO;

	if (bdev_read_only(bdev))
		return -EPERM;

	while (nr_sects != 0) {
		bio = blk_next_bio(bio, __blkdev_sectors_to_bio_pages(nr_sects),
				   gfp_mask);
		bio->bi_iter.bi_sector = sector;
		bio_set_dev(bio, bdev);
		bio_set_op_attrs(bio, REQ_OP_WRITE, 0);

		while (nr_sects != 0) {
			sz = min((sector_t) PAGE_SIZE, nr_sects << 9);
			bi_size = bio_add_page(bio, ZERO_PAGE(0), sz, 0);
			nr_sects -= bi_size >> 9;
			sector += bi_size >> 9;
			if (bi_size < sz)
				break;
		}
		cond_resched();
	}

	*biop = bio;
	return 0;
}

/**
 * __blkdev_issue_zeroout - generate number of zero filed write bios
 * @bdev:	blockdev to issue
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @biop:	pointer to anchor bio
 * @flags:	controls detailed behavior
 *
 * Description:
 *  Zero-fill a block range, either using hardware offload or by explicitly
 *  writing zeroes to the device.
 *
 *  If a device is using logical block provisioning, the underlying space will
 *  not be released if %flags contains BLKDEV_ZERO_NOUNMAP.
 *
 *  If %flags contains BLKDEV_ZERO_NOFALLBACK, the function will return
 *  -EOPNOTSUPP if no explicit hardware offload for zeroing is provided.
 */
int __blkdev_issue_zeroout(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, struct bio **biop,
		unsigned flags)
{
	int ret;
	sector_t bs_mask;

	bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;

	ret = __blkdev_issue_write_zeroes(bdev, sector, nr_sects, gfp_mask,
			biop, flags);
	if (ret != -EOPNOTSUPP || (flags & BLKDEV_ZERO_NOFALLBACK))
		return ret;

	return __blkdev_issue_zero_pages(bdev, sector, nr_sects, gfp_mask,
					 biop);
}
EXPORT_SYMBOL(__blkdev_issue_zeroout);

/**
 * blkdev_issue_zeroout - zero-fill a block range
 * @bdev:	blockdev to write
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @flags:	controls detailed behavior
 *
 * Description:
 *  Zero-fill a block range, either using hardware offload or by explicitly
 *  writing zeroes to the device.  See __blkdev_issue_zeroout() for the
 *  valid values for %flags.
 */
int blkdev_issue_zeroout(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, unsigned flags)
{
	int ret = 0;
	sector_t bs_mask;
	struct bio *bio;
	struct blk_plug plug;
	bool try_write_zeroes = !!bdev_write_zeroes_sectors(bdev);

	bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;

retry:
	bio = NULL;
	blk_start_plug(&plug);
	if (try_write_zeroes) {
		ret = __blkdev_issue_write_zeroes(bdev, sector, nr_sects,
						  gfp_mask, &bio, flags);
	} else if (!(flags & BLKDEV_ZERO_NOFALLBACK)) {
		ret = __blkdev_issue_zero_pages(bdev, sector, nr_sects,
						gfp_mask, &bio);
	} else {
		/* No zeroing offload support */
		ret = -EOPNOTSUPP;
	}
	if (ret == 0 && bio) {
		ret = submit_bio_wait(bio);
		bio_put(bio);
	}
	blk_finish_plug(&plug);
	if (ret && try_write_zeroes) {
		if (!(flags & BLKDEV_ZERO_NOFALLBACK)) {
			try_write_zeroes = false;
			goto retry;
		}
		if (!bdev_write_zeroes_sectors(bdev)) {
			/*
			 * Zeroing offload support was indicated, but the
			 * device reported ILLEGAL REQUEST (for some devices
			 * there is no non-destructive way to verify whether
			 * WRITE ZEROES is actually supported).
			 */
			ret = -EOPNOTSUPP;
		}
	}

	return ret;
}
EXPORT_SYMBOL(blkdev_issue_zeroout);
