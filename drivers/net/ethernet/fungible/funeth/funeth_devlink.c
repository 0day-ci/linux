// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

#include <linux/firmware.h>
#include <linux/pci.h>

#include "funeth.h"
#include "funeth_devlink.h"

/* max length of scatter-gather list used to pass FW image data to the device */
#define FUN_FW_SGL_LEN ((ADMIN_SQE_SIZE - sizeof(struct fun_admin_swu_req)) / \
			sizeof(struct fun_subop_sgl))

/* size of each DMA buffer that is part of the above SGL */
#define FUN_FW_SGL_BUF_LEN 65536U

struct fun_fw_buf {
	void *vaddr;
	dma_addr_t dma_addr;
	unsigned int data_len;
};

/* Start or commit the FW update for the given component with a FW image of
 * size @img_size.
 */
static int fun_fw_update_one(struct fun_dev *fdev, unsigned int handle,
			     unsigned int comp_id, unsigned int flags,
			     unsigned int img_size)
{
	union {
		struct fun_admin_swu_req req;
		struct fun_admin_swu_rsp rsp;
	} cmd;
	int rc;

	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_SWUPGRADE,
						    sizeof(cmd.req));
	cmd.req.u.upgrade =
		FUN_ADMIN_SWU_UPGRADE_REQ_INIT(FUN_ADMIN_SWU_SUBOP_UPGRADE,
					       flags, handle, comp_id,
					       img_size);
	rc = fun_submit_admin_sync_cmd(fdev, &cmd.req.common, &cmd.rsp,
				       sizeof(cmd.rsp), 0);
	if (!rc)
		rc = -be32_to_cpu(cmd.rsp.u.upgrade.status);
	return rc;
}

/* DMA a gather list of FW image data starting at @offset to the device's FW
 * staging area.
 */
static int fun_fw_write(struct fun_dev *fdev, unsigned int handle,
			unsigned int offset, unsigned int nsgl,
			const struct fun_fw_buf *bufs)
{
	unsigned int cmd_sz, total_data_len = 0, i;
	union {
		struct fun_admin_swu_req req;
		u8 v[ADMIN_SQE_SIZE];
	} cmd;

	cmd_sz = struct_size(&cmd.req, sgl, nsgl);
	if (cmd_sz > sizeof(cmd))
		return -EINVAL;

	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_SWUPGRADE,
						    cmd_sz);

	for (i = 0; i < nsgl; i++, bufs++) {
		total_data_len += bufs->data_len;
		cmd.req.sgl[i] = FUN_SUBOP_SGL_INIT(FUN_DATAOP_GL, 0,
						    i ? 0 : nsgl,
						    bufs->data_len,
						    bufs->dma_addr);
	}

	cmd.req.u.upgrade_data =
		FUN_ADMIN_SWU_UPGRADE_DATA_REQ_INIT(FUN_ADMIN_SWU_SUBOP_UPGRADE_DATA,
						    0, handle, offset,
						    total_data_len);
	return fun_submit_admin_sync_cmd(fdev, &cmd.req.common, NULL, 0, 0);
}

/* Convert a FW component string into a component ID.
 * Component names are 4 characters long.
 */
static unsigned int fw_component_id(const char *component)
{
	if (strlen(component) != 4)
		return 0;

	return component[0] | (component[1] << 8) | (component[2] << 16) |
	       (component[3] << 24);
}

/* Allocate the SG buffers for the DMA transfer of the FW image of the
 * given size. We allocate up to the max SG length supported by the device.
 * Return success if at least 1 buffer is allocated.
 */
static int fun_init_fw_dma_bufs(struct device *dev, struct fun_fw_buf *bufs,
				size_t fw_size)
{
	unsigned int i, nbufs;

	nbufs = min(FUN_FW_SGL_LEN, DIV_ROUND_UP(fw_size, FUN_FW_SGL_BUF_LEN));
	for (i = 0; i < nbufs; i++, bufs++) {
		bufs->vaddr = dma_alloc_coherent(dev, FUN_FW_SGL_BUF_LEN,
						 &bufs->dma_addr, GFP_KERNEL);
		if (!bufs->vaddr) {
			if (!i)
				return -ENOMEM;
			break;
		}
	}
	return i;
}

static void fun_free_fw_bufs(struct device *dev, struct fun_fw_buf *bufs,
			     unsigned int nbufs)
{
	for ( ; nbufs; nbufs--, bufs++)
		dma_free_coherent(dev, FUN_FW_SGL_BUF_LEN, bufs->vaddr,
				  bufs->dma_addr);
}

/* Scatter the FW data starting at @offset into the @nbufs DMA buffers.
 * Return the new offset into the FW image.
 */
static unsigned int fun_fw_scatter(struct fun_fw_buf *bufs, unsigned int nbufs,
				   const struct firmware *fw,
				   unsigned int offset)
{
	for ( ; nbufs && offset < fw->size; nbufs--, bufs++) {
		bufs->data_len = min_t(unsigned int, fw->size - offset,
				       FUN_FW_SGL_BUF_LEN);
		memcpy(bufs->vaddr, fw->data + offset, bufs->data_len);
		offset += bufs->data_len;
	}
	return offset;
}

static int fun_dl_flash_update(struct devlink *devlink,
			       struct devlink_flash_update_params *params,
			       struct netlink_ext_ack *extack)
{
	unsigned int comp_id, update_flags, nbufs, offset;
	struct fun_dev *fdev = devlink_priv(devlink);
	const char *component = params->component;
	struct fun_fw_buf bufs[FUN_FW_SGL_LEN];
	const struct firmware *fw;
	int err;

	if (!to_pci_dev(fdev->dev)->is_physfn)
		return -EOPNOTSUPP;

	if (!component) {
		NL_SET_ERR_MSG_MOD(extack, "must specify FW component");
		return -EINVAL;
	}

	comp_id = fw_component_id(component);
	if (!comp_id) {
		NL_SET_ERR_MSG_MOD(extack, "bad FW component");
		return -EINVAL;
	}

	err = fun_get_fw_handle(fdev);
	if (err < 0) {
		NL_SET_ERR_MSG_MOD(extack, "can't create FW update handle");
		return err;
	}

	fw = params->fw;

	err = fun_init_fw_dma_bufs(fdev->dev, bufs, fw->size);
	if (err < 0) {
		NL_SET_ERR_MSG_MOD(extack, "unable to create FW DMA SGL");
		return err;
	}
	nbufs = err;

	devlink_flash_update_status_notify(devlink, "Preparing to flash",
					   component, 0, 1);

	err = fun_fw_update_one(fdev, fdev->fw_handle, comp_id,
				FUN_ADMIN_SWU_UPGRADE_FLAG_INIT, fw->size);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "unable to create device staging area for FW image");
		goto free_bufs;
	}

	devlink_flash_update_status_notify(devlink, "Preparing to flash",
					   component, 1, 1);

	/* Write FW to the device staging area, in chunks if needed. */
	for (offset = 0; offset < fw->size; ) {
		unsigned int new_offset, nsgl;

		new_offset = fun_fw_scatter(bufs, nbufs, fw, offset);
		nsgl = DIV_ROUND_UP(new_offset - offset, FUN_FW_SGL_BUF_LEN);
		devlink_flash_update_status_notify(devlink, "Staging FW",
						   component, offset, fw->size);
		err = fun_fw_write(fdev, fdev->fw_handle, offset, nsgl, bufs);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "error staging FW image");
			goto free_bufs;
		}
		offset = new_offset;
	}
	devlink_flash_update_status_notify(devlink, "Staging FW",
					   component, offset, fw->size);

	update_flags = FUN_ADMIN_SWU_UPGRADE_FLAG_COMPLETE |
		       FUN_ADMIN_SWU_UPGRADE_FLAG_DOWNGRADE;
	err = fun_fw_update_one(fdev, fdev->fw_handle, comp_id, update_flags,
				fw->size);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "unable to commit FW update");
		devlink_flash_update_status_notify(devlink, "FW update failed",
						   component, 0, fw->size);
	} else {
		devlink_flash_update_status_notify(devlink, "FW updated",
						   component, fw->size,
						   fw->size);
	}

free_bufs:
	fun_free_fw_bufs(fdev->dev, bufs, nbufs);
	return err;
}

static int fun_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
			   struct netlink_ext_ack *extack)
{
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;

	err = devlink_info_version_fixed_put(req,
			DEVLINK_INFO_VERSION_GENERIC_BOARD_MANUFACTURE,
			"Fungible");
	if (err)
		return err;

	return 0;
}

static const struct devlink_ops fun_dl_ops = {
	.info_get       = fun_dl_info_get,
	.flash_update   = fun_dl_flash_update,
};

struct devlink *fun_devlink_alloc(struct device *dev)
{
	return devlink_alloc(&fun_dl_ops, sizeof(struct fun_ethdev), dev);
}

void fun_devlink_free(struct devlink *devlink)
{
	devlink_free(devlink);
}

void fun_devlink_register(struct devlink *devlink)
{
	devlink_register(devlink);
}

void fun_devlink_unregister(struct devlink *devlink)
{
	devlink_unregister(devlink);
}
