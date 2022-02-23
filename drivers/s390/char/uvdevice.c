// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright IBM Corp. 2022
 *  Author(s): Steffen Eiden <seiden@linux.ibm.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt ".\n"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include <asm/uvdevice.h>
#include <asm/uv.h>

/**
 * uvio_qui() - Perform a Query Ultravisor Information UVC.
 *
 * uv_ioctl: ioctl control block
 *
 * uvio_qui() does a Query Ultravisor Information (QUI) Ultravisor Call.
 * It creates the uvc qui request and sends it to the Ultravisor. After that
 * it copies the response to userspace and fills the rc and rrc of uv_ioctl
 * uv_call with the response values of the Ultravisor.
 *
 * Create the UVC structure, send the UVC to UV and write the response in the ioctl struct.
 *
 * Return: 0 on success or a negative error code on error.
 */
static int uvio_qui(struct uvio_ioctl_cb *uv_ioctl)
{
	u8 __user *user_buf_addr = (__user u8 *)uv_ioctl->argument_addr;
	size_t user_buf_len = uv_ioctl->argument_len;
	struct uv_cb_header *uvcb_qui = NULL;
	int ret;

	/*
	 * Do not check for a too small buffer. If userspace provides a buffer
	 * that is too small the Ultravisor will complain.
	 */
	ret = -EINVAL;
	if (!user_buf_len || user_buf_len > UVIO_QUI_MAX_LEN)
		goto out;
	ret = -ENOMEM;
	uvcb_qui = kvzalloc(user_buf_len, GFP_KERNEL);
	if (!uvcb_qui)
		goto out;
	uvcb_qui->len = user_buf_len;
	uvcb_qui->cmd = UVC_CMD_QUI;

	uv_call(0, (u64)uvcb_qui);

	ret = -EFAULT;
	if (copy_to_user(user_buf_addr, uvcb_qui, uvcb_qui->len))
		goto out;
	uv_ioctl->uv_rc = uvcb_qui->rc;
	uv_ioctl->uv_rrc = uvcb_qui->rrc;

	ret = 0;
out:
	kvfree(uvcb_qui);
	return ret;
}

static int uvio_copy_and_check_ioctl(struct uvio_ioctl_cb *ioctl, void __user *argp)
{
	if (copy_from_user(ioctl, argp, sizeof(*ioctl)))
		return -EFAULT;
	if (ioctl->flags != 0)
		return -EINVAL;
	if (memchr_inv(ioctl->reserved14, 0, sizeof(ioctl->reserved14)))
		return -EINVAL;

	return 0;
}

/*
 * IOCTL entry point for the Ultravisor device.
 */
static long uvio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct uvio_ioctl_cb *uv_ioctl;
	long ret;

	ret = -ENOMEM;
	uv_ioctl = vzalloc(sizeof(*uv_ioctl));
	if (!uv_ioctl)
		goto out;

	switch (cmd) {
	case UVIO_IOCTL_QUI:
		ret = uvio_copy_and_check_ioctl(uv_ioctl, argp);
		if (ret)
			goto out;
		ret = uvio_qui(uv_ioctl);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	if (ret)
		goto out;

	if (copy_to_user(argp, uv_ioctl, sizeof(*uv_ioctl)))
		ret = -EFAULT;

 out:
	vfree(uv_ioctl);
	return ret;
}

static const struct file_operations uvio_dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = uvio_ioctl,
	.llseek = no_llseek,
};

static struct miscdevice uvio_dev_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = UVIO_DEVICE_NAME,
	.fops = &uvio_dev_fops,
};

static void __exit uvio_dev_exit(void)
{
	misc_deregister(&uvio_dev_miscdev);
}

static int __init uvio_dev_init(void)
{
	if (!test_facility(158))
		return -ENXIO;
	return misc_register(&uvio_dev_miscdev);
}

module_init(uvio_dev_init);
module_exit(uvio_dev_exit);

MODULE_AUTHOR("IBM Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ultravisor UAPI driver");
