/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SCRUB_H
#define BTRFS_SCRUB_H

#define SCRUB_BIOS_PER_SCTX	64	/* 8MB per device in flight */

struct scrub_ctx {
	struct scrub_bio	*bios[SCRUB_BIOS_PER_SCTX];
	struct btrfs_fs_info	*fs_info;
	int			first_free;
	int			curr;
	atomic_t		bios_in_flight;
	atomic_t		workers_pending;
	spinlock_t		list_lock;
	wait_queue_head_t	list_wait;
	struct list_head	csum_list;
	atomic_t		cancel_req;
	int			readonly;
	int			pages_per_rd_bio;

	/* State of IO submission throttling affecting the associated device */
	ktime_t			throttle_deadline;
	u64			throttle_sent;

	int			is_dev_replace;
	u64			write_pointer;

	struct scrub_bio        *wr_curr_bio;
	struct mutex            wr_lock;
	int                     pages_per_wr_bio; /* <= SCRUB_PAGES_PER_WR_BIO */
	struct btrfs_device     *wr_tgtdev;
	bool                    flush_all_writes;

	/*
	 * statistics
	 */
	struct btrfs_scrub_progress stat;
	spinlock_t		stat_lock;

	/*
	 * Use a ref counter to avoid use-after-free issues. Scrub workers
	 * decrement bios_in_flight and workers_pending and then do a wakeup
	 * on the list_wait wait queue. We must ensure the main scrub task
	 * doesn't free the scrub context before or while the workers are
	 * doing the wakeup() call.
	 */
	refcount_t              refs;
};

void btrfs_scrub_submit(struct scrub_ctx *sctx);
void btrfs_scrub_wr_submit(struct scrub_ctx *sctx);
int btrfs_scrub_dev(struct btrfs_fs_info *fs_info, u64 devid, u64 start,
		    u64 end, struct btrfs_scrub_progress *progress,
		    int readonly, int is_dev_replace);
void btrfs_scrub_pause(struct btrfs_fs_info *fs_info);
void btrfs_scrub_continue(struct btrfs_fs_info *fs_info);
int btrfs_scrub_cancel(struct btrfs_fs_info *info);
int btrfs_scrub_cancel_dev(struct btrfs_device *dev);
int btrfs_scrub_progress(struct btrfs_fs_info *fs_info, u64 devid,
			 struct btrfs_scrub_progress *progress);
#endif /* BTRFS_SCRUB_H */
