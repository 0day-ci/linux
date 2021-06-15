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
#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <asm/unaligned.h>

#include "ultrasoc-axi-com.h"

static void axi_com_enable_hw(struct axi_com_drv_data *drvdata)
{
	u32 val;

	val = readl(drvdata->base + AXIC_US_CTL);
	val |= AXIC_US_CTL_EN;
	writel(val, drvdata->base + AXIC_US_CTL);

	val = readl(drvdata->base + AXIC_DS_CTL);
	val |= AXIC_DS_CTL_EN;
	writel(val, drvdata->base + AXIC_DS_CTL);
}

static void axi_com_disable_hw(struct axi_com_drv_data *drvdata)
{
	u32 val;

	val = readl(drvdata->base + AXIC_US_CTL);
	val &= ~AXIC_US_CTL_EN;
	writel(val, drvdata->base + AXIC_US_CTL);

	val = readl(drvdata->base + AXIC_DS_CTL);
	val &= ~AXIC_DS_CTL_EN;
	writel(val, drvdata->base + AXIC_DS_CTL);
}

static inline bool axi_com_us_buf_full(struct axi_com_drv_data *drvdata)
{
	return readl(drvdata->base + AXIC_US_BUF_STS) & BIT(0);
}

static inline bool axi_com_ds_buf_full(struct axi_com_drv_data *drvdata)
{
	return readl(drvdata->base + AXIC_DS_BUF_STS) & BIT(0);
}

static int axi_com_try_send_msg(struct axi_com_drv_data *drvdata)
{
	struct msg_descp *msg;
	struct list_head *node;
	int index = 0;
	int unsent;
	u32 data;

	if (axi_com_us_buf_full(drvdata)) {
		dev_err_once(drvdata->dev, "No room for upstream buffer.\n");
		return US_SERVICE_IDLE;
	}

	spin_lock(&drvdata->us_msg_list_lock);
	if (list_empty(&drvdata->us_msg_head)) {
		spin_unlock(&drvdata->us_msg_list_lock);
		return US_SERVICE_IDLE;
	}

	node = drvdata->us_msg_head.next;
	list_del(node);
	drvdata->us_msg_cur--;
	msg = container_of(node, struct msg_descp, node);
	spin_unlock(&drvdata->us_msg_list_lock);

	unsent = msg->msg_len;
	dev_dbg(drvdata->dev, "Length of send msg: %d.\n", msg->msg_len);
	while (unsent > 0) {
		data = get_unaligned_le32(&msg->msg_buf[index++]);
		writel(data, drvdata->base + AXIC_US_DATA);
		unsent -= AXIC_MSG_LEN_PER_SEND;
	}
	kfree(msg);

	return US_SERVICE_ONWORK;
}

static int axi_com_try_recv_msg(struct axi_com_drv_data *drvdata)
{
	struct msg_descp tmp_msg = {0};
	struct msg_descp *msg;
	bool lost = false;
	u32 index = 0;
	u32 status, entries, data;

	if (!axi_com_ds_buf_full(drvdata))
		return US_SERVICE_IDLE;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg) {
		/*
		 * create local variable tmp_msg to read and clear
		 * the downstream message.
		 */
		msg = &tmp_msg;
		lost = true;
	}

	do {
		if (index == USMSG_MAX_IDX) {
			dev_warn(drvdata->dev, "Illegal message.\n");
			break;
		}
		data = readl(drvdata->base + AXIC_DS_DATA);
		put_unaligned_le32(data, &msg->msg_buf[index++]);
		status = readl(drvdata->base + AXIC_DS_RD_STS);
		entries = status & GENMASK(7, 4);
		msg->msg_len += AXIC_MSG_LEN_PER_REC;
	} while (entries != 0);

	if (!lost) {
		spin_lock(&drvdata->ds_msg_list_lock);
		drvdata->ds_msg_cur++;
		drvdata->ds_msg_counter++;
		list_add_tail(&msg->node, &drvdata->ds_msg_head);
		spin_unlock(&drvdata->ds_msg_list_lock);
	}

	return US_SERVICE_ONWORK;
}

static int axi_com_work(struct ultrasoc_com *uscom)
{
	struct axi_com_drv_data *drvdata = ultrasoc_com_get_drvdata(uscom);
	int us_ds_flag;

	us_ds_flag = axi_com_try_recv_msg(drvdata);
	us_ds_flag |= axi_com_try_send_msg(drvdata);

	return us_ds_flag;
}

static ssize_t axi_com_show_status(struct ultrasoc_com *uscom, char *buf,
				   ssize_t wr_size)
{
	struct axi_com_drv_data *drvdata = ultrasoc_com_get_drvdata(uscom);

	wr_size += sysfs_emit_at(buf, wr_size, "%-20s: %d\n",
				 "ds msg list num", drvdata->ds_msg_cur);
	wr_size += sysfs_emit_at(buf, wr_size, "%-20s: %d\n",
				 "us msg list num", drvdata->us_msg_cur);

	return wr_size;
}

static void axi_com_put_raw_msg(struct ultrasoc_com *uscom, int msg_size,
			unsigned long long msg_data)
{
	struct axi_com_drv_data *drvdata = ultrasoc_com_get_drvdata(uscom);
	struct msg_descp *p_msg;

	p_msg = kmalloc(sizeof(*p_msg), GFP_KERNEL);
	if (!p_msg)
		return;

	p_msg->msg_len = msg_size;
	put_unaligned_le64(msg_data, &p_msg->msg_buf[0]);
	spin_lock(&drvdata->us_msg_list_lock);
	list_add_tail(&p_msg->node, &drvdata->us_msg_head);
	drvdata->us_msg_cur++;
	spin_unlock(&drvdata->us_msg_list_lock);

	if (uscom->service_status != ULTRASOC_COM_SERVICE_STOPPED)
		wake_up_process(uscom->service);
	else
		dev_warn(uscom->dev, "Com service is not running.\n");
}

static struct uscom_ops axi_com_ops = {
	.com_status = axi_com_show_status,
	.put_raw_msg = axi_com_put_raw_msg,
};

/*
 * Config hardwares on the tracing path, using DSM calls to avoid exposing
 * hardware message format.
 */
static int axi_com_config_inport(struct axi_com_drv_data *drvdata, bool enable)
{
	struct device *dev = drvdata->dev;
	u32 flag = enable ? 1 : 0;
	union acpi_object *obj;
	guid_t guid;

	if (guid_parse("82ae1283-7f6a-4cbe-aa06-53e8fb24db18", &guid)) {
		dev_err(dev, "Get GUID failed.\n");
		return -EINVAL;
	}

	obj = acpi_evaluate_dsm(ACPI_HANDLE(dev), &guid, 0, flag, NULL);
	if (!obj)
		dev_err(dev, "ACPI handle failed!\n");

	ACPI_FREE(obj);

	return 0;
}

static int axi_com_config_com_descp(struct platform_device *pdev,
				    struct axi_com_drv_data *drvdata)
{
	struct device *parent = pdev->dev.parent;
	struct ultrasoc_com_descp com_descp = {0};
	struct device *dev = &pdev->dev;
	struct ultrasoc_com *com;

	com_descp.name = pdev->name;
	com_descp.com_type = ULTRASOC_COM_TYPE_BOTH;
	com_descp.com_dev = dev;
	com_descp.uscom_ops = &axi_com_ops;
	com_descp.com_work = axi_com_work;

	if (device_property_read_u64(dev, "ultrasoc,default_route",
				     &com_descp.default_route_msg)) {
		dev_err(dev, "Failed to read default_route!\n");
		return -EINVAL;
	}

	com = ultrasoc_register_com(parent, &com_descp);
	if (IS_ERR(com)) {
		dev_err(dev, "Failed to register to ultrasoc.\n");
		return PTR_ERR(com);
	}

	/*
	 * record the returned com point in drvdata,
	 * it will be used to unregister the com
	 * from ultrasoc.
	 */
	drvdata->com = com;
	return 0;
}

static int axi_com_probe(struct platform_device *pdev)
{
	struct axi_com_drv_data *drvdata;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base)) {
		dev_err(&pdev->dev, "Failed to ioremap resource.\n");
		return PTR_ERR(drvdata->base);
	}

	drvdata->dev = &pdev->dev;
	spin_lock_init(&drvdata->ds_msg_list_lock);
	spin_lock_init(&drvdata->us_msg_list_lock);
	INIT_LIST_HEAD(&drvdata->us_msg_head);
	INIT_LIST_HEAD(&drvdata->ds_msg_head);

	axi_com_enable_hw(drvdata);
	ret = axi_com_config_inport(drvdata, true);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, drvdata);
	return axi_com_config_com_descp(pdev, drvdata);
}

static int axi_com_remove(struct platform_device *pdev)
{
	struct axi_com_drv_data *drvdata = platform_get_drvdata(pdev);
	int ret;

	if (ultrasoc_unregister_com(drvdata->com) == -EBUSY)
		return -EBUSY;

	ret = axi_com_config_inport(drvdata, false);
	if (ret)
		return ret;

	axi_com_disable_hw(drvdata);
	usmsg_list_realse_all(&drvdata->ds_msg_head);
	usmsg_list_realse_all(&drvdata->us_msg_head);

	return 0;
}

static const struct acpi_device_id ultrasoc_axi_com_acpi_match[] = {
	{"HISI03B1", },
	{},
};

static struct platform_driver axi_com_driver = {
	.driver = {
		.name = "ultrasoc,axi-com",
		.acpi_match_table = ultrasoc_axi_com_acpi_match,
	},
	.probe = axi_com_probe,
	.remove = axi_com_remove,
};
module_platform_driver(axi_com_driver);

MODULE_DESCRIPTION("Ultrasoc AXI COM driver");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Jonathan Zhou <jonathan.zhouwen@huawei.com>");
MODULE_AUTHOR("Qi Liu <liuqi115@huawei.com>");
