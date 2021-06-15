/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2021 Hisilicon Limited Permission is hereby granted, free of
 * charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Code herein communicates with and accesses proprietary hardware which is
 * licensed intellectual property (IP) belonging to Siemens Digital Industries
 * Software Ltd.
 *
 * Siemens Digital Industries Software Ltd. asserts and reserves all rights to
 * their intellectual property. This paragraph may not be removed or modified
 * in any way without permission from Siemens Digital Industries Software Ltd.
 */

#ifndef _LINUX_ULTRASOC_H
#define _LINUX_ULTRASOC_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/slab.h>

struct ultrasoc_drv_data {
	struct device *dev;
	void __iomem *com_mux;
	struct list_head ultrasoc_com_head;
	struct ultrasoc_com *def_up_com;
	const char *dev_data_path;
	spinlock_t spinlock;
};

enum ultrasoc_com_type {
	ULTRASOC_COM_TYPE_BOTH,
	ULTRASOC_COM_TYPE_DOWN,
};

struct ultrasoc_com_descp {
	const char *name;
	enum ultrasoc_com_type com_type;
	struct device *com_dev;
	struct uscom_ops *uscom_ops;
	int (*com_work)(struct ultrasoc_com *com);
	u64 default_route_msg;
};

enum ultrasoc_com_service_status {
	ULTRASOC_COM_SERVICE_STOPPED,
	ULTRASOC_COM_SERVICE_SLEEPING,
	ULTRASOC_COM_SERVICE_RUNNING_NORMAL,
};

#define USMSG_MAX_IDX				9
struct msg_descp {
	unsigned int msg_len;
	__le32 msg_buf[USMSG_MAX_IDX];
	struct list_head node;
};

static inline void usmsg_list_realse_all(struct list_head *msg_head)
{
	struct msg_descp *msgd, *next;

	list_for_each_entry_safe(msgd, next, msg_head, node) {
		list_del(&msgd->node);
		kfree(msgd);
	}
}

struct ultrasoc_com {
	const char *name;
	enum ultrasoc_com_type com_type;
	struct device *root;
	struct device *dev;

	long core_bind;
	int (*com_work)(struct ultrasoc_com *com);
	spinlock_t service_lock;
	struct task_struct *service;
	int service_status;
	unsigned int timeout;

	char *data_path;
	struct uscom_ops *com_ops;

	struct list_head node;
};

struct uscom_ops {
	ssize_t (*com_status)(struct ultrasoc_com *com, char *buf,
			      ssize_t size);
	void (*put_raw_msg)(struct ultrasoc_com *com, int msg_size,
			    unsigned long long msg);
};

#define uscom_ops_com_status(uscom, buf, size)                           \
	(((uscom)->com_ops && (uscom)->com_ops->com_status) ?            \
		 (uscom)->com_ops->com_status(uscom, buf, size) : 0)

static inline void *ultrasoc_com_get_drvdata(struct ultrasoc_com *uscom)
{
	return dev_get_drvdata(uscom->dev);
}

struct ultrasoc_com *
ultrasoc_register_com(struct device *root_dev,
		      struct ultrasoc_com_descp *com_descp);
int ultrasoc_unregister_com(struct ultrasoc_com *com);
int ultrasoc_com_del_usmsg_device(struct ultrasoc_com *com, int index);

struct ultrasoc_com *ultrasoc_find_com_by_dev(struct device *com_dev);

#define ULTRASOC_COM_ATTR_WO_OPS(attr_name, com_ops)                           \
	static ssize_t attr_name##_store(struct device *dev,                   \
					 struct device_attribute *attr,        \
					 const char *buf, size_t size)         \
	{                                                                      \
		struct ultrasoc_com *com = ultrasoc_find_com_by_dev(dev);      \
		long attr_name;                                                \
		int ret;                                                       \
		if (!com)                                                      \
			return 0;                                              \
		ret = kstrtol(buf, 0, &attr_name);                             \
		if (ret) {                                                     \
			return size;                                           \
		}                                                              \
		if (attr_name == 1) {                                          \
			com_ops(com);                                          \
		}                                                              \
		return size;                                                   \
	}                                                                      \
	static DEVICE_ATTR_WO(attr_name)

#define ULTRASOC_COM_ATTR_RO_OPS(attr_name, com_ops)                           \
	static ssize_t attr_name##_show(struct device *dev,                    \
					struct device_attribute *attr,         \
					char *buf)                             \
	{                                                                      \
		struct ultrasoc_com *com = ultrasoc_find_com_by_dev(dev);      \
		if (!com)                                                      \
			return 0;                                              \
		return com_ops(com, buf);                                      \
	}                                                                      \
	static DEVICE_ATTR_RO(attr_name)

/* 1000 * (10us ~ 100us) */
#define US_SERVICE_TIMEOUT		1000
/* communicator service work status */
#define US_SERVICE_ONWORK		1
#define US_SERVICE_IDLE			0
#define US_ROUTE_LENGTH			11
#define US_SELECT_ONCHIP		0x3

#endif
