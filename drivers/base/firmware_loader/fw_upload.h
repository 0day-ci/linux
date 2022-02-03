/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FIRMWARE_UPLOAD_H
#define __FIRMWARE_UPLOAD_H

#include <linux/device.h>

struct fw_upload_priv {
	struct fw_upload *fw_upload;
	const char *name;
	const struct fw_upload_ops *ops;
	struct mutex lock;		/* protect data structure contents */
	struct work_struct work;
	const u8 *data;			/* pointer to update data */
	u32 remaining_size;		/* size remaining to transfer */
	u32 progress;
	u32 err_progress;		/* progress at time of failure */
	u32 err_code;			/* security manager error code */
	bool driver_unload;
};

extern struct device_attribute dev_attr_status;
extern struct device_attribute dev_attr_error;
extern struct device_attribute dev_attr_cancel;
extern struct device_attribute dev_attr_remaining_size;

umode_t
fw_upload_is_visible(struct kobject *kobj, struct attribute *attr, int n);

#endif /* __FIRMWARE_UPLOAD_H */
