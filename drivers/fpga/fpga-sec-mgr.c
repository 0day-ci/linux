// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Security Manager
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/fpga/fpga-sec-mgr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define SEC_MGR_XA_LIMIT	XA_LIMIT(0, INT_MAX)
static DEFINE_XARRAY_ALLOC(fpga_sec_mgr_xa);

static struct class *fpga_sec_mgr_class;

#define to_sec_mgr(d) container_of(d, struct fpga_sec_mgr, dev)

static void update_progress(struct fpga_sec_mgr *smgr,
			    enum fpga_sec_prog new_progress)
{
	smgr->progress = new_progress;
	sysfs_notify(&smgr->dev.kobj, "update", "status");
}

static void fpga_sec_set_error(struct fpga_sec_mgr *smgr, enum fpga_sec_err err_code)
{
	smgr->err_state = smgr->progress;
	smgr->err_code = err_code;
}

static void fpga_sec_dev_error(struct fpga_sec_mgr *smgr,
			       enum fpga_sec_err err_code)
{
	fpga_sec_set_error(smgr, err_code);
	smgr->sops->cancel(smgr);
}

static int progress_transition(struct fpga_sec_mgr *smgr,
			       enum fpga_sec_prog new_progress)
{
	int ret = 0;

	mutex_lock(&smgr->lock);
	if (smgr->request_cancel) {
		fpga_sec_set_error(smgr, FPGA_SEC_ERR_CANCELED);
		smgr->sops->cancel(smgr);
		ret = -ECANCELED;
	} else {
		update_progress(smgr, new_progress);
	}
	mutex_unlock(&smgr->lock);
	return ret;
}

static void progress_complete(struct fpga_sec_mgr *smgr)
{
	mutex_lock(&smgr->lock);
	update_progress(smgr, FPGA_SEC_PROG_IDLE);
	complete_all(&smgr->update_done);
	mutex_unlock(&smgr->lock);
}

static void fpga_sec_mgr_update(struct work_struct *work)
{
	struct fpga_sec_mgr *smgr;
	const struct firmware *fw;
	enum fpga_sec_err ret;
	u32 offset = 0;

	smgr = container_of(work, struct fpga_sec_mgr, work);

	get_device(&smgr->dev);
	if (request_firmware(&fw, smgr->filename, &smgr->dev)) {
		fpga_sec_set_error(smgr, FPGA_SEC_ERR_FILE_READ);
		goto idle_exit;
	}

	smgr->data = fw->data;
	smgr->remaining_size = fw->size;

	if (!try_module_get(smgr->dev.parent->driver->owner)) {
		fpga_sec_set_error(smgr, FPGA_SEC_ERR_BUSY);
		goto release_fw_exit;
	}

	if (progress_transition(smgr, FPGA_SEC_PROG_PREPARING))
		goto modput_exit;

	ret = smgr->sops->prepare(smgr);
	if (ret != FPGA_SEC_ERR_NONE) {
		fpga_sec_dev_error(smgr, ret);
		goto modput_exit;
	}

	if (progress_transition(smgr, FPGA_SEC_PROG_WRITING))
		goto done;

	while (smgr->remaining_size && !smgr->request_cancel) {
		ret = smgr->sops->write_blk(smgr, offset);
		if (ret != FPGA_SEC_ERR_NONE) {
			fpga_sec_dev_error(smgr, ret);
			goto done;
		}

		offset = fw->size - smgr->remaining_size;
	}

	if (progress_transition(smgr, FPGA_SEC_PROG_PROGRAMMING))
		goto done;

	ret = smgr->sops->poll_complete(smgr);
	if (ret != FPGA_SEC_ERR_NONE)
		fpga_sec_dev_error(smgr, ret);

done:
	if (smgr->sops->cleanup)
		smgr->sops->cleanup(smgr);

modput_exit:
	module_put(smgr->dev.parent->driver->owner);

release_fw_exit:
	smgr->data = NULL;
	release_firmware(fw);

idle_exit:
	/*
	 * Note: smgr->remaining_size is left unmodified here to
	 * provide additional information on errors. It will be
	 * reinitialized when the next secure update begins.
	 */
	kfree(smgr->filename);
	smgr->filename = NULL;
	put_device(&smgr->dev);
	progress_complete(smgr);
}

static const char * const sec_mgr_prog_str[] = {
	[FPGA_SEC_PROG_IDLE]	    = "idle",
	[FPGA_SEC_PROG_READING]	    = "reading",
	[FPGA_SEC_PROG_PREPARING]   = "preparing",
	[FPGA_SEC_PROG_WRITING]	    = "writing",
	[FPGA_SEC_PROG_PROGRAMMING] = "programming"
};

static const char * const sec_mgr_err_str[] = {
	[FPGA_SEC_ERR_NONE]	    = "none",
	[FPGA_SEC_ERR_HW_ERROR]	    = "hw-error",
	[FPGA_SEC_ERR_TIMEOUT]	    = "timeout",
	[FPGA_SEC_ERR_CANCELED]	    = "user-abort",
	[FPGA_SEC_ERR_BUSY]	    = "device-busy",
	[FPGA_SEC_ERR_INVALID_SIZE] = "invalid-file-size",
	[FPGA_SEC_ERR_RW_ERROR]	    = "read-write-error",
	[FPGA_SEC_ERR_WEAROUT]	    = "flash-wearout",
	[FPGA_SEC_ERR_FILE_READ]    = "file-read-error"
};

static const char *sec_progress(struct device *dev, enum fpga_sec_prog prog)
{
	const char *status = "unknown-status";

	if (prog < FPGA_SEC_PROG_MAX)
		status = sec_mgr_prog_str[prog];
	else
		dev_err(dev, "Invalid status during secure update: %d\n",
			prog);

	return status;
}

static const char *sec_error(struct device *dev, enum fpga_sec_err err_code)
{
	const char *error = "unknown-error";

	if (err_code < FPGA_SEC_ERR_MAX)
		error = sec_mgr_err_str[err_code];
	else
		dev_err(dev, "Invalid error code during secure update: %d\n",
			err_code);

	return error;
}

static ssize_t
status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fpga_sec_mgr *smgr = to_sec_mgr(dev);

	return sysfs_emit(buf, "%s\n", sec_progress(dev, smgr->progress));
}
static DEVICE_ATTR_RO(status);

static ssize_t
error_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fpga_sec_mgr *smgr = to_sec_mgr(dev);
	int ret;

	mutex_lock(&smgr->lock);

	if (smgr->progress != FPGA_SEC_PROG_IDLE)
		ret = -EBUSY;
	else if (!smgr->err_code)
		ret = 0;
	else
		ret = sysfs_emit(buf, "%s:%s\n",
				 sec_progress(dev, smgr->err_state),
				 sec_error(dev, smgr->err_code));

	mutex_unlock(&smgr->lock);

	return ret;
}
static DEVICE_ATTR_RO(error);

static ssize_t remaining_size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct fpga_sec_mgr *smgr = to_sec_mgr(dev);

	return sysfs_emit(buf, "%u\n", smgr->remaining_size);
}
static DEVICE_ATTR_RO(remaining_size);

static ssize_t filename_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fpga_sec_mgr *smgr = to_sec_mgr(dev);
	int ret = count;

	if (!count || count >= PATH_MAX)
		return -EINVAL;

	mutex_lock(&smgr->lock);
	if (smgr->driver_unload || smgr->progress != FPGA_SEC_PROG_IDLE) {
		ret = -EBUSY;
		goto unlock_exit;
	}

	smgr->filename = kmemdup_nul(buf, count, GFP_KERNEL);
	if (!smgr->filename) {
		ret = -ENOMEM;
		goto unlock_exit;
	}

	smgr->err_code = FPGA_SEC_ERR_NONE;
	smgr->request_cancel = false;
	smgr->progress = FPGA_SEC_PROG_READING;
	reinit_completion(&smgr->update_done);
	schedule_work(&smgr->work);

unlock_exit:
	mutex_unlock(&smgr->lock);
	return ret;
}
static DEVICE_ATTR_WO(filename);

static ssize_t cancel_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fpga_sec_mgr *smgr = to_sec_mgr(dev);
	bool cancel;
	int ret = count;

	if (kstrtobool(buf, &cancel) || !cancel)
		return -EINVAL;

	mutex_lock(&smgr->lock);
	if (smgr->progress == FPGA_SEC_PROG_PROGRAMMING)
		ret = -EBUSY;
	else if (smgr->progress == FPGA_SEC_PROG_IDLE)
		ret = -ENODEV;
	else
		smgr->request_cancel = true;
	mutex_unlock(&smgr->lock);

	return ret;
}
static DEVICE_ATTR_WO(cancel);

static struct attribute *sec_mgr_update_attrs[] = {
	&dev_attr_filename.attr,
	&dev_attr_cancel.attr,
	&dev_attr_status.attr,
	&dev_attr_error.attr,
	&dev_attr_remaining_size.attr,
	NULL,
};

static struct attribute_group sec_mgr_update_attr_group = {
	.name = "update",
	.attrs = sec_mgr_update_attrs,
};

static const struct attribute_group *fpga_sec_mgr_attr_groups[] = {
	&sec_mgr_update_attr_group,
	NULL,
};

/**
 * fpga_sec_mgr_register - create and register an FPGA
 *			   Security Manager device
 *
 * @dev:  fpga security manager device from pdev
 * @sops: pointer to a structure of fpga callback functions
 * @priv: fpga security manager private data
 *
 * Returns a struct fpga_sec_mgr pointer on success, or ERR_PTR() on
 * error. The caller of this function is responsible for calling
 * fpga_sec_mgr_unregister().
 */
struct fpga_sec_mgr *
fpga_sec_mgr_register(struct device *parent,
		      const struct fpga_sec_mgr_ops *sops, void *priv)
{
	struct fpga_sec_mgr *smgr;
	int id, ret;

	if (!sops || !sops->cancel || !sops->prepare ||
	    !sops->write_blk || !sops->poll_complete) {
		dev_err(parent, "Attempt to register without all required ops\n");
		return NULL;
	}

	smgr = kzalloc(sizeof(*smgr), GFP_KERNEL);
	if (!smgr)
		return NULL;

	ret = xa_alloc(&fpga_sec_mgr_xa, &smgr->dev.id, smgr, SEC_MGR_XA_LIMIT,
		       GFP_KERNEL);
	if (ret)
		goto error_kfree;

	mutex_init(&smgr->lock);

	smgr->priv = priv;
	smgr->sops = sops;
	smgr->err_code = FPGA_SEC_ERR_NONE;
	smgr->progress = FPGA_SEC_PROG_IDLE;
	init_completion(&smgr->update_done);
	INIT_WORK(&smgr->work, fpga_sec_mgr_update);

	smgr->dev.class = fpga_sec_mgr_class;
	smgr->dev.parent = parent;

	ret = dev_set_name(&smgr->dev, "fpga_sec%d", id);
	if (ret) {
		dev_err(parent, "Failed to set device name: fpga_sec%d\n", id);
		goto error_device;
	}

	ret = device_register(&smgr->dev);
	if (ret) {
		put_device(&smgr->dev);
		return ERR_PTR(ret);
	}

	return smgr;

error_device:
	xa_erase(&fpga_sec_mgr_xa, smgr->dev.id);

error_kfree:
	kfree(smgr);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(fpga_sec_mgr_register);

/**
 * fpga_sec_mgr_unregister - unregister an FPGA security manager
 *
 * @mgr: fpga manager struct
 *
 * This function is intended for use in an FPGA security manager
 * driver's remove() function.
 *
 * For some devices, once the secure update has begun authentication
 * the hardware cannot be signaled to stop, and the driver will not
 * exit until the hardware signals completion.  This could be 30+
 * minutes of waiting. The driver_unload flag enables a force-unload
 * of the driver (e.g. modprobe -r) by signaling the parent driver to
 * exit even if the hardware update is incomplete. The driver_unload
 * flag also prevents new updates from starting once the unregister
 * process has begun.
 */
void fpga_sec_mgr_unregister(struct fpga_sec_mgr *smgr)
{
	mutex_lock(&smgr->lock);
	smgr->driver_unload = true;
	if (smgr->progress == FPGA_SEC_PROG_IDLE) {
		mutex_unlock(&smgr->lock);
		goto unregister;
	}

	if (smgr->progress != FPGA_SEC_PROG_PROGRAMMING)
		smgr->request_cancel = true;

	mutex_unlock(&smgr->lock);
	wait_for_completion(&smgr->update_done);

unregister:
	device_unregister(&smgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_sec_mgr_unregister);

static void fpga_sec_mgr_dev_release(struct device *dev)
{
	struct fpga_sec_mgr *smgr = to_sec_mgr(dev);

	xa_erase(&fpga_sec_mgr_xa, smgr->dev.id);
	kfree(smgr);
}

static int __init fpga_sec_mgr_class_init(void)
{
	pr_info("FPGA Security Manager\n");

	fpga_sec_mgr_class = class_create(THIS_MODULE, "fpga_sec_mgr");
	if (IS_ERR(fpga_sec_mgr_class))
		return PTR_ERR(fpga_sec_mgr_class);

	fpga_sec_mgr_class->dev_groups = fpga_sec_mgr_attr_groups;
	fpga_sec_mgr_class->dev_release = fpga_sec_mgr_dev_release;

	return 0;
}

static void __exit fpga_sec_mgr_class_exit(void)
{
	class_destroy(fpga_sec_mgr_class);
	WARN_ON(!xa_empty(&fpga_sec_mgr_xa));
}

MODULE_DESCRIPTION("FPGA Security Manager Driver");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_sec_mgr_class_init);
module_exit(fpga_sec_mgr_class_exit)
