// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/printk.h>

#include "bootimage.h"
#include "device.h"
#include "hostres.h"
#include "ipc_protocol.h"
#include "nnp_boot_defs.h"

#define MAX_IMAGE_NAME_LEN   (NAME_MAX + 1)

void nnpdev_boot_image_init(struct image_info *boot_image)
{
	boot_image->state = IMAGE_NONE;
	boot_image->hostres = NULL;
	mutex_init(&boot_image->mutex);
}

static int load_firmware(struct image_info *image_info)
{
	const struct firmware *fw;
	struct nnp_device *nnpdev = container_of(image_info, struct nnp_device,
						 boot_image);
	struct device *dev = nnpdev->dev;
	struct kstat stat;
	struct path path;
	static const char *fname = "/lib/firmware/" NNP_FIRMWARE_NAME;
	void *vptr;
	int ret;

	lockdep_assert_held(&image_info->mutex);

	/*
	 * find image file size
	 *
	 * NOTE: we look for the file under a constant path "/lib/firmware"
	 * since it works and accepted on all platforms that NNP-I device
	 * can be installed.
	 * A better solution would be to look at the same paths that the
	 * firmware API will search however the firmware API does not
	 * export any function to do the search and there is no point
	 * duplicating it here.
	 */
	ret = kern_path(fname, LOOKUP_FOLLOW, &path);
	if (ret) {
		pr_err("Could not find image under /lib/firmware\n");
		return ret;
	}

	ret = vfs_getattr(&path, &stat, STATX_SIZE, 0);
	path_put(&path);
	if (ret) {
		pr_err("Failed to get file size for %s error=%d\n", fname, ret);
		return ret;
	}

	/* create host resource to hold the boot image content */
	image_info->hostres = nnp_hostres_alloc(stat.size, DMA_TO_DEVICE);
	if (IS_ERR(image_info->hostres))
		return PTR_ERR(image_info->hostres);

	vptr = nnp_hostres_vptr(image_info->hostres);

	/*
	 * load the image into the host resource.
	 * We load directly to pre-allocated host resource memory
	 * in order to prevent caching of the boot image inside
	 * firmware API
	 */
	ret = request_firmware_into_buf(&fw, NNP_FIRMWARE_NAME, dev, vptr,
					stat.size);
	if (ret) {
		pr_err("failed to load firmware %s ret=%d\n", fname, ret);
		nnp_hostres_put(image_info->hostres);
		image_info->hostres = NULL;
		return ret;
	}

	release_firmware(fw);
	image_info->state = IMAGE_AVAILABLE;

	return 0;
}

static void load_image_handler(struct work_struct *work)
{
	struct image_info *image_info = container_of(work, struct image_info,
						     work);
	struct nnp_device *nnpdev = container_of(image_info, struct nnp_device,
						 boot_image);
	dma_addr_t page_list_addr;
	unsigned int total_chunks;
	unsigned int image_size;
	u64 cmd[3];
	u32 val;
	int ret;

	mutex_lock(&image_info->mutex);

	/* do not load if image load request has canceled */
	if (image_info->state != IMAGE_REQUESTED) {
		mutex_unlock(&image_info->mutex);
		return;
	}

	/* load boot image from disk */
	ret = load_firmware(image_info);
	if (ret) {
		image_info->state = IMAGE_LOAD_FAILED;
		goto fail;
	}

	/* map image to the device */
	image_info->hostres_map = nnp_hostres_map_device(image_info->hostres,
							 nnpdev, true,
							 &page_list_addr,
							 &total_chunks);
	if (IS_ERR(image_info->hostres_map)) {
		nnp_hostres_put(image_info->hostres);
		image_info->hostres = NULL;
		image_info->state = IMAGE_NONE;
		goto fail;
	}

	mutex_unlock(&image_info->mutex);

	image_size = (unsigned int)nnp_hostres_size(image_info->hostres);

	/* image successfully mapped - send it to the device to boot */
	dev_dbg(nnpdev->dev,
		"Mapped boot image num_chunks=%u total_size=%u\n", total_chunks,
		image_size);

	/* write image address directly to the command Q */
	cmd[0] = FIELD_PREP(NNP_H2C_BOOT_IMAGE_READY_QW0_OP_MASK,
			    NNP_IPC_H2C_OP_BIOS_PROTOCOL);
	cmd[0] |= FIELD_PREP(NNP_H2C_BOOT_IMAGE_READY_QW0_TYPE_MASK,
			     NNP_IPC_H2C_TYPE_BOOT_IMAGE_READY);
	cmd[0] |= FIELD_PREP(NNP_H2C_BOOT_IMAGE_READY_QW0_SIZE_MASK,
			     2 * sizeof(u64));

	cmd[1] = (u64)page_list_addr + sizeof(struct nnp_dma_chain_header);

	cmd[2] = FIELD_PREP(NNP_H2C_BOOT_IMAGE_READY_QW2_DESC_SIZE_MASK,
			    total_chunks * sizeof(struct nnp_dma_chain_entry));
	cmd[2] |= FIELD_PREP(NNP_H2C_BOOT_IMAGE_READY_QW2_IMAGE_SIZE_MASK,
			     image_size);

	nnpdev->ops->cmdq_write_mesg(nnpdev, cmd, 3);
	return;

fail:
	/* notify card that boot image cannot be loaded */
	val = FIELD_PREP(NNP_HOST_ERROR_MASK,
			 NNP_HOST_ERROR_CANNOT_LOAD_IMAGE);
	nnpdev->ops->set_host_doorbell_value(nnpdev, val);
	mutex_unlock(&image_info->mutex);
}

/**
 * nnpdev_load_boot_image() - load boot image and send it to device
 * @nnpdev: the device requested the image
 *
 * This function starts the flow of loading a boot image and map it to the
 * requesting device. It will launch a work to load the boot image.
 * It is an error to call this function if boot image load for the same
 * device is already in progress.
 *
 * Return:
 * * 0       - boot image was successfully loaded, mapped and sent to the device.
 * * -EINVAL - image load is already in progress
 */
int nnpdev_load_boot_image(struct nnp_device *nnpdev)
{
	struct image_info *image_info = &nnpdev->boot_image;

	/* check if the image is already loaded or in progress */
	mutex_lock(&image_info->mutex);
	if (image_info->state != IMAGE_NONE) {
		mutex_unlock(&image_info->mutex);
		return -EINVAL;
	}

	/* initialize image load request */
	image_info->state = IMAGE_REQUESTED;
	mutex_unlock(&image_info->mutex);
	INIT_WORK(&image_info->work, load_image_handler);

	/* schedule work to load the image */
	schedule_work(&image_info->work);

	return 0;
}

/**
 * nnpdev_unload_boot_image() - unmaps boot image for device
 * @nnpdev: the device
 *
 * This function is called when the device no longer need the boot image
 * in memory. either because it was already copied to the device or when
 * the device is removed during the image load request is in progress.
 * The function unmaps the device from the host resource.
 *
 * Return: error code or zero.
 */
int nnpdev_unload_boot_image(struct nnp_device *nnpdev)
{
	struct image_info *image_info = &nnpdev->boot_image;
	int ret = 0;

	mutex_lock(&image_info->mutex);
	switch (image_info->state) {
	case IMAGE_NONE:
		ret = -EINVAL;
		goto done;
	case IMAGE_REQUESTED:
		image_info->state = IMAGE_NONE;
		mutex_unlock(&image_info->mutex);
		cancel_work_sync(&image_info->work);
		return 0;
	case IMAGE_LOAD_FAILED:
	case IMAGE_AVAILABLE:
		break;
	}

	if (image_info->hostres) {
		nnp_hostres_unmap_device(image_info->hostres_map);
		nnp_hostres_put(image_info->hostres);
		image_info->hostres = NULL;
	}

	image_info->state = IMAGE_NONE;

done:
	mutex_unlock(&image_info->mutex);
	return ret;
}
