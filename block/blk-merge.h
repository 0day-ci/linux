/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_MERGE_H
#define BLK_MERGE_H

#include "blk-mq.h"

struct bio *blk_bio_discard_split(struct request_queue *q, struct bio *bio,
				  struct bio_set *bs, unsigned *nsegs);
struct bio *blk_bio_write_zeroes_split(struct request_queue *q, struct bio *bio,
				       struct bio_set *bs, unsigned *nsegs);
struct bio *blk_bio_write_same_split(struct request_queue *q, struct bio *bio,
				     struct bio_set *bs, unsigned *nsegs);
struct bio *blk_bio_segment_split(struct request_queue *q, struct bio *bio,
				  struct bio_set *bs, unsigned *segs);
void blk_bio_handle_split(struct bio **bio, struct bio *split);

/**
 * blk_queue_split - split a bio and submit the second half
 * @bio:     [in, out] bio to be split
 * @nr_segs: [out] number of segments in the first bio
 *
 * Split a bio into two bios, chain the two bios, submit the second half and
 * store a pointer to the first half in *@bio. If the second bio is still too
 * big it will be split by a recursive call to this function. Since this
 * function may allocate a new bio from q->bio_split, it is the responsibility
 * of the caller to ensure that q->bio_split is only released after processing
 * of the split bio has finished.
 */
static inline void __blk_queue_split(struct request_queue *q, struct bio **bio,
				     unsigned int *nr_segs)
{
	struct bio *split = NULL;

	switch (bio_op(*bio)) {
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
		split = blk_bio_discard_split(q, *bio, &q->bio_split, nr_segs);
		break;
	case REQ_OP_WRITE_ZEROES:
		split = blk_bio_write_zeroes_split(q, *bio, &q->bio_split,
				nr_segs);
		break;
	case REQ_OP_WRITE_SAME:
		split = blk_bio_write_same_split(q, *bio, &q->bio_split,
				nr_segs);
		break;
	default:
		/*
		 * All drivers must accept single-segments bios that are <=
		 * PAGE_SIZE.  This is a quick and dirty check that relies on
		 * the fact that bi_io_vec[0] is always valid if a bio has data.
		 * The check might lead to occasional false negatives when bios
		 * are cloned, but compared to the performance impact of cloned
		 * bios themselves the loop below doesn't matter anyway.
		 */
		if (!q->limits.chunk_sectors &&
		    (*bio)->bi_vcnt == 1 &&
		    ((*bio)->bi_io_vec[0].bv_len +
		     (*bio)->bi_io_vec[0].bv_offset) <= PAGE_SIZE) {
			*nr_segs = 1;
			break;
		}
		split = blk_bio_segment_split(q, *bio, &q->bio_split, nr_segs);
		break;
	}

	if (split)
		blk_bio_handle_split(bio, split);
}

#endif
