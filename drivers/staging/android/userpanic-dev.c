// SPDX-License-Identifier: GPL-2.0-only
/* userpanic-dev.c
 *
 * User-panic Device Interface
 *
 * Copyright 2021 Google LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/miscdevice.h>

struct userpanic_crash_info {
	void __user *title_uaddr;
	void __user *msg_uaddr;
};

#define CRASH_INFO		(_IOW('U', 179, struct userpanic_crash_info))

static int do_userpanic(const char *title, const char *msg)
{
	const size_t msgbuf_sz = PAGE_SIZE;
	char *msgbuf;

	msgbuf = kmalloc(msgbuf_sz, GFP_KERNEL);
	if (!msgbuf)
		return -ENOMEM;

	pr_emerg("User process '%.*s' %d requesting kernel panic\n",
		 sizeof(current->comm), current->comm, current->pid);
	if (msg)
		pr_emerg("   with message: %s\n", msg);

	/* Request panic with customized panic title. */
	snprintf(msgbuf, msgbuf_sz, "U: %s: %s", current->comm, title);
	panic(msgbuf);
	kfree(msgbuf);
	return -EFAULT;
}

static long userpanic_device_ioctl(struct file *file, u_int cmd, u_long arg)
{
	struct userpanic_crash_info crash_info;
	char *title;
	char *msg = NULL;
	int ret;

	switch (cmd) {
	case CRASH_INFO:
		if (copy_from_user(&crash_info, (void __user *)arg, sizeof(crash_info)))
			return -EFAULT;

		if (!crash_info.title_uaddr)
			return -EINVAL;

		title = strndup_user(crash_info.title_uaddr, PAGE_SIZE);
		if (IS_ERR(title)) {
			pr_err("failed to strndup .title_uaddr: %d\n", PTR_ERR(title));
			return -EINVAL;
		}

		if (crash_info.msg_uaddr) {
			msg = strndup_user(crash_info.msg_uaddr, PAGE_SIZE);
			if (IS_ERR(msg)) {
				kfree(title);
				pr_err("failed to strndup .msg_uaddr: %d\n", PTR_ERR(msg));
				return -EINVAL;
			}
		}

		ret = do_userpanic(title, msg);
		kfree(msg);
		kfree(title);
		return ret;
	}

	return -EINVAL;
}

static const struct file_operations userpanic_device_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = userpanic_device_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

static struct miscdevice userpanic_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "userspace_panic",
	.fops  = &userpanic_device_fops,
};

static int __init userspace_panic_dev_init(void)
{
	int ret;

	ret = misc_register(&userpanic_device);
	if (ret)
		pr_err("misc_register failed for userspace_panic device\n");

	return ret;
}

device_initcall(userspace_panic_dev_init);

MODULE_DESCRIPTION("User-panic interface device driver");
MODULE_AUTHOR("Woody Lin <woodylin@google.com>");
MODULE_LICENSE("GPL v2");
