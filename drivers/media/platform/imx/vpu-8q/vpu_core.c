// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#define TAG		"CORE"

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/firmware.h>
#include "vpu.h"
#include "vpu_defs.h"
#include "vpu_mbox.h"
#include "vpu_msgs.h"
#include "vpu_rpc.h"
#include "vpu_log.h"
#include "vpu_cmds.h"

unsigned int vpu_dbg_level = LVL_ERR | LVL_WARN | LVL_INFO;
module_param(vpu_dbg_level, uint, 0644);

void csr_writel(struct vpu_core *core, u32 reg, u32 val)
{
	writel(val, core->base + reg);
}

u32 csr_readl(struct vpu_core *core, u32 reg)
{
	return readl(core->base + reg);
}

static int vpu_core_load_firmware(struct vpu_core *core)
{
	const struct firmware *pfw = NULL;
	int ret = 0;

	WARN_ON(!core || !core->res || !core->res->fwname);
	if (!core->fw.virt) {
		core_err(core, "firmware buffer is not ready\n");
		return -EINVAL;
	}

	ret = request_firmware(&pfw, core->res->fwname, core->dev);
	core_dbg(core, LVL_DEBUG, "request_firmware %s : %d\n", core->res->fwname, ret);
	if (ret) {
		core_err(core, "request firmware %s failed, ret = %d\n",
				core->res->fwname, ret);
		return ret;
	}

	if (core->fw.length < pfw->size) {
		core_err(core, "firmware buffer size want %ld, but %d\n",
				pfw->size, core->fw.length);
		ret = -EINVAL;
		goto exit;
	}

	memset_io(core->fw.virt, 0, core->fw.length);
	memcpy(core->fw.virt, pfw->data, pfw->size);
	core->fw.bytesused = pfw->size;
	ret = vpu_iface_on_firmware_loaded(core);
exit:
	release_firmware(pfw);
	pfw = NULL;

	return ret;
}

static int vpu_core_wait_boot_done(struct vpu_core *core)
{
	int ret;
	u32 fw_version;

	ret = wait_for_completion_timeout(&core->cmp, VPU_TIMEOUT);
	if (!ret) {
		core_err(core, "boot timeout\n");
		return -EINVAL;
	}

	fw_version = vpu_iface_get_version(core);
	core_dbg(core, LVL_WARN, "firmware version : %d.%d.%d\n",
			(fw_version >> 16) & 0xff,
			(fw_version >> 8) & 0xff,
			fw_version & 0xff);
	core->supported_instance_count = vpu_iface_get_max_instance_count(core);
	if (core->res->act_size) {
		u32 count = core->act.length / core->res->act_size;

		core->supported_instance_count = min(core->supported_instance_count, count);
	}
	core->fw_version = fw_version;

	return 0;
}

static int vpu_core_boot(struct vpu_core *core, bool load)
{
	int ret;

	WARN_ON(!core);

	if (!core->res->standalone)
		return 0;

	core_dbg(core, LVL_WARN, "boot\n");
	reinit_completion(&core->cmp);
	if (load) {
		ret = vpu_core_load_firmware(core);
		if (ret)
			return ret;
	}

	vpu_iface_boot_core(core);
	return vpu_core_wait_boot_done(core);
}

static int vpu_core_shutdown(struct vpu_core *core)
{
	if (!core->res->standalone)
		return 0;
	return vpu_iface_shutdown_core(core);
}

static int vpu_core_restore(struct vpu_core *core)
{
	if (!core->res->standalone)
		return 0;
	return vpu_iface_restore_core(core);
}

static int __vpu_alloc_dma(struct device *dev, struct vpu_buffer *buf)
{
	gfp_t gfp = GFP_KERNEL | GFP_DMA32;

	WARN_ON(!dev || !buf);

	if (!buf->length)
		return 0;

	buf->virt = dma_alloc_coherent(dev, buf->length, &buf->phys, gfp);
	if (!buf->virt)
		return -ENOMEM;

	buf->dev = dev;

	return 0;
}

void vpu_free_dma(struct vpu_buffer *buf)
{
	WARN_ON(!buf);

	if (!buf->virt || !buf->dev)
		return;

	dma_free_coherent(buf->dev, buf->length, buf->virt, buf->phys);
	buf->virt = NULL;
	buf->phys = 0;
	buf->length = 0;
	buf->bytesused = 0;
	buf->dev = NULL;
}

int vpu_alloc_dma(struct vpu_core *core, struct vpu_buffer *buf)
{
	WARN_ON(!core || !buf);

	return __vpu_alloc_dma(core->dev, buf);
}

struct vpu_core *vpu_core_find_next_by_type(struct vpu_dev *vpu, u32 type)
{
	struct vpu_core *c;

	WARN_ON(!vpu);

	list_for_each_entry(c, &vpu->cores, list) {
		if (c->type == type)
			return c;
	}

	return NULL;
}

int vpu_core_check_fmt(struct vpu_core *core, u32 pixelfmt)
{
	if (!core)
		return -EINVAL;

	if (vpu_iface_check_format(core, pixelfmt))
		return 0;

	return -EINVAL;
}

static void vpu_core_check_hang(struct vpu_core *core)
{
	if (core->hang_mask)
		core->state = VPU_CORE_HANG;
}

struct vpu_core *vpu_core_find_proper_by_type(struct vpu_dev *vpu, u32 type)
{
	struct vpu_core *core = NULL;
	int request_count = INT_MAX;
	struct vpu_core *c;

	WARN_ON(!vpu);

	list_for_each_entry(c, &vpu->cores, list) {
		core_dbg(c, LVL_DEBUG, "instance_mask = 0x%lx, state = %d\n",
				c->instance_mask,
				c->state);
		if (c->type != type)
			continue;
		if (c->state == VPU_CORE_DEINIT) {
			core = c;
			break;
		}
		vpu_core_check_hang(c);
		if (c->state != VPU_CORE_ACTIVE)
			continue;
		if (c->request_count < request_count) {
			request_count = c->request_count;
			core = c;
		}
		if (!request_count)
			break;
	}

	return core;
}

static bool vpu_core_is_exist(struct vpu_dev *vpu, struct vpu_core *core)
{
	struct vpu_core *c;

	list_for_each_entry(c, &vpu->cores, list) {
		if (c == core)
			return true;
	}

	return false;
}

static void vpu_core_get_vpu(struct vpu_core *core)
{
	core->vpu->get_vpu(core->vpu);
	if (core->type == VPU_CORE_TYPE_ENC)
		core->vpu->get_enc(core->vpu);
	if (core->type == VPU_CORE_TYPE_DEC)
		core->vpu->get_dec(core->vpu);
}

int vpu_core_register(struct device *dev, struct vpu_core *core)
{
	struct vpu_dev *vpu = dev_get_drvdata(dev);
	int ret = 0;

	core_dbg(core, LVL_DEBUG, "register core\n");
	if (vpu_core_is_exist(vpu, core))
		return 0;

	core->workqueue = alloc_workqueue("vpu", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!core->workqueue) {
		core_err(core, "fail to alloc workqueue\n");
		return -ENOMEM;
	}
	INIT_WORK(&core->msg_work, vpu_msg_run_work);
	INIT_DELAYED_WORK(&core->msg_delayed_work, vpu_msg_delayed_work);
	core->msg_buffer_size = roundup_pow_of_two(VPU_MSG_BUFFER_SIZE);
	core->msg_buffer = vzalloc(core->msg_buffer_size);
	if (!core->msg_buffer) {
		core_err(core, "failed allocate buffer for fifo\n");
		ret = -ENOMEM;
		goto error;
	}
	ret = kfifo_init(&core->msg_fifo, core->msg_buffer, core->msg_buffer_size);
	if (ret) {
		core_err(core, "failed init kfifo\n");
		goto error;
	}

	list_add_tail(&core->list, &vpu->cores);

	vpu_core_get_vpu(core);

	if (core->type == VPU_CORE_TYPE_ENC && !vpu->vdev_enc)
		venc_create_video_device(vpu);
	if (core->type == VPU_CORE_TYPE_DEC && !vpu->vdev_dec)
		vdec_create_video_device(vpu);

	return 0;
error:
	if (core->msg_buffer) {
		vfree(core->msg_buffer);
		core->msg_buffer = NULL;
	}
	if (core->workqueue) {
		destroy_workqueue(core->workqueue);
		core->workqueue = NULL;
	}
	return ret;
}

static void vpu_core_put_vpu(struct vpu_core *core)
{
	if (core->type == VPU_CORE_TYPE_ENC)
		core->vpu->put_enc(core->vpu);
	if (core->type == VPU_CORE_TYPE_DEC)
		core->vpu->put_dec(core->vpu);
	core->vpu->put_vpu(core->vpu);
}

int vpu_core_unregister(struct device *dev, struct vpu_core *core)
{
	struct vpu_dev *vpu = dev_get_drvdata(dev);

	list_del_init(&core->list);

	vpu_core_put_vpu(core);
	core->vpu = NULL;
	vfree(core->msg_buffer);
	core->msg_buffer = NULL;

	if (core->workqueue) {
		cancel_work_sync(&core->msg_work);
		cancel_delayed_work_sync(&core->msg_delayed_work);
		destroy_workqueue(core->workqueue);
		core->workqueue = NULL;
	}

	if (vpu_core_find_next_by_type(vpu, core->type))
		return 0;

	if (core->type == VPU_CORE_TYPE_ENC)
		video_unregister_device(vpu->vdev_enc);
	if (core->type == VPU_CORE_TYPE_DEC)
		video_unregister_device(vpu->vdev_dec);

	return 0;
}

int vpu_core_acquire_instance(struct vpu_core *core)
{
	int id;

	WARN_ON(!core);

	id = ffz(core->instance_mask);
	if (id >= core->supported_instance_count)
		return -EINVAL;

	set_bit(id, &core->instance_mask);

	return id;
}

void vpu_core_release_instance(struct vpu_core *core, int id)
{
	WARN_ON(!core);

	if (id < 0 || id >= core->supported_instance_count)
		return;

	clear_bit(id, &core->instance_mask);
}

struct vpu_inst *vpu_inst_get(struct vpu_inst *inst)
{
	if (!inst)
		return NULL;

	atomic_inc(&inst->ref_count);

	return inst;
}

void vpu_inst_put(struct vpu_inst *inst)
{
	if (!inst)
		return;
	if (atomic_dec_and_test(&inst->ref_count)) {
		if (inst->release)
			inst->release(inst);
	}
}

struct vpu_core *vpu_request_core(struct vpu_dev *vpu, enum vpu_core_type type)
{
	struct vpu_core *core = NULL;
	int ret;

	mutex_lock(&vpu->lock);

	core = vpu_core_find_proper_by_type(vpu, type);
	if (!core)
		goto exit;

	core_dbg(core, LVL_DEBUG, "is found\n");
	mutex_lock(&core->lock);
	pm_runtime_get_sync(core->dev);

	if (core->state == VPU_CORE_DEINIT) {
		ret = vpu_core_boot(core, true);
		if (ret) {
			pm_runtime_put_sync(core->dev);
			mutex_unlock(&core->lock);
			core = NULL;
			goto exit;
		}
		core->state = VPU_CORE_ACTIVE;
	}

	core->request_count++;

	mutex_unlock(&core->lock);
exit:
	mutex_unlock(&vpu->lock);

	return core;
}

void vpu_release_core(struct vpu_core *core)
{
	if (!core)
		return;

	mutex_lock(&core->lock);
	pm_runtime_put_sync(core->dev);
	if (core->request_count)
		core->request_count--;
	mutex_unlock(&core->lock);
}

int vpu_inst_register(struct vpu_inst *inst)
{
	struct vpu_core *core;
	int ret = 0;

	WARN_ON(!inst || !inst->core);

	core = inst->core;
	mutex_lock(&core->lock);
	if (inst->id >= 0 && inst->id < core->supported_instance_count)
		goto exit;

	ret = vpu_core_acquire_instance(core);
	if (ret < 0)
		goto exit;

	inst->id = ret;
	list_add_tail(&inst->list, &core->instances);
	ret = 0;
	inst->pid = current->pid;
	inst->tgid = current->tgid;
	if (core->res->act_size) {
		inst->act.phys = core->act.phys + core->res->act_size * inst->id;
		inst->act.virt = core->act.virt + core->res->act_size * inst->id;
		inst->act.length = core->res->act_size;
	}
	vpu_inst_create_dbgfs_file(inst);
exit:
	mutex_unlock(&core->lock);

	if (ret)
		core_err(core, "register instance fail\n");
	return ret;
}

int vpu_inst_unregister(struct vpu_inst *inst)
{
	struct vpu_core *core;

	WARN_ON(!inst);

	core = inst->core;

	vpu_clear_request(inst);
	mutex_lock(&core->lock);
	if (inst->id >= 0 && inst->id < core->supported_instance_count) {
		vpu_inst_remove_dbgfs_file(inst);
		list_del_init(&inst->list);
		vpu_core_release_instance(core, inst->id);
		inst->id = VPU_INST_NULL_ID;
	}
	vpu_core_check_hang(core);
	if (core->state == VPU_CORE_HANG && !core->instance_mask) {
		core_dbg(core, LVL_WARN, "reset hang core\n");
		if (!vpu_core_sw_reset(core)) {
			core->state = VPU_CORE_ACTIVE;
			core->hang_mask = 0;
		}
	}
	mutex_unlock(&core->lock);

	return 0;
}

struct vpu_inst *vpu_core_find_instance(struct vpu_core *core, u32 index)
{
	struct vpu_inst *inst = NULL;
	struct vpu_inst *tmp;

	mutex_lock(&core->lock);
	if (!test_bit(index, &core->instance_mask))
		goto exit;
	list_for_each_entry(tmp, &core->instances, list) {
		if (tmp->id == index) {
			inst = vpu_inst_get(tmp);
			break;
		}
	}
exit:
	mutex_unlock(&core->lock);

	return inst;
}

static int vpu_core_parse_dt(struct vpu_core *core, struct device_node *np)
{
	struct device_node *node;
	struct resource res;

	if (of_count_phandle_with_args(np, "memory-region", NULL) < 2) {
		core_err(core, "need 2 memory-region for boot and rpc\n");
		return -ENODEV;
	}

	node = of_parse_phandle(np, "memory-region", 0);
	if (!node) {
		core_err(core, "boot-region of_parse_phandle error\n");
		return -ENODEV;
	}
	if (of_address_to_resource(node, 0, &res)) {
		core_err(core, "boot-region of_address_to_resource error\n");
		return -EINVAL;
	}
	core->fw.phys = res.start;
	core->fw.length = resource_size(&res);
	core_dbg(core, LVL_INFO, "boot-region : <0x%llx, 0x%llx>\n",
			res.start, resource_size(&res));

	node = of_parse_phandle(np, "memory-region", 1);
	if (!node) {
		core_err(core, "rpc-region of_parse_phandle error\n");
		return -ENODEV;
	}
	if (of_address_to_resource(node, 0, &res)) {
		core_err(core, "rpc-region of_address_to_resource error\n");
		return -EINVAL;
	}
	core->rpc.phys = res.start;
	core->rpc.length = resource_size(&res);
	core_dbg(core, LVL_DEBUG, "rpc-region : <0x%llx, 0x%llx>\n",
			res.start, resource_size(&res));

	if (core->rpc.length < core->res->rpc_size + core->res->fwlog_size) {
		core_err(core, "the rpc-region <0x%llx, 0x%llx> is not enough\n",
				res.start, resource_size(&res));
		return -EINVAL;
	}

	core->fw.virt = ioremap_wc(core->fw.phys, core->fw.length);
	core->rpc.virt = ioremap_wc(core->rpc.phys, core->rpc.length);
	memset_io(core->rpc.virt, 0, core->rpc.length);

	if (vpu_iface_check_memory_region(core,
				core->rpc.phys,
				core->rpc.length) != VPU_CORE_MEMORY_UNCACHED) {
		core_err(core, "rpc region<0x%llx, 0x%x> isn't uncached\n",
				core->rpc.phys,
				core->rpc.length);
		return -EINVAL;
	}

	core->log.phys = core->rpc.phys + core->res->rpc_size;
	core->log.virt = core->rpc.virt + core->res->rpc_size;
	core->log.length = core->res->fwlog_size;
	core->act.phys = core->log.phys + core->log.length;
	core->act.virt = core->log.virt + core->log.length;
	core->act.length = core->rpc.length - core->res->rpc_size - core->log.length;
	core->rpc.length = core->res->rpc_size;

	return 0;
}

static int vpu_core_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vpu_core *core;
	struct vpu_dev *vpu = dev_get_drvdata(dev->parent);
	struct vpu_shared_addr *iface;
	u32 iface_data_size;
	struct resource *r;
	int ret;

	vpu_dbg(LVL_WARN, "core %s probe\n", pdev->dev.of_node->name);
	if (!vpu)
		return -EINVAL;
	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->pdev = pdev;
	core->dev = dev;
	platform_set_drvdata(pdev, core);
	core->vpu = vpu;
	INIT_LIST_HEAD(&core->instances);
	mutex_init(&core->lock);
	mutex_init(&core->cmd_lock);
	init_completion(&core->cmp);
	init_waitqueue_head(&core->ack_wq);
	core->state = VPU_CORE_DEINIT;

	core->res = of_device_get_match_data(dev);
	if (!core->res)
		return -ENODEV;

	core->type = core->res->type;
	core->id = of_alias_get_id(dev->of_node, "vpu_core");
	if (core->id < 0) {
		vpu_err("can't get vpu core id\n");
		return core->id;
	}
	ret = vpu_core_parse_dt(core, dev->of_node);
	if (ret)
		return ret;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		core_err(core, "fail to get core reg\n");
		return -EINVAL;
	}
	core->base = devm_ioremap_resource(dev, r);
	if (IS_ERR(core->base))
		return PTR_ERR(core->base);
	core_dbg(core, LVL_WARN, "reg : <0x%llx, 0x%llx>\n", r->start, resource_size(r));

	if (!vpu_iface_check_codec(core)) {
		core_err(core, "is not supported\n");
		return -EINVAL;
	}

	ret = vpu_mbox_init(core);
	if (ret)
		return ret;

	iface = devm_kzalloc(dev, sizeof(*iface), GFP_KERNEL);
	if (!iface)
		return -ENOMEM;

	iface_data_size = vpu_iface_get_data_size(core);
	if (iface_data_size) {
		iface->priv = devm_kzalloc(dev, iface_data_size, GFP_KERNEL);
		if (!iface->priv)
			return -ENOMEM;
	}

	ret = vpu_iface_init(core, iface, &core->rpc, core->fw.phys);
	if (ret) {
		core_err(core, "init iface fail, ret = %d\n", ret);
		return ret;
	}

	vpu_iface_config_system(core, vpu->res->mreg_base, vpu->base);
	vpu_iface_set_log_buf(core, &core->log);

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret) {
		pm_runtime_put_noidle(dev);
		pm_runtime_set_suspended(dev);
		goto err_runtime_disable;
	}

	if (vpu_iface_get_power_state(core))
		ret = vpu_core_restore(core);
	if (ret)
		goto err_core_boot;

	ret = vpu_core_register(dev->parent, core);
	if (ret)
		goto err_core_register;
	core->parent = dev->parent;

	pm_runtime_put_sync(dev);
	vpu_core_create_dbgfs_file(core);

	return 0;

err_core_register:
	vpu_core_shutdown(core);
err_core_boot:
	pm_runtime_put_sync(dev);
err_runtime_disable:
	pm_runtime_disable(dev);

	return ret;
}

static int vpu_core_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vpu_core *core = platform_get_drvdata(pdev);
	int ret;

	vpu_core_remove_dbgfs_file(core);
	ret = pm_runtime_get_sync(dev);
	WARN_ON(ret < 0);

	vpu_core_shutdown(core);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	vpu_core_unregister(core->parent, core);
	iounmap(core->fw.virt);
	iounmap(core->rpc.virt);
	mutex_destroy(&core->lock);
	mutex_destroy(&core->cmd_lock);

	return 0;
}

static int __maybe_unused vpu_core_runtime_resume(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);

	return vpu_mbox_request(core);
}

static int __maybe_unused vpu_core_runtime_suspend(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);

	vpu_mbox_free(core);
	return 0;
}

static void vpu_core_cancel_work(struct vpu_core *core)
{
	struct vpu_inst *inst = NULL;

	cancel_work_sync(&core->msg_work);
	cancel_delayed_work_sync(&core->msg_delayed_work);

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list)
		cancel_work_sync(&inst->msg_work);
	mutex_unlock(&core->lock);
}

static void vpu_core_resume_work(struct vpu_core *core)
{
	struct vpu_inst *inst = NULL;
	unsigned long delay = msecs_to_jiffies(10);

	queue_work(core->workqueue, &core->msg_work);
	queue_delayed_work(core->workqueue, &core->msg_delayed_work, delay);

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list)
		queue_work(inst->workqueue, &inst->msg_work);
	mutex_unlock(&core->lock);
}

static int __maybe_unused vpu_core_resume(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);
	int ret = 0;

	if (!core->res->standalone)
		return 0;

	mutex_lock(&core->lock);
	pm_runtime_get_sync(dev);
	vpu_core_get_vpu(core);
	if (core->state != VPU_CORE_SNAPSHOT)
		goto exit;

	if (!vpu_iface_get_power_state(core)) {
		if (!list_empty(&core->instances)) {
			ret = vpu_core_boot(core, false);
			if (ret) {
				core_err(core, "%s boot fail\n", __func__);
				core->state = VPU_CORE_DEINIT;
				goto exit;
			}
			core->state = VPU_CORE_ACTIVE;
		} else {
			core->state = VPU_CORE_DEINIT;
		}
	} else {
		if (!list_empty(&core->instances)) {
			ret = vpu_core_sw_reset(core);
			if (ret) {
				core_err(core, "%s sw_reset fail\n", __func__);
				core->state = VPU_CORE_HANG;
				goto exit;
			}
		}
		core->state = VPU_CORE_ACTIVE;
	}

exit:
	pm_runtime_put_sync(dev);
	mutex_unlock(&core->lock);

	vpu_core_resume_work(core);
	return ret;
}

static int __maybe_unused vpu_core_suspend(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);
	int ret = 0;

	if (!core->res->standalone)
		return 0;

	mutex_lock(&core->lock);
	if (core->state == VPU_CORE_ACTIVE) {
		if (!list_empty(&core->instances)) {
			ret = vpu_core_snapshot(core);
			if (ret) {
				mutex_unlock(&core->lock);
				return ret;
			}
		}

		core->state = VPU_CORE_SNAPSHOT;
	}
	mutex_unlock(&core->lock);

	vpu_core_cancel_work(core);

	mutex_lock(&core->lock);
	vpu_core_put_vpu(core);
	mutex_unlock(&core->lock);
	return ret;
}

static const struct dev_pm_ops vpu_core_pm_ops = {
	SET_RUNTIME_PM_OPS(vpu_core_runtime_suspend, vpu_core_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(vpu_core_suspend, vpu_core_resume)
};

static struct vpu_core_resources imx8q_enc = {
	.type = VPU_CORE_TYPE_ENC,
	.fwname = "vpu/vpu_fw_imx8_enc.bin",
	.stride = 16,
	.max_width = 1920,
	.max_height = 1920,
	.min_width = 64,
	.min_height = 48,
	.step_width = 2,
	.step_height = 2,
	.rpc_size = 0x80000,
	.fwlog_size = 0x80000,
	.act_size = 0xc0000,
	.standalone = true,
};

static struct vpu_core_resources imx8q_dec = {
	.type = VPU_CORE_TYPE_DEC,
	.fwname = "vpu/vpu_fw_imx8_dec.bin",
	.stride = 256,
	.max_width = 8188,
	.max_height = 8188,
	.min_width = 16,
	.min_height = 16,
	.step_width = 1,
	.step_height = 1,
	.rpc_size = 0x80000,
	.fwlog_size = 0x80000,
	.standalone = true,
};

static const struct of_device_id vpu_core_dt_match[] = {
	{ .compatible = "nxp,imx8q-vpu-encoder", .data = &imx8q_enc },
	{ .compatible = "nxp,imx8q-vpu-decoder", .data = &imx8q_dec },
	{}
};
MODULE_DEVICE_TABLE(of, vpu_core_dt_match);

static struct platform_driver imx_vpu_core_driver = {
	.probe = vpu_core_probe,
	.remove = vpu_core_remove,
	.driver = {
		.name = "imx-vpu-core",
		.of_match_table = vpu_core_dt_match,
		.pm = &vpu_core_pm_ops,
	},
};
module_platform_driver(imx_vpu_core_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Linux VPU driver for Freescale i.MX/MXC");
MODULE_LICENSE("GPL v2");
