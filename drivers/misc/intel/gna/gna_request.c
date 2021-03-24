// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2021 Intel Corporation

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "gna_device.h"
#include "gna_driver.h"
#include "gna_request.h"

static struct gna_request *gna_request_create(struct gna_file_private *file_priv,
				       struct gna_compute_cfg *compute_cfg)
{
	struct gna_request *score_request;
	struct gna_private *gna_priv;

	gna_priv = file_priv->gna_priv;
	if (IS_ERR(gna_priv))
		return NULL;

	score_request = kzalloc(sizeof(*score_request), GFP_KERNEL);
	if (!score_request)
		return NULL;
	kref_init(&score_request->refcount);

	dev_dbg(&gna_priv->pdev->dev, "layer_base %d layer_count %d\n",
		compute_cfg->layer_base, compute_cfg->layer_count);

	score_request->request_id = atomic_inc_return(&gna_priv->request_count);
	score_request->compute_cfg = *compute_cfg;
	score_request->fd = file_priv->fd;
	score_request->gna_priv = gna_priv;
	score_request->state = NEW;
	init_waitqueue_head(&score_request->waitq);

	return score_request;
}

/*
 * returns true if [inner_offset, inner_size) is embraced by [0, outer_size). False otherwise.
 */
static bool gna_validate_ranges(u64 outer_size, u64 inner_offset, u64 inner_size)
{
	return inner_offset < outer_size &&
		inner_size <= (outer_size - inner_offset);
}

static int gna_validate_patches(struct gna_private *gna_priv, __u64 buffer_size,
				struct gna_memory_patch *patches, u64 count)
{
	u64 idx;

	for (idx = 0; idx < count; ++idx) {
		if (patches[idx].size > 8) {
			dev_err(&gna_priv->pdev->dev, "invalid patch size: %llu\n", patches[idx].size);
			return -EINVAL;
		}

		if (!gna_validate_ranges(buffer_size, patches[idx].offset, patches[idx].size)) {
			dev_err(&gna_priv->pdev->dev,
				"patch out of bounds. buffer size: %llu, patch offset/size:%llu/%llu\n",
				buffer_size, patches[idx].offset, patches[idx].size);
			return -EINVAL;
		}
	}

	return 0;
}

static int gna_buffer_fill_patches(struct gna_buffer *buffer, struct gna_private *gna_priv)
{
	__u64 patches_user = buffer->patches_ptr;
	struct gna_memory_patch *patches;
	/* At this point, the buffer points to a memory region in kernel space where the copied
	 * patches_ptr also lives, but the value of it is still an address from user space. This
	 * function will set patches_ptr to either an address in kernel space or null before it
	 * exits.
	 */
	u64 patch_count;
	int ret;

	buffer->patches_ptr = 0;
	patch_count = buffer->patch_count;
	if (!patch_count)
		return 0;

	patches = kvmalloc_array(patch_count, sizeof(struct gna_memory_patch), GFP_KERNEL);
	if (!patches)
		return -ENOMEM;

	if (copy_from_user(patches, u64_to_user_ptr(patches_user),
				sizeof(struct gna_memory_patch) * patch_count)) {
		dev_err(&gna_priv->pdev->dev, "copy %llu patches from user failed\n", patch_count);
		ret = -EFAULT;
		goto err_fill_patches;
	}

	ret = gna_validate_patches(gna_priv, buffer->size, patches, patch_count);
	if (ret) {
		dev_err(&gna_priv->pdev->dev, "patches failed validation\n");
		goto err_fill_patches;
	}

	buffer->patches_ptr = (uintptr_t)patches;

	return 0;

err_fill_patches:
	kvfree(patches);
	return ret;
}

static int gna_request_fill_buffers(struct gna_request *score_request,
				    struct gna_compute_cfg *compute_cfg)
{
	struct gna_buffer *buffer_list;
	struct gna_memory_object *mo;
	struct gna_private *gna_priv;
	u64 buffers_total_size = 0;
	struct gna_buffer *buffer;
	u64 buffer_count;
	u64 memory_id;
	u64 i, j;
	int ret;

	gna_priv = score_request->gna_priv;

	buffer_count = compute_cfg->buffer_count;
	buffer_list = kvmalloc_array(buffer_count, sizeof(struct gna_buffer), GFP_KERNEL);
	if (!buffer_list)
		return -ENOMEM;

	if (copy_from_user(buffer_list, u64_to_user_ptr(compute_cfg->buffers_ptr),
			sizeof(*buffer_list) * buffer_count)) {
		dev_err(&gna_priv->pdev->dev, "copying %llu buffers failed\n", buffer_count);
		ret = -EFAULT;
		goto err_free_buffers;
	}

	for (i = 0; i < buffer_count; i++) {
		buffer = &buffer_list[i];
		memory_id = buffer->memory_id;

		for (j = 0; j < i; j++) {
			if (buffer_list[j].memory_id == memory_id) {
				dev_err(&gna_priv->pdev->dev,
					"doubled memory id in score config. id:%llu\n", memory_id);
				ret = -EINVAL;
				goto err_zero_patch_ptr;
			}
		}

		buffers_total_size +=
			gna_buffer_get_size(buffer->offset, buffer->size);
		if (buffers_total_size > gna_priv->info.max_hw_mem) {
			dev_err(&gna_priv->pdev->dev, "buffers' total size too big\n");
			ret = -EINVAL;
			goto err_zero_patch_ptr;
		}

		mutex_lock(&gna_priv->memidr_lock);
		mo = idr_find(&gna_priv->memory_idr, memory_id);
		if (!mo) {
			mutex_unlock(&gna_priv->memidr_lock);
			dev_err(&gna_priv->pdev->dev, "memory object %llu not found\n", memory_id);
			ret = -EINVAL;
			goto err_zero_patch_ptr;
		}
		mutex_unlock(&gna_priv->memidr_lock);

		if (mo->fd != score_request->fd) {
			dev_err(&gna_priv->pdev->dev,
				"memory object from another file. %p != %p\n",
				mo->fd, score_request->fd);
			ret = -EINVAL;
			goto err_zero_patch_ptr;
		}

		if (!gna_validate_ranges(mo->memory_size, buffer->offset, buffer->size)) {
			dev_err(&gna_priv->pdev->dev,
				"buffer out of bounds. mo size: %llu, buffer offset/size:%llu/%llu\n",
				mo->memory_size, buffer->offset, buffer->size);
			ret = -EINVAL;
			goto err_zero_patch_ptr;
		}

		ret = gna_buffer_fill_patches(buffer, gna_priv);
		if (ret)
			goto err_free_patches;
	}

	score_request->buffer_list = buffer_list;
	score_request->buffer_count = buffer_count;

	return 0;

err_zero_patch_ptr:
	/* patches_ptr may still hold an address in userspace.
	 * Don't pass it to kvfree().
	 */
	buffer->patches_ptr = 0;

err_free_patches:
	/* patches_ptr of each processed buffer should be either
	 * null or pointing to an allocated memory block in the
	 * kernel at this point.
	 */
	for (j = 0; j <= i; j++)
		kvfree((void *)(uintptr_t)buffer_list[j].patches_ptr);

err_free_buffers:
	kvfree(buffer_list);
	return ret;
}

int gna_enqueue_request(struct gna_compute_cfg *compute_cfg,
			struct gna_file_private *file_priv, u64 *request_id)
{
	struct gna_request *score_request;
	struct gna_private *gna_priv;
	int ret;

	if (!file_priv)
		return -EINVAL;

	gna_priv = file_priv->gna_priv;

	score_request = gna_request_create(file_priv, compute_cfg);
	if (!score_request)
		return -ENOMEM;

	ret = gna_request_fill_buffers(score_request, compute_cfg);
	if (ret) {
		kref_put(&score_request->refcount, gna_request_release);
		return ret;
	}

	kref_get(&score_request->refcount);
	mutex_lock(&gna_priv->reqlist_lock);
	list_add_tail(&score_request->node, &gna_priv->request_list);
	mutex_unlock(&gna_priv->reqlist_lock);

	kref_put(&score_request->refcount, gna_request_release);

	*request_id = score_request->request_id;

	return 0;
}

void gna_request_release(struct kref *ref)
{
	struct gna_request *score_request =
		container_of(ref, struct gna_request, refcount);
	kfree(score_request);
}

struct gna_request *gna_find_request_by_id(u64 req_id, struct gna_private *gna_priv)
{
	struct gna_request *req, *found_req;
	struct list_head *reqs_list;

	mutex_lock(&gna_priv->reqlist_lock);

	reqs_list = &gna_priv->request_list;
	found_req = NULL;
	if (!list_empty(reqs_list)) {
		list_for_each_entry(req, reqs_list, node) {
			if (req_id == req->request_id) {
				found_req = req;
				kref_get(&found_req->refcount);
				break;
			}
		}
	}

	mutex_unlock(&gna_priv->reqlist_lock);

	return found_req;
}

void gna_delete_request_by_id(u64 req_id, struct gna_private *gna_priv)
{
	struct gna_request *req, *temp_req;
	struct list_head *reqs_list;

	mutex_lock(&gna_priv->reqlist_lock);

	reqs_list = &gna_priv->request_list;
	if (!list_empty(reqs_list)) {
		list_for_each_entry_safe(req, temp_req, reqs_list, node) {
			if (req->request_id == req_id) {
				list_del(&req->node);
				kref_put(&req->refcount, gna_request_release);
				break;
			}
		}
	}

	mutex_unlock(&gna_priv->reqlist_lock);
}

void gna_delete_file_requests(struct file *fd, struct gna_private *gna_priv)
{
	struct gna_request *req, *temp_req;
	struct list_head *reqs_list;

	mutex_lock(&gna_priv->reqlist_lock);

	reqs_list = &gna_priv->request_list;
	if (!list_empty(reqs_list)) {
		list_for_each_entry_safe(req, temp_req, reqs_list, node) {
			if (req->fd == fd) {
				list_del(&req->node);
				kref_put(&req->refcount, gna_request_release);
				break;
			}
		}
	}

	mutex_unlock(&gna_priv->reqlist_lock);
}

void gna_delete_memory_requests(u64 memory_id, struct gna_private *gna_priv)
{
	struct gna_request *req, *temp_req;
	struct list_head *reqs_list;
	int i;

	mutex_lock(&gna_priv->reqlist_lock);

	reqs_list = &gna_priv->request_list;
	if (!list_empty(reqs_list)) {
		list_for_each_entry_safe(req, temp_req, reqs_list, node) {
			for (i = 0; i < req->buffer_count; ++i) {
				if (req->buffer_list[i].memory_id == memory_id) {
					list_del(&req->node);
					kref_put(&req->refcount, gna_request_release);
					break;
				}
			}
		}
	}

	mutex_unlock(&gna_priv->reqlist_lock);
}
