// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "dpu_dbg.h"
#include "dpu_kms.h"
#include "dpu_hw_catalog.h"

#ifdef CONFIG_DEV_COREDUMP
static ssize_t dpu_devcoredump_read(char *buffer, loff_t offset,
		size_t count, void *data, size_t datalen)
{
	struct drm_print_iterator iter;
	struct drm_printer p;
	struct dpu_dbg_base *dpu_dbg;

	dpu_dbg = data;

	iter.data = buffer;
	iter.offset = 0;
	iter.start = offset;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	drm_printf(&p, "---\n");

	drm_printf(&p, "module: " KBUILD_MODNAME "\n");
	drm_printf(&p, "dpu devcoredump\n");
	drm_printf(&p, "timestamp %lld\n", ktime_to_ns(dpu_dbg->timestamp));

	dpu_dbg->dpu_dbg_printer = &p;

	dpu_dbg_print_regs(dpu_dbg->drm_dev, DPU_DBG_DUMP_IN_COREDUMP);

	drm_printf(&p, "===================dpu drm state================\n");

	if (dpu_dbg->atomic_state)
		drm_atomic_print_new_state(dpu_dbg->atomic_state,
				&p);

	return count - iter.remain;
}

static void dpu_devcoredump_free(void *data)
{
	struct dpu_dbg_base *dpu_dbg;

	dpu_dbg = data;

	if (dpu_dbg->atomic_state) {
		drm_atomic_state_put(dpu_dbg->atomic_state);
		dpu_dbg->atomic_state = NULL;
	}

	dpu_dbg_free_blk_mem(dpu_dbg->drm_dev);

	dpu_dbg->coredump_pending = false;
}

static void dpu_devcoredump_capture_state(struct dpu_dbg_base *dpu_dbg)
{
	struct drm_device *ddev;
	struct drm_modeset_acquire_ctx ctx;

	dpu_dbg->timestamp = ktime_get();

	ddev = dpu_dbg->drm_dev;

	drm_modeset_acquire_init(&ctx, 0);

	while (drm_modeset_lock_all_ctx(ddev, &ctx) != 0)
		drm_modeset_backoff(&ctx);

	dpu_dbg->atomic_state = drm_atomic_helper_duplicate_state(ddev,
			&ctx);
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}
#else
static void dpu_devcoredump_capture_state(struct dpu_dbg_base *dpu_dbg)
{
}
#endif /* CONFIG_DEV_COREDUMP */

static void _dpu_dump_work(struct kthread_work *work)
{
	struct dpu_dbg_base *dpu_dbg = container_of(work, struct dpu_dbg_base, dump_work);
	struct drm_printer p;

	/* reset the reg_dump_method to IN_MEM before every dump */
	dpu_dbg->reg_dump_method = DPU_DBG_DUMP_IN_MEM;

	dpu_dbg_dump_blks(dpu_dbg);

	dpu_devcoredump_capture_state(dpu_dbg);

	if (DPU_DBG_DUMP_IN_CONSOLE) {
		p = drm_info_printer(dpu_dbg->drm_dev->dev);
		dpu_dbg->dpu_dbg_printer = &p;
		dpu_dbg_print_regs(dpu_dbg->drm_dev, DPU_DBG_DUMP_IN_LOG);
	}

#ifdef CONFIG_DEV_COREDUMP
	dev_coredumpm(dpu_dbg->dev, THIS_MODULE, dpu_dbg, 0, GFP_KERNEL,
			dpu_devcoredump_read, dpu_devcoredump_free);
	dpu_dbg->coredump_pending = true;
#endif
}

void dpu_dbg_dump(struct drm_device *drm_dev, const char *name, ...)
{
	va_list args;
	char *blk_name = NULL;
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct dpu_dbg_base *dpu_dbg;
	int index = 0;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return;
	}

	priv = drm_dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);
	dpu_dbg = dpu_kms->dpu_dbg;

	if (!dpu_dbg) {
		DRM_ERROR("invalid params\n");
		return;
	}

	/*
	 * if there is a coredump pending return immediately till dump
	 * if read by userspace or timeout happens
	 */
	if (((dpu_dbg->reg_dump_method == DPU_DBG_DUMP_IN_MEM) ||
		 (dpu_dbg->reg_dump_method == DPU_DBG_DUMP_IN_COREDUMP)) &&
		dpu_dbg->coredump_pending) {
		DRM_DEBUG("coredump is pending read\n");
		return;
	}

	va_start(args, name);

	while ((blk_name = va_arg(args, char*))) {

		if (IS_ERR_OR_NULL(blk_name))
			break;

		if (index < DPU_DBG_BASE_MAX)
			dpu_dbg->blk_names[index++] = blk_name;
		else
			DRM_ERROR("too many blk names\n");
	}
	va_end(args);

	kthread_queue_work(dpu_dbg->dump_worker,
			&dpu_dbg->dump_work);
}

int dpu_dbg_init(struct drm_device *drm_dev)
{
	struct dpu_kms *dpu_kms;
	struct msm_drm_private *priv;
	struct dpu_dbg_base *dpu_dbg;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return -EINVAL;
	}

	priv = drm_dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);

	dpu_dbg = devm_kzalloc(drm_dev->dev, sizeof(struct dpu_dbg_base), GFP_KERNEL);

	mutex_init(&dpu_dbg->mutex);

	dpu_dbg->dev = drm_dev->dev;
	dpu_dbg->drm_dev = drm_dev;

	dpu_dbg->reg_dump_method = DEFAULT_REGDUMP;

	dpu_dbg->dump_worker = kthread_create_worker(0, "%s", "dpu_dbg");
	if (IS_ERR(dpu_dbg->dump_worker))
		DRM_ERROR("failed to create dpu dbg task\n");

	kthread_init_work(&dpu_dbg->dump_work, _dpu_dump_work);

	dpu_kms->dpu_dbg = dpu_dbg;

	dpu_dbg_init_blk_info(drm_dev);

	return 0;
}

void dpu_dbg_destroy(struct drm_device *drm_dev)
{
	struct dpu_kms *dpu_kms;
	struct msm_drm_private *priv;
	struct dpu_dbg_base *dpu_dbg;

	if (!drm_dev) {
		DRM_ERROR("invalid params\n");
		return;
	}

	priv = drm_dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);
	dpu_dbg = dpu_kms->dpu_dbg;

	if (dpu_dbg->dump_worker)
		kthread_destroy_worker(dpu_dbg->dump_worker);

	mutex_destroy(&dpu_dbg->mutex);
}
