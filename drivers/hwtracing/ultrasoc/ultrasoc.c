// SPDX-License-Identifier: MIT
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
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "ultrasoc.h"

static ssize_t com_mux_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t size)
{
	struct ultrasoc_drv_data *drvdata = dev_get_drvdata(dev);
	long val;
	int ret;

	ret = kstrtol(buf, 0, &val);
	if (ret)
		return -EINVAL;

	writel(val & 0xffffffff, drvdata->com_mux);
	return size;
}

static ssize_t com_mux_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct ultrasoc_drv_data *drvdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", readl(drvdata->com_mux));
}
static DEVICE_ATTR_RW(com_mux);

static umode_t ultrasoc_com_mux_is_visible(struct kobject *kobj,
					   struct attribute *attr, int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct ultrasoc_drv_data *drvdata = dev_get_drvdata(dev);

	if (IS_ERR(drvdata->com_mux))
		return 0;

	return attr->mode;
}

static struct attribute *ultrasoc_com_mux_attr[] = {
	&dev_attr_com_mux.attr,
	NULL,
};

static const struct attribute_group ultrasoc_com_mux_group = {
	.attrs = ultrasoc_com_mux_attr,
	.is_visible = ultrasoc_com_mux_is_visible,
};

static const struct attribute_group *ultrasoc_global_groups[] = {
	&ultrasoc_com_mux_group,
	NULL,
};

static int ultrasoc_probe(struct platform_device *pdev)
{
	struct ultrasoc_drv_data *drvdata;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	drvdata->dev = &pdev->dev;
	INIT_LIST_HEAD(&drvdata->ultrasoc_com_head);

	drvdata->com_mux = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->com_mux)) {
		dev_err(&pdev->dev, "Failed to ioremap for com_mux resource.\n");
		return PTR_ERR(drvdata->com_mux);
	}
	/* switch ultrasoc commucator mux for on-chip drivers. */
	writel(US_SELECT_ONCHIP, drvdata->com_mux);
	platform_set_drvdata(pdev, drvdata);

	return 0;
}

static int ultrasoc_remove(struct platform_device *pdev)
{
	struct ultrasoc_drv_data *pdata = platform_get_drvdata(pdev);

	/* switch back to external debuger users if necessary.*/
	if (!IS_ERR(pdata->com_mux))
		writel(0, pdata->com_mux);

	return 0;
}

static struct acpi_device_id ultrasoc_acpi_match[] = {
	{"HISI0391", },
	{},
};
MODULE_DEVICE_TABLE(acpi, ultrasoc_acpi_match);

static struct platform_driver ultrasoc_driver = {
	.driver = {
		.name = "ultrasoc",
		.acpi_match_table = ultrasoc_acpi_match,
		.dev_groups = ultrasoc_global_groups,
	},
	.probe = ultrasoc_probe,
	.remove = ultrasoc_remove,
};
module_platform_driver(ultrasoc_driver);

static const char * const ultrasoc_com_type_string[] = {
	"UNKNOWN",
	"UP-DOWN-BOTH",
	"DOWN-ONLY",
};

static const char * const ultrasoc_com_service_status_string[] = {
	"stopped",
	"sleeping",
	"running normal",
};

/*
 * To avoid communicator buffer overflow, we create a service thread
 * to do the communicator work. This is the service thread entry.
 */
static int ultrasoc_com_service(void *arg)
{
	unsigned int deep_sleep = 0;
	struct ultrasoc_com *com;
	int ud_flag = 0;
	int core;

	core = smp_processor_id();
	com = (struct ultrasoc_com *)arg;
	if (!com->com_work) {
		dev_err(com->dev,
			 "This communicator do not have a work entry.\n");
		com->service_status = ULTRASOC_COM_SERVICE_STOPPED;
		return -EINVAL;
	}
	dev_dbg(com->dev, "ultrasoc com service %s run on core %d.\n",
		com->name,  core);

	while (true) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock(&com->service_lock);
		if (com->service_status == ULTRASOC_COM_SERVICE_SLEEPING) {
			spin_unlock(&com->service_lock);
			schedule();
			spin_lock(&com->service_lock);
		}

		/*
		 * Since this thread service might be woken up with a status
		 * of STOP, we check the status again to avoid setting an error
		 * status
		 */
		if (com->service_status == ULTRASOC_COM_SERVICE_SLEEPING) {
			com->service_status =
				ULTRASOC_COM_SERVICE_RUNNING_NORMAL;
			deep_sleep = 0;
		}
		spin_unlock(&com->service_lock);
		__set_current_state(TASK_RUNNING);

		if (com->service_status == ULTRASOC_COM_SERVICE_STOPPED)
			break;

		ud_flag = com->com_work(com);
		if (!ud_flag) {
			usleep_range(10, 100);
			deep_sleep++;
		} else {
			deep_sleep = 0;
			usleep_range(1, 4);
		}
		if (deep_sleep > com->timeout)
			com->service_status = ULTRASOC_COM_SERVICE_SLEEPING;
		if (kthread_should_stop())
			break;
	}
	com->service_status = ULTRASOC_COM_SERVICE_STOPPED;

	return 0;
}

static void com_try_stop_service(struct ultrasoc_com *com)
{
	if (com->service_status != ULTRASOC_COM_SERVICE_STOPPED) {
		spin_lock(&com->service_lock);
		com->service_status = ULTRASOC_COM_SERVICE_STOPPED;
		spin_unlock(&com->service_lock);
		kthread_stop(com->service);
		com->service = NULL;
	}
}

static void com_try_start_service(struct ultrasoc_com *com)
{
	if (com->service &&
	    com->service_status != ULTRASOC_COM_SERVICE_STOPPED) {
		dev_notice(com->dev, "Service is already running on %ld.\n",
			   com->core_bind);
		wake_up_process(com->service);
		return;
	}

	dev_dbg(com->dev, "Starting service %s on core %ld.\n",	com->name,
		com->core_bind);
	com->service = kthread_create(ultrasoc_com_service, com, "%s_service",
				      com->name);
	if (IS_ERR(com->service)) {
		spin_lock(&com->service_lock);
		com->service_status = ULTRASOC_COM_SERVICE_STOPPED;
		spin_unlock(&com->service_lock);
		dev_err(com->dev, "Failed to start service.\n");
	}

	if (com->core_bind != -1)
		kthread_bind(com->service, com->core_bind);

	spin_lock(&com->service_lock);
	com->service_status = ULTRASOC_COM_SERVICE_RUNNING_NORMAL;
	spin_unlock(&com->service_lock);
	wake_up_process(com->service);
}

static void com_service_restart(struct ultrasoc_com *com)
{
	com_try_stop_service(com);
	com_try_start_service(com);
}

static ssize_t ultrasoc_com_status(struct ultrasoc_com *com, char *buf)
{
	enum ultrasoc_com_service_status status = com->service_status;
	enum ultrasoc_com_type type = com->com_type;
	ssize_t wr_size;

	wr_size = sysfs_emit(buf, "%-20s: %s\n", "com-type",
			     ultrasoc_com_type_string[type]);
	wr_size += sysfs_emit_at(buf, wr_size, "%-20s: %s\n", "service status",
				 ultrasoc_com_service_status_string[status]);
	wr_size += uscom_ops_com_status(com, buf, wr_size);

	return wr_size;
}

ULTRASOC_COM_ATTR_WO_OPS(start, com_try_start_service);
ULTRASOC_COM_ATTR_WO_OPS(stop, com_try_stop_service);
ULTRASOC_COM_ATTR_WO_OPS(restart, com_service_restart);
ULTRASOC_COM_ATTR_RO_OPS(com_status, ultrasoc_com_status);

struct ultrasoc_com *ultrasoc_find_com_by_dev(struct device *com_dev)
{
	struct ultrasoc_drv_data *pdata = dev_get_drvdata(com_dev->parent);
	struct list_head *com_head = &pdata->ultrasoc_com_head;
	struct ultrasoc_com *com;
	struct list_head *cur;

	list_for_each(cur, com_head) {
		com = list_entry(cur, struct ultrasoc_com, node);
		if (com->dev == com_dev)
			return com;
	}

	dev_err(com_dev, "Unable to find com associated with this device!\n");
	return NULL;
}

static ssize_t core_bind_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t size)
{
	struct ultrasoc_com *com = ultrasoc_find_com_by_dev(dev);
	long core_bind;
	int ret;

	if (!com)
		return 0;

	ret = kstrtol(buf, 0, &core_bind);
	if (!ret)
		com->core_bind = core_bind;

	return size;
}

static ssize_t core_bind_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ultrasoc_com *com = ultrasoc_find_com_by_dev(dev);

	if (!com)
		return 0;

	return sysfs_emit(buf, "%#lx", com->core_bind);
}
static DEVICE_ATTR_RW(core_bind);

static ssize_t message_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t size)
{
	struct ultrasoc_com *com = ultrasoc_find_com_by_dev(dev);
	u64 msg, msg_len;
	int elements;

	elements = sscanf(buf, "%llx %llx", &msg, &msg_len);
	if (elements < 2)
		return -EINVAL;

	com->com_ops->put_raw_msg(com, msg_len, msg);
	dev_dbg(dev, "Set message %#llx, length is %#llx.\n", msg, msg_len);

	return size;
}
static DEVICE_ATTR_WO(message);

static umode_t ultrasoc_com_message_is_visible(struct kobject *kobj,
					   struct attribute *attr, int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct ultrasoc_com *com = ultrasoc_find_com_by_dev(dev);

	if (com->com_type != ULTRASOC_COM_TYPE_BOTH)
		return 0;

	return attr->mode;
}

static struct attribute *ultrasoc_com_global_attrs[] = {
	&dev_attr_com_status.attr,
	NULL,
};

static struct attribute *ultrasoc_com_service_attrs[] = {
	&dev_attr_core_bind.attr,
	&dev_attr_start.attr,
	&dev_attr_stop.attr,
	&dev_attr_restart.attr,
	NULL,
};

static struct attribute *ultrasoc_com_message_attrs[] = {
	&dev_attr_message.attr,
	NULL,
};

static const struct attribute_group ultrasoc_com_global_group = {
	.attrs = ultrasoc_com_global_attrs,
};

static const struct attribute_group ultrasoc_com_service_group = {
	.attrs = ultrasoc_com_service_attrs,
	.name = "service",
};

static const struct attribute_group ultrasoc_com_message_group = {
	.attrs = ultrasoc_com_message_attrs,
	.is_visible = ultrasoc_com_message_is_visible,
};

static const struct attribute_group *ultrasoc_com_attr[] = {
	&ultrasoc_com_global_group,
	&ultrasoc_com_service_group,
	&ultrasoc_com_message_group,
	NULL,
};

static int ultrasoc_validate_com_descp(struct ultrasoc_com_descp *com_descp)
{
	if (!com_descp->uscom_ops)
		return -EINVAL;

	if (com_descp->com_type == ULTRASOC_COM_TYPE_BOTH) {
		if (!com_descp->uscom_ops->put_raw_msg ||
		    !com_descp->default_route_msg)
			return -EINVAL;
	}

	return 0;
}

static int wait_com_service_stop(struct ultrasoc_com *com)
{
	u32 timeout = 0;

	if (com->service_status != ULTRASOC_COM_SERVICE_STOPPED)
		com_try_stop_service(com);
	while (com->service_status != ULTRASOC_COM_SERVICE_STOPPED) {
		usleep_range(10, 100);
		timeout++;
		if (timeout > com->timeout)
			return -ETIMEDOUT;
	}

	return 0;
}

/**
 * ultrasoc_register_com - register a ultrasoc communicator for communication
 * between usmsg bus devices and platform bus devices.
 *
 * @top_dev: the ultrasoc top platform device to manage all communicator.
 * @com_descp: the communicator description to be registered.
 * Return: the pointer to a new communicator if register ok, NULL if failure.
 */
struct ultrasoc_com *ultrasoc_register_com(struct device *top_dev,
					   struct ultrasoc_com_descp *com_descp)
{
	struct ultrasoc_drv_data *drv_data = dev_get_drvdata(top_dev);
	struct ultrasoc_com *com;
	int ret;

	if (!drv_data)
		return ERR_PTR(-EBUSY);

	ret = ultrasoc_validate_com_descp(com_descp);
	if (ret)
		return ERR_PTR(-EINVAL);

	com = devm_kzalloc(top_dev, sizeof(*com), GFP_KERNEL);
	if (!com)
		return ERR_PTR(-ENOMEM);

	com->name = com_descp->name;
	com->com_type = com_descp->com_type;
	com->com_ops = com_descp->uscom_ops;
	com->com_work = com_descp->com_work;
	com->timeout = US_SERVICE_TIMEOUT;
	com->core_bind = -1;
	com->root = top_dev;
	com->dev = com_descp->com_dev;
	spin_lock_init(&com->service_lock);

	device_lock(top_dev);
	list_add_tail(&com->node, &drv_data->ultrasoc_com_head);
	device_unlock(top_dev);

	if (com->com_type == ULTRASOC_COM_TYPE_BOTH && !drv_data->def_up_com) {
		/*
		 * There is one ULTRASOC_COM_TYPE_BOTH device per ultrasoc
		 * system, so race will not happen.
		 */
		drv_data->def_up_com = com;
		/* start the default communicator service. */
		com_try_start_service(com);
		/* set ultrasoc route all msgs to port 1 as default*/
		com->com_ops->put_raw_msg(com, US_ROUTE_LENGTH,
					  com_descp->default_route_msg);
	}

	ret = device_add_groups(com->dev, ultrasoc_com_attr);
	if (ret)
		return  ERR_PTR(ret);

	return com;
}
EXPORT_SYMBOL_GPL(ultrasoc_register_com);

int ultrasoc_unregister_com(struct ultrasoc_com *com)
{
	struct ultrasoc_drv_data *pdata = dev_get_drvdata(com->root);
	struct device *com_dev = com->dev;
	struct device *dev = com->root;

	if (wait_com_service_stop(com)) {
		dev_err(com_dev, "Com service is still running.\n");
		return -EBUSY;
	}

	if (pdata->def_up_com == com)
		pdata->def_up_com = NULL;

	device_lock(dev);
	list_del(&com->node);
	device_unlock(dev);
	device_remove_groups(com_dev, ultrasoc_com_attr);

	return 0;
}
EXPORT_SYMBOL_GPL(ultrasoc_unregister_com);

MODULE_DESCRIPTION("Ultrasoc driver");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Jonathan Zhou <jonathan.zhouwen@huawei.com>");
MODULE_AUTHOR("Qi Liu <liuqi115@huawei.com>");
