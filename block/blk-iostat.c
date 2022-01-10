#include <linux/kernel.h>
#include <linux/blk_types.h>
#include <linux/module.h>
#include <linux/blk-cgroup.h>
#include <linux/bio.h>
#include <linux/spinlock.h>

#include "blk.h"
#include "blk-rq-qos.h"

enum {
	IOSTAT_READ = 0,
	IOSTAT_WRITE,
	IOSTAT_DISCARD,
	IOSTAT_MAX,
};

struct iostat_data {
	u64 bytes[IOSTAT_MAX];
	u64 ios[IOSTAT_MAX];
	u64 queue_lat[IOSTAT_MAX];
	u64 dev_lat[IOSTAT_MAX];
};

struct iostat_queue {
	struct rq_qos rqos;
};

struct iostat_gq {
	struct blkg_policy_data pd;
	char disk_name[DISK_NAME_LEN];
	struct {
		struct iostat_data __percpu *data;
		struct iostat_data __percpu *meta;
	} stat;
};

struct iostat_cgrp {
	struct blkcg_policy_data cpd;
};

DEFINE_MUTEX(iostat_mutex);

static struct blkcg_policy blkcg_policy_iostat;

static inline struct iostat_gq *pd_to_ist(struct blkg_policy_data *pd)
{
	return pd ? container_of(pd, struct iostat_gq, pd) : NULL;
}

static inline struct iostat_gq *blkg_to_ist(struct blkcg_gq *blkg)
{
	return pd_to_ist(blkg_to_pd(blkg, &blkcg_policy_iostat));
}

static inline bool req_is_meta(struct request *req)
{
	return req->cmd_flags & REQ_META;
}

static inline int iostat_op(struct request *req)
{
	int op;

	if (unlikely(op_is_discard(req_op(req))))
		op = IOSTAT_DISCARD;
	else if (op_is_write(req_op(req)))
		op = IOSTAT_WRITE;
	else
		op = IOSTAT_READ;

	return op;
}

static void __iostat_issue(struct rq_qos *rqos,
		struct iostat_gq *is, struct request *req)
{
	struct iostat_data *stat;
	int op = iostat_op(req);

	/*
	 * blk_mq_start_request() inherents bio_issue_time() when BLK_CGROUP
	 * to avoid overhead of readtsc.
	 */
	req->io_start_time_ns = ktime_get_ns();
	if (req_is_meta(req))
		stat = get_cpu_ptr(is->stat.meta);
	else
		stat = get_cpu_ptr(is->stat.data);
	/*
	 * alloc_time_ns is get before get tag, we use it monitor depth,
	 * tag waits and in queue time.
	 */
	stat->queue_lat[op] += req->io_start_time_ns - req->alloc_time_ns;
	stat->ios[op]++;
	stat->bytes[op] += blk_rq_bytes(req);
	put_cpu_ptr(stat);
}

static void iostat_issue(struct rq_qos *rqos, struct request *req)
{
	struct iostat_gq *is;

	if (unlikely(!req->bio))
		return;

	is = blkg_to_ist(req->blkg);
	/*
	 * Most of time, bios from submit_bio would have the valid bi_blkg,
	 * however, blk_execute_rq case is an exception.
	 */
	if (is)
		__iostat_issue(rqos, is, req);
}

static void __iostat_done(struct rq_qos *rq_qos,
		struct iostat_gq *is, struct request *req)
{
	struct iostat_data *stat;
	int op = iostat_op(req);

	if (req_is_meta(req))
		stat = get_cpu_ptr(is->stat.meta);
	else
		stat = get_cpu_ptr(is->stat.data);
	if (req->io_start_time_ns)
		stat->dev_lat[op] += ktime_get_ns() - req->io_start_time_ns;
	put_cpu_ptr(stat);
}

static void iostat_done(struct rq_qos *rqos, struct request *req)
{
	struct iostat_gq *is = blkg_to_ist(req->blkg);

	if (is)
		__iostat_done(rqos, is, req);
}

static void iostat_exit(struct rq_qos *rqos)
{
	struct iostat_queue *isq = container_of(rqos, struct iostat_queue, rqos);

	blkcg_deactivate_policy(rqos->q, &blkcg_policy_iostat);
	rq_qos_deactivate(rqos);
	kfree(isq);
}

static int iostat_init(struct request_queue *q);

struct rq_qos_ops iostat_rq_ops = {
#if IS_MODULE(CONFIG_BLK_CGROUP_IOLATENCY)
	.owner = THIS_MODULE,
#endif
	.name = "iostat",
	.flags = RQOS_FLAG_CGRP_POL | RQOS_FLAG_RQ_ALLOC_TIME,
	.issue = iostat_issue,
	.done = iostat_done,
	.exit = iostat_exit,
	.init = iostat_init,
};

static int iostat_init(struct request_queue *q)
{
	struct iostat_queue *isq;
	struct rq_qos *rqos;
	int ret;

	isq = kzalloc_node(sizeof(*isq), GFP_KERNEL, q->node);
	if (!isq) {
		ret = -ENOMEM;
		goto out;
	}

	blk_queue_flag_set(QUEUE_FLAG_RQ_ALLOC_TIME, q);
	rqos = &isq->rqos;
	rq_qos_activate(q, rqos, &iostat_rq_ops);

	ret = blkcg_activate_policy(q, &blkcg_policy_iostat);
	if (ret) {
		rq_qos_deactivate(rqos);
		kfree(isq);
	}
out:
	return ret;
}

static void iostat_sum(struct blkcg_gq *blkg,
		struct iostat_data *sum, bool meta)
{
	struct iostat_gq *is = blkg_to_ist(blkg);
	struct iostat_data *stat;
	int cpu, i;

	for_each_possible_cpu(cpu) {
		if (meta)
			stat = per_cpu_ptr(is->stat.meta, cpu);
		else
			stat = per_cpu_ptr(is->stat.data, cpu);
		for (i = 0; i < IOSTAT_MAX; i++) {
			sum->bytes[i] += stat->bytes[i];
			sum->ios[i] += stat->ios[i];
			sum->dev_lat[i] += stat->dev_lat[i];
			sum->queue_lat[i] += stat->queue_lat[i];
		}
	}
}

static int iostat_show(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct cgroup_subsys_state *pos_css;
	struct iostat_gq *is;
	struct blkcg_gq *blkg, *pos_blkg;
	struct iostat_data data_sum, meta_sum;
	int i;

	rcu_read_lock();
	hlist_for_each_entry_rcu(blkg, &blkcg->blkg_list, blkcg_node) {
		is = blkg_to_ist(blkg);
		/*
		 * The is activated on demand so iostat may be NULL
		 */
		if (!is)
			continue;

		memset(&data_sum, 0, sizeof(data_sum));
		memset(&meta_sum, 0, sizeof(meta_sum));
		if (blkg == blkg->q->root_blkg) {
			iostat_sum(blkg, &data_sum, false);
			iostat_sum(blkg, &meta_sum, true);
		} else {
			/*
			 * Iterate every children blkg to agregate statistics
			 */
			blkg_for_each_descendant_pre(pos_blkg, pos_css, blkg) {
				if (!pos_blkg->online)
					continue;
				iostat_sum(pos_blkg, &data_sum, false);
				iostat_sum(pos_blkg, &meta_sum, true);
			}
		}

		seq_printf(sf, "%s-data ", is->disk_name);
		for (i = 0; i < IOSTAT_MAX; i++)
			seq_printf(sf, "%llu %llu %llu %llu ",
				data_sum.bytes[i], data_sum.ios[i],
				data_sum.queue_lat[i], data_sum.dev_lat[i]);
		seq_printf(sf, "\n");
		seq_printf(sf, "%s-meta ", is->disk_name);
		for (i = 0; i < IOSTAT_MAX; i++)
			seq_printf(sf, "%llu %llu %llu %llu ",
				meta_sum.bytes[i], meta_sum.ios[i],
				meta_sum.queue_lat[i], meta_sum.dev_lat[i]);
		seq_printf(sf, "\n");
	}
	rcu_read_unlock();

	return 0;
}

static struct cftype iostat_files[] = {
	{
		.name = "iostat",
		.seq_show = iostat_show,
	},
	{}
};

static struct cftype iostat_legacy_files[] = {
	{
		.name = "iostat",
		.seq_show = iostat_show,
	},
	{}
};

static void iostat_pd_free(struct blkg_policy_data *pd)
{
	struct iostat_gq *is = pd_to_ist(pd);

	if (is->stat.data)
		free_percpu(is->stat.data);

	if (is->stat.meta)
		free_percpu(is->stat.meta);

	kfree(is);
}

static struct blkg_policy_data *iostat_pd_alloc(gfp_t gfp,
		struct request_queue *q, struct blkcg *blkcg)
{
	struct iostat_gq *is;

	is = kzalloc_node(sizeof(*is), gfp, q->node);
	if (!is)
		return NULL;

	is->stat.data = __alloc_percpu_gfp(sizeof(struct iostat_data),
			__alignof__(struct iostat_data), gfp);
	if (!is->stat.data)
		goto out_free;

	is->stat.meta = __alloc_percpu_gfp(sizeof(struct iostat_data),
			__alignof__(struct iostat_data), gfp);
	if (!is->stat.meta)
		goto out_free;
	/*
	 * request_queue.kobj's parent is gendisk
	 */
	strlcpy(is->disk_name,
		kobject_name(q->kobj.parent),
		DISK_NAME_LEN);
	return &is->pd;
out_free:
	if (is->stat.data)
		free_percpu(is->stat.data);
	iostat_pd_free(&is->pd);
	return NULL;
}

static struct blkcg_policy blkcg_policy_iostat = {
	.dfl_cftypes	= iostat_files,
	.legacy_cftypes	= iostat_legacy_files,
	.pd_alloc_fn	= iostat_pd_alloc,
	.pd_free_fn	= iostat_pd_free,
};

static int __init iostat_mod_init(void)
{
	int ret;

	ret = rq_qos_register(&iostat_rq_ops);
	if (ret)
		return ret;

	ret = blkcg_policy_register(&blkcg_policy_iostat);
	if (ret) {
		rq_qos_unregister(&iostat_rq_ops);
		return ret;
	}

	return 0;
}

static void __exit iostat_mod_exit(void)
{
	rq_qos_unregister(&iostat_rq_ops);
	blkcg_policy_unregister(&blkcg_policy_iostat);
}

module_init(iostat_mod_init);
module_exit(iostat_mod_exit);
MODULE_AUTHOR("Wang Jianchao");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Block Statistics per Cgroup");
