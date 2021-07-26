// SPDX-License-Identifier: GPL-2.0
/*
 * virtio_pmem.c: Virtio pmem Driver
 *
 * Discovers persistent memory range information
 * from host and provides a virtio based flushing
 * interface.
 */
#include "virtio_pmem.h"
#include "nd.h"

 /* The interrupt handler */
void virtio_pmem_host_ack(struct virtqueue *vq)
{
	struct virtio_pmem *vpmem = vq->vdev->priv;
	struct virtio_pmem_request *req_data, *req_buf;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vpmem->pmem_lock, flags);
	while ((req_data = virtqueue_get_buf(vq, &len)) != NULL) {
		req_data->done = true;
		wake_up(&req_data->host_acked);

		if (!list_empty(&vpmem->req_list)) {
			req_buf = list_first_entry(&vpmem->req_list,
					struct virtio_pmem_request, list);
			req_buf->wq_buf_avail = true;
			wake_up(&req_buf->wq_buf);
			list_del(&req_buf->list);
		}
	}
	spin_unlock_irqrestore(&vpmem->pmem_lock, flags);
}
EXPORT_SYMBOL_GPL(virtio_pmem_host_ack);

 /* The request submission function */
static int virtio_pmem_flush(struct nd_region *nd_region)
{
	struct virtio_device *vdev = nd_region->provider_data;
	struct virtio_pmem *vpmem  = vdev->priv;
	struct virtio_pmem_request *req_data;
	struct scatterlist *sgs[2], sg, ret;
	unsigned long flags;
	int err, err1;

	might_sleep();
	req_data = kmalloc(sizeof(*req_data), GFP_KERNEL);
	if (!req_data)
		return -ENOMEM;

	req_data->done = false;
	init_waitqueue_head(&req_data->host_acked);
	init_waitqueue_head(&req_data->wq_buf);
	INIT_LIST_HEAD(&req_data->list);
	req_data->req.type = cpu_to_le32(VIRTIO_PMEM_REQ_TYPE_FLUSH);
	sg_init_one(&sg, &req_data->req, sizeof(req_data->req));
	sgs[0] = &sg;
	sg_init_one(&ret, &req_data->resp.ret, sizeof(req_data->resp));
	sgs[1] = &ret;

	spin_lock_irqsave(&vpmem->pmem_lock, flags);
	 /*
	  * If virtqueue_add_sgs returns -ENOSPC then req_vq virtual
	  * queue does not have free descriptor. We add the request
	  * to req_list and wait for host_ack to wake us up when free
	  * slots are available.
	  */
	while ((err = virtqueue_add_sgs(vpmem->req_vq, sgs, 1, 1, req_data,
					GFP_ATOMIC)) == -ENOSPC) {

		dev_info(&vdev->dev, "failed to send command to virtio pmem device, no free slots in the virtqueue\n");
		req_data->wq_buf_avail = false;
		list_add_tail(&req_data->list, &vpmem->req_list);
		spin_unlock_irqrestore(&vpmem->pmem_lock, flags);

		/* A host response results in "host_ack" getting called */
		wait_event(req_data->wq_buf, req_data->wq_buf_avail);
		spin_lock_irqsave(&vpmem->pmem_lock, flags);
	}
	err1 = virtqueue_kick(vpmem->req_vq);
	spin_unlock_irqrestore(&vpmem->pmem_lock, flags);
	/*
	 * virtqueue_add_sgs failed with error different than -ENOSPC, we can't
	 * do anything about that.
	 */
	if (err || !err1) {
		dev_info(&vdev->dev, "failed to send command to virtio pmem device\n");
		err = -EIO;
	} else {
		/* A host repsonse results in "host_ack" getting called */
		wait_event(req_data->host_acked, req_data->done);
		err = le32_to_cpu(req_data->resp.ret);
	}

	kfree(req_data);
	return err;
};

static void submit_async_flush(struct work_struct *ws);

/* The asynchronous flush callback function */
int async_pmem_flush(struct nd_region *nd_region, struct bio *bio)
{
	/* queue asynchronous flush and coalesce the flush requests */
	struct virtio_device *vdev = nd_region->provider_data;
	struct virtio_pmem *vpmem  = vdev->priv;
	ktime_t req_start = ktime_get_boottime();

	spin_lock_irq(&vpmem->lock);
	/* flush requests wait until ongoing flush completes,
	 * hence coalescing all the pending requests.
	 */
	wait_event_lock_irq(vpmem->sb_wait,
			    !vpmem->flush_bio ||
			    ktime_before(req_start, vpmem->prev_flush_start),
			    vpmem->lock);
	/* new request after previous flush is completed */
	if (ktime_after(req_start, vpmem->prev_flush_start)) {
		WARN_ON(vpmem->flush_bio);
		vpmem->flush_bio = bio;
		bio = NULL;
	}
	spin_unlock_irq(&vpmem->lock);

	if (!bio) {
		INIT_WORK(&vpmem->flush_work, submit_async_flush);
		queue_work(vpmem->pmem_wq, &vpmem->flush_work);
		return 1;
	}

	/* flush completed in other context while we waited */
	if (bio && (bio->bi_opf & REQ_PREFLUSH)) {
		bio->bi_opf &= ~REQ_PREFLUSH;
		submit_bio(bio);
	} else if (bio && (bio->bi_opf & REQ_FUA)) {
		bio->bi_opf &= ~REQ_FUA;
		bio_endio(bio);
	}

	return 0;
};
EXPORT_SYMBOL_GPL(async_pmem_flush);

static void submit_async_flush(struct work_struct *ws)
{
	struct virtio_pmem *vpmem = container_of(ws, struct virtio_pmem, flush_work);
	struct bio *bio = vpmem->flush_bio;

	vpmem->start_flush = ktime_get_boottime();
	bio->bi_status = errno_to_blk_status(virtio_pmem_flush(vpmem->nd_region));
	vpmem->prev_flush_start = vpmem->start_flush;
	vpmem->flush_bio = NULL;
	wake_up(&vpmem->sb_wait);

	/* Submit parent bio only for PREFLUSH */
	if (bio && (bio->bi_opf & REQ_PREFLUSH)) {
		bio->bi_opf &= ~REQ_PREFLUSH;
		submit_bio(bio);
	} else if (bio && (bio->bi_opf & REQ_FUA)) {
		bio->bi_opf &= ~REQ_FUA;
		bio_endio(bio);
	}
}
MODULE_LICENSE("GPL");
