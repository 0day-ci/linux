// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Software Defined Silicon driver
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: "David E. Box" <david.e.box@linux.intel.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <uapi/linux/sdsi_if.h>

#include "extended_caps.h"

#define ACCESS_TYPE_BARID		2
#define ACCESS_TYPE_LOCAL		3

#define SDSI_MIN_SIZE_DWORDS		276
#define SDSI_SIZE_CONTROL		8
#define SDSI_SIZE_MAILBOX		1024
#define SDSI_SIZE_REGS			72
#define SDSI_SIZE_CMD			sizeof(u64)

/*
 * Write messages are currently up to the size of the mailbox
 * while read messages are up to 4 times the size of the
 * mailbox, sent in packets
 */
#define SDSI_SIZE_WRITE_MSG		SDSI_SIZE_MAILBOX
#define SDSI_SIZE_READ_MSG		(SDSI_SIZE_MAILBOX * 4)

#define SDSI_ENABLED_FEATURES_OFFSET	16
#define SDSI_ENABLED			BIT(3)
#define SDSI_SOCKET_ID_OFFSET		64
#define SDSI_SOCKET_ID			GENMASK(3, 0)

#define SDSI_MBOX_CMD_SUCCESS		0x40
#define SDSI_MBOX_CMD_TIMEOUT		0x80

#define MBOX_TIMEOUT_US			2000
#define MBOX_TIMEOUT_ACQUIRE_US		1000
#define MBOX_POLLING_PERIOD_US		100
#define MBOX_MAX_PACKETS		4

#define MBOX_OWNER_NONE			0x00
#define MBOX_OWNER_INBAND		0x01

#define CTRL_RUN_BUSY			BIT(0)
#define CTRL_READ_WRITE			BIT(1)
#define CTRL_SOM			BIT(2)
#define CTRL_EOM			BIT(3)
#define CTRL_OWNER			GENMASK(5, 4)
#define CTRL_COMPLETE			BIT(6)
#define CTRL_READY			BIT(7)
#define CTRL_STATUS			GENMASK(15, 8)
#define CTRL_PACKET_SIZE		GENMASK(31, 16)
#define CTRL_MSG_SIZE			GENMASK(63, 48)

#define DISC_TABLE_SIZE			12
#define DT_ACCESS_TYPE			GENMASK(3, 0)
#define DT_SIZE				GENMASK(19, 12)
#define DT_TBIR				GENMASK(2, 0)
#define DT_OFFSET(v)			((v) & GENMASK(31, 3))

enum sdsi_command {
	SDSI_CMD_PROVISION_AKC		= 0x04,
	SDSI_CMD_PROVISION_CAP		= 0x08,
	SDSI_CMD_READ_STATE		= 0x10,
};

struct sdsi_mbox_info {
	u64	*payload;
	u64	*buffer;
	int	size;
	bool	is_write;
};

struct disc_table {
	u32	access_info;
	u32	guid;
	u32	offset;
};

struct sdsi_priv {
	struct mutex		mb_lock;
	struct mutex		akc_lock;
	struct miscdevice	miscdev;
	struct file		*akc_owner;
	void __iomem		*control_addr;
	void __iomem		*mbox_addr;
	void __iomem		*regs_addr;
	u32			guid;
	int			socket_id;
	bool			sdsi_enabled;
	bool			dev_present;
};

static __always_inline void
sdsi_qword_memcpy_toio(u64 __iomem *to, const u64 *from, size_t count_bytes)
{
	size_t count = count_bytes / sizeof(*to);
	int i;

	for (i = 0; i < count; i++)
		writeq(from[i], &to[i]);
}

static __always_inline void
sdsi_qword_memcpy_fromio(u64 *to, const u64 __iomem *from, size_t count_bytes)
{
	size_t count = count_bytes / sizeof(*to);
	int i;

	for (i = 0; i < count; i++)
		to[i] = readq(&from[i]);
}

static inline struct sdsi_priv *to_sdsi_priv(struct miscdevice *miscdev)
{
	return container_of(miscdev, struct sdsi_priv, miscdev);
}

static inline void sdsi_complete_transaction(struct sdsi_priv *priv)
{
	u64 control = FIELD_PREP(CTRL_COMPLETE, 1);

	lockdep_assert_held(&priv->mb_lock);
	writeq(control, priv->control_addr);
}

static int sdsi_status_to_errno(u32 status)
{
	switch (status) {
	case SDSI_MBOX_CMD_SUCCESS:
		return 0;
	case SDSI_MBOX_CMD_TIMEOUT:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

static int sdsi_mbox_cmd_read(struct sdsi_priv *priv, struct sdsi_mbox_info *info, int *data_size)
{
	struct device *dev = priv->miscdev.this_device;
	u32 total, loop, eom, status, message_size;
	u64 control;
	int ret;

	lockdep_assert_held(&priv->mb_lock);

	/* Format and send the read command */
	control = FIELD_PREP(CTRL_EOM, 1) |
		  FIELD_PREP(CTRL_SOM, 1) |
		  FIELD_PREP(CTRL_RUN_BUSY, 1) |
		  FIELD_PREP(CTRL_PACKET_SIZE, info->size);
	writeq(control, priv->control_addr);

	/* For reads, data sizes that are larger than the mailbox size are read in packets. */
	total = 0;
	loop = 0;
	do {
		int offset = SDSI_SIZE_MAILBOX * loop;
		void __iomem *addr = priv->mbox_addr + offset;
		u64 *buf = info->buffer + (offset / SDSI_SIZE_CMD);
		u32 packet_size;

		/* Poll on ready bit */
		ret = readq_poll_timeout(priv->control_addr, control, control & CTRL_READY,
					 MBOX_POLLING_PERIOD_US, MBOX_TIMEOUT_US);
		if (ret)
			break;

		eom = FIELD_GET(CTRL_EOM, control);
		status = FIELD_GET(CTRL_STATUS, control);
		packet_size = FIELD_GET(CTRL_PACKET_SIZE, control);
		message_size = FIELD_GET(CTRL_MSG_SIZE, control);

		ret = sdsi_status_to_errno(status);
		if (ret)
			break;

		/* Only the last packet can be less than the mailbox size. */
		if (!eom && packet_size != SDSI_SIZE_MAILBOX) {
			dev_err(dev, "Invalid packet size\n");
			ret = -EPROTO;
			break;
		}

		if (packet_size > SDSI_SIZE_MAILBOX) {
			dev_err(dev, "Packet size to large\n");
			ret = -EPROTO;
			break;
		}

		sdsi_qword_memcpy_fromio(buf, addr, round_up(packet_size, SDSI_SIZE_CMD));

		total += packet_size;

		sdsi_complete_transaction(priv);
	} while (!eom && ++loop < MBOX_MAX_PACKETS);

	if (ret) {
		sdsi_complete_transaction(priv);
		return ret;
	}

	if (!eom) {
		dev_err(dev, "Exceeded read attempts\n");
		return -EPROTO;
	}

	/* Message size check is only valid for multi-packet transfers */
	if (loop && total != message_size)
		dev_warn(dev, "Read count %d differs from expected count %d\n",
			 total, message_size);

	*data_size = total;

	return 0;
}

static int sdsi_mbox_cmd_write(struct sdsi_priv *priv, struct sdsi_mbox_info *info)
{
	u64 control;
	u32 status;
	int ret;

	lockdep_assert_held(&priv->mb_lock);

	/* Write rest of the payload */
	sdsi_qword_memcpy_toio(priv->mbox_addr + SDSI_SIZE_CMD, info->payload + 1,
			       info->size - SDSI_SIZE_CMD);

	/* Format and send the write command */
	control = FIELD_PREP(CTRL_EOM, 1) |
		  FIELD_PREP(CTRL_SOM, 1) |
		  FIELD_PREP(CTRL_RUN_BUSY, 1) |
		  FIELD_PREP(CTRL_READ_WRITE, 1) |
		  FIELD_PREP(CTRL_PACKET_SIZE, info->size);
	writeq(control, priv->control_addr);

	/* Poll on run_busy bit */
	ret = readq_poll_timeout(priv->control_addr, control, !(control & CTRL_RUN_BUSY),
				 MBOX_POLLING_PERIOD_US, MBOX_TIMEOUT_US);

	if (ret)
		goto release_mbox;

	status = FIELD_GET(CTRL_STATUS, control);
	ret = sdsi_status_to_errno(status);

release_mbox:
	sdsi_complete_transaction(priv);

	return ret;
}

static int sdsi_mbox_cmd(struct sdsi_priv *priv, struct sdsi_mbox_info *info, int *data_size)
{
	u64 control;
	int ret = 0;
	u32 owner;

	lockdep_assert_held(&priv->mb_lock);

	/* Check mailbox is available */
	control = readq(priv->control_addr);
	owner = FIELD_GET(CTRL_OWNER, control);
	if (owner != MBOX_OWNER_NONE)
		return -EBUSY;

	/* Write first qword of payload */
	writeq(info->payload[0], priv->mbox_addr);

	/* Check for ownership */
	ret = readq_poll_timeout(priv->control_addr, control,
				 FIELD_GET(CTRL_OWNER, control) & MBOX_OWNER_INBAND,
				 MBOX_POLLING_PERIOD_US, MBOX_TIMEOUT_ACQUIRE_US);
	if (ret)
		return ret;

	if (info->is_write)
		ret = sdsi_mbox_cmd_write(priv, info);
	else
		ret = sdsi_mbox_cmd_read(priv, info, data_size);

	return ret;
}

static long sdsi_if_provision(struct sdsi_priv *priv, void __user *argp, enum sdsi_command cmd)
{
	struct sdsi_mbox_info info;
	u32 __user *datap = argp;
	u32 data_size;
	int ret;

	if (get_user(data_size, datap))
		return -EFAULT;

	if (data_size > (SDSI_SIZE_WRITE_MSG - SDSI_SIZE_CMD))
		return -EOVERFLOW;

	/* Qword aligned message + command qword */
	info.size = round_up(data_size, SDSI_SIZE_CMD) + SDSI_SIZE_CMD;
	info.is_write = true;

	info.payload = kzalloc(info.size, GFP_KERNEL);
	if (!info.payload)
		return -ENOMEM;

	/* Copy message to payload buffer */
	if (copy_from_user(info.payload, argp + sizeof(data_size), data_size)) {
		ret = -EFAULT;
		goto free_payload;
	}

	/* Command is last qword of payload buffer */
	info.payload[(info.size - SDSI_SIZE_CMD) / SDSI_SIZE_CMD] = cmd;

	ret = mutex_lock_interruptible(&priv->mb_lock);
	if (ret)
		goto free_payload;
	ret = sdsi_mbox_cmd(priv, &info, NULL);
	mutex_unlock(&priv->mb_lock);

free_payload:
	kfree(info.payload);

	return (ret < 0) ? ret : 0;
}

static long sdsi_if_read_state_cert(struct sdsi_priv *priv, void __user *argp)
{
	u64 command = SDSI_CMD_READ_STATE;
	struct sdsi_mbox_info info;
	u32 __user *datap = argp;
	u32 data_size;
	int ret;

	/* Buffer for return data */
	info.buffer = kmalloc(SDSI_SIZE_READ_MSG, GFP_KERNEL);
	if (!info.buffer)
		return -ENOMEM;

	info.payload = &command;
	info.size = sizeof(command);
	info.is_write = false;

	ret = mutex_lock_interruptible(&priv->mb_lock);
	if (ret)
		goto free_buffer;
	ret = sdsi_mbox_cmd(priv, &info, &data_size);
	mutex_unlock(&priv->mb_lock);
	if (ret < 0)
		goto free_buffer;

	/* First field is the size of the data */
	if (put_user(data_size, datap)) {
		ret = -EFAULT;
		goto free_buffer;
	}

	/* Copy the data */
	if (copy_to_user(argp + sizeof(data_size), info.buffer, data_size)) {
		ret = -EFAULT;
		goto free_buffer;
	}

free_buffer:
	kfree(info.buffer);

	return (ret < 0) ? ret : 0;
}

static long sdsi_device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *miscdev = file->private_data;
	struct sdsi_priv *priv = to_sdsi_priv(miscdev);
	void __user *argp = (void __user *)arg;
	long ret = -EINVAL;

	if (!priv->dev_present)
		return -ENODEV;

	if (!priv->sdsi_enabled)
		return -EPERM;

	if (cmd == SDSI_IF_READ_STATE)
		return sdsi_if_read_state_cert(priv, argp);

	mutex_lock(&priv->akc_lock);
	switch (cmd) {
	case SDSI_IF_PROVISION_AKC:
		/*
		 * While writing an authentication certificate disallow other openers
		 * from using AKC or CAP.
		 */
		if (!priv->akc_owner)
			priv->akc_owner = file;

		if (priv->akc_owner != file) {
			ret = -EUSERS;
			goto unlock_akc;
		}

		ret = sdsi_if_provision(priv, argp, SDSI_CMD_PROVISION_AKC);
		break;

	case SDSI_IF_PROVISION_CAP:
		if (priv->akc_owner && priv->akc_owner != file) {
			ret = -EUSERS;
			goto unlock_akc;
		}

		ret = sdsi_if_provision(priv, argp, SDSI_CMD_PROVISION_CAP);
		break;

	default:
		break;
	}

unlock_akc:
	mutex_unlock(&priv->akc_lock);

	return ret;
}

static ssize_t sdsi_read_registers(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf, loff_t off,
				   size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sdsi_priv *priv = to_sdsi_priv(miscdev);
	void __iomem *addr = priv->regs_addr;

	if (!priv->dev_present)
		return -ENODEV;

	memcpy_fromio(buf, addr + off, count);

	return count;
}
static BIN_ATTR(registers, 0400, sdsi_read_registers, NULL, SDSI_SIZE_REGS);

static struct bin_attribute *sdsi_bin_attrs[] = {
	&bin_attr_registers,
	NULL,
};

static ssize_t guid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sdsi_priv *priv = to_sdsi_priv(miscdev);

	return sprintf(buf, "0x%x\n", priv->guid);
}
static DEVICE_ATTR_RO(guid);

static struct attribute *sdsi_attrs[] = {
	&dev_attr_guid.attr,
	NULL
};

static const struct attribute_group sdsi_group = {
	.attrs = sdsi_attrs,
	.bin_attrs = sdsi_bin_attrs,
};
__ATTRIBUTE_GROUPS(sdsi);

static int sdsi_device_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;

	get_device(miscdev->this_device);

	return 0;
}

static int sdsi_device_release(struct inode *inode, struct file *file)
{

	struct miscdevice *miscdev = file->private_data;
	struct sdsi_priv *priv = to_sdsi_priv(miscdev);

	if (priv->akc_owner == file)
		priv->akc_owner = NULL;

	put_device(miscdev->this_device);

	return 0;
}

static const struct file_operations sdsi_char_device_ops = {
	.owner = THIS_MODULE,
	.open = sdsi_device_open,
	.unlocked_ioctl = sdsi_device_ioctl,
	.release = sdsi_device_release,
};

static int sdsi_create_misc_device(struct sdsi_priv *priv, struct device *parent)
{
	int ret;

	priv->miscdev.name = kasprintf(GFP_KERNEL, "isdsi-%d", priv->socket_id);
	if (!priv->miscdev.name)
		return -ENOMEM;

	priv->miscdev.minor = MISC_DYNAMIC_MINOR;
	priv->miscdev.fops = &sdsi_char_device_ops;
	priv->miscdev.groups = sdsi_groups;
	priv->miscdev.parent = parent;

	ret = misc_register(&priv->miscdev);
	if (ret)
		kfree(priv->miscdev.name);

	return ret;
}

static int sdsi_map_sdsi_registers(struct device *dev, struct disc_table *disc_table,
				   struct resource *disc_res, struct pci_dev *pci_dev)
{
	struct sdsi_priv *priv = dev_get_drvdata(dev);
	u32 access_type = FIELD_GET(DT_ACCESS_TYPE, disc_table->access_info);
	u32 size = FIELD_GET(DT_SIZE, disc_table->access_info);
	u32 tbir = FIELD_GET(DT_TBIR, disc_table->offset);
	u32 offset = DT_OFFSET(disc_table->offset);
	struct resource res = {};

	/* Starting location of SDSi MMIO region based on access type */
	switch (access_type) {
	case ACCESS_TYPE_LOCAL:
		if (tbir) {
			dev_err(dev,
				"Unsupported BAR index %d for access type %d\n",
				tbir, access_type);
			return -EINVAL;
		}

		/*
		 * For access_type LOCAL, the base address is as follows:
		 * base address = end of discovery region + base offset + 1
		 */
		res.start = disc_res->end + offset + 1;
		break;

	case ACCESS_TYPE_BARID:
		res.start = pci_resource_start(pci_dev, tbir) + offset;
		break;

	default:
		dev_err(dev, "Unrecognized access_type %d\n", access_type);
		return -EINVAL;
	}

	res.end = res.start + size * sizeof(u32) - 1;
	res.flags = IORESOURCE_MEM;

	priv->control_addr = devm_ioremap_resource(dev, &res);
	if (IS_ERR(priv->control_addr))
		return PTR_ERR(priv->control_addr);

	priv->mbox_addr = priv->control_addr + SDSI_SIZE_CONTROL;
	priv->regs_addr = priv->mbox_addr + SDSI_SIZE_MAILBOX;

	priv->socket_id = readl(priv->regs_addr + SDSI_SOCKET_ID_OFFSET) &
				SDSI_SOCKET_ID;

	priv->sdsi_enabled = !!(readq(priv->regs_addr + SDSI_ENABLED_FEATURES_OFFSET) &
				SDSI_ENABLED);
	return 0;
}

static void sdsi_miscdev_remove(void *data)
{
	struct sdsi_priv *priv = data;

	kfree(priv->miscdev.name);
	kfree(priv);
	priv = NULL;
}

static int sdsi_probe(struct auxiliary_device *adev, const struct auxiliary_device_id *id)
{
	struct intel_extended_cap_device *intel_cap_dev =
			container_of(adev, struct intel_extended_cap_device, aux_dev);
	struct disc_table disc_table;
	struct resource *disc_res;
	void __iomem *disc_addr;
	struct sdsi_priv *priv;
	int ret;

	/* Get the SDSi discovery table */
	disc_res = intel_ext_cap_get_resource(intel_cap_dev, 0);
	if (!disc_res)
		return -ENODEV;

	disc_addr = devm_ioremap_resource(&adev->dev, disc_res);
	if (IS_ERR(disc_addr))
		return PTR_ERR(disc_addr);

	memcpy_fromio(&disc_table, disc_addr, DISC_TABLE_SIZE);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&adev->dev, priv);

	priv->guid = disc_table.guid;

	/* Map the SDSi mailbox registers */
	ret = sdsi_map_sdsi_registers(&adev->dev, &disc_table, disc_res, intel_cap_dev->pcidev);
	if (ret) {
		kfree(priv);
		return ret;
	}

	ret = sdsi_create_misc_device(priv, &adev->dev);
	if (ret) {
		kfree(priv);
		return ret;
	}

	ret = devm_add_action_or_reset(priv->miscdev.this_device, sdsi_miscdev_remove, priv);
	if (ret)
		goto deregister_misc;

	mutex_init(&priv->mb_lock);
	mutex_init(&priv->akc_lock);
	priv->dev_present = true;

	return 0;

deregister_misc:
	misc_deregister(&priv->miscdev);

	return ret;
}

static void sdsi_remove(struct auxiliary_device *adev)
{
	struct sdsi_priv *priv = dev_get_drvdata(&adev->dev);

	priv->dev_present = false;
	misc_deregister(&priv->miscdev);
}

static const struct auxiliary_device_id sdsi_aux_id_table[] = {
	{ .name = "intel_extended_caps.65", },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, sdsi_aux_id_table);

static struct auxiliary_driver sdsi_aux_driver = {
	.id_table	= sdsi_aux_id_table,
	.remove		= sdsi_remove,
	.probe		= sdsi_probe,
};

static int __init sdsi_aux_init(void)
{
	return auxiliary_driver_register(&sdsi_aux_driver);
}
module_init(sdsi_aux_init);

static void __exit sdsi_aux_exit(void)
{
	auxiliary_driver_unregister(&sdsi_aux_driver);
}
module_exit(sdsi_aux_exit);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Software Defined Silicon driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(INTEL_EXT_CAPS);
