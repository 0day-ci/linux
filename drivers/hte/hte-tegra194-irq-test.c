// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 NVIDIA Corporation
 *
 * Author: Dipen Patel <dipenp@nvidia.com>
 */

#include <linux/version.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/hte.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

/*
 * Tegra194 On chip HTE (hardware timestamping engine) also known as GTE
 * (generic timestamping engine) can monitor LIC (Legacy interrupt controller)
 * IRQ lines for the event and timestamp accordingly in realtime. Follow
 * technical reference manual for the IRQ numbers and descriptions.
 *
 * This sample HTE IRQ test driver demonstrating HTE API usage by enabling
 * lic irq line in HTE to monitor and timestamp.
 */

/*
 * Used to increase line buffer length to this power in case of the dropped
 * timestamps.
 */
static unsigned int len_pow = 2;
module_param(len_pow, uint, 0660);

static struct tegra_hte_test {
	size_t buf_len;
	bool update_buf_len;
	struct hte_ts_desc *desc;
	struct work_struct ev_work;
	struct kobject *kobj;
	struct device *pdev;
} hte;

static void hte_callback(enum hte_notify n)
{
	if (n == HTE_TS_AVAIL) {
		hte.update_buf_len = false;
	} else if (n == HTE_TS_DROPPED) {
		dev_info(hte.pdev, "Timestamp dropped\n");
		hte.update_buf_len = true;
	} else {
		dev_dbg(hte.pdev, "Wrong notify value (%d)\n", n);
		return;
	}
	schedule_work(&hte.ev_work);
}

static void tegra_hte_irq_get_ts(void)
{
	int ret, i;
	size_t avail;
	struct hte_ts_data *el;

	avail = hte_available_ts(hte.desc);

	/*
	 * Workqueue only got scheduled from the hte_callback, it is highly
	 * unlikely that there is no timestamp to retrieve.
	 */
	if (unlikely(!avail)) {
		dev_dbg(hte.pdev, "timestamp not available\n");
		goto error;
	}

	el = kzalloc(avail * sizeof(*el), GFP_KERNEL);
	if (!el) {
		dev_dbg(hte.pdev, "Can not allocate %lu bytes memory\n",
			avail * sizeof(*el));
		/*
		 * We have two options here:
		 * 1. Release the line as system is running low memory.
		 * 2. Run an loop to retrieve an element till its drained.
		 *
		 * We will use 1st option.
		 */
		goto error;
	}

	ret = hte_retrieve_ts_ns_wait(hte.desc, el, avail);
	if (ret < 0) {
		dev_dbg(hte.pdev,
			"Something went wrong retrieving timestamp data\n");
		kfree(el);
		goto error;
	}

	for (i = 0; i < avail; i++) {
		dev_info(hte.pdev, "IRQ HW timestamp(%llu): %llu, edge: %s\n",
			 el[i].seq, el[i].tsc,
			 (el[i].dir == 1) ? "rising" : "falling");
	}

	kfree(el);

	return;

error:
	hte_release_ts(hte.desc);
}

static void hte_ts_work(struct work_struct *data)
{
	size_t temp;
	int ret;
	(void) data;

	if (hte.update_buf_len) {
		ret = hte_disable_ts(hte.desc);
		if (ret) {
			dev_err(hte.pdev, "Not able to disable line\n");
			goto error;
		}

		temp = hte.buf_len * len_pow;
		ret = hte_set_buf_len(hte.desc, temp);
		if (ret) {
			dev_err(hte.pdev, "Not able to set new buf len (%lu)\n",
				temp);
			goto error;
		}

		hte.buf_len = hte_get_buf_len(hte.desc);
		if (unlikely(hte.buf_len != temp)) {
			dev_err(hte.pdev, "New length is (%lu) != (%lu)\n",
				hte.buf_len, temp);
			goto error;
		}

		dev_dbg(hte.pdev, "New buffer length (%lu)\n", hte.buf_len);
		hte.update_buf_len = false;
		ret = hte_enable_ts(hte.desc);
		if (ret) {
			dev_err(hte.pdev, "failed to enable line\n");
			goto error;
		}

		return;
	}

	tegra_hte_irq_get_ts();

	return;

error:
	hte_release_ts(hte.desc);
}

/*
 * Sysfs attribute to request/release HTE IRQ line.
 */
static ssize_t store_en_dis(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	int ret = count;
	unsigned long val = 0;
	struct hte_clk_info ci;

	if (kstrtoul(buf, 10, &val) < 0) {
		ret = -EINVAL;
		goto error;
	}

	if (val == 1) {
		if (hte.desc) {
			ret = -EEXIST;
			goto error;
		}

		hte.desc = devm_of_hte_request_ts(hte.pdev, "hte-lic",
						  hte_callback);
		if (IS_ERR(hte.desc)) {
			ret = PTR_ERR(hte.desc);
			hte.desc = NULL;
			goto error;
		}

		hte_get_clk_src_info(hte.desc, &ci);
		dev_info(hte.pdev, "clk rate:%llu, clk type: %d\n",
			 ci.hz, ci.type);

		hte.buf_len = hte_get_buf_len(hte.desc);
		if (hte.buf_len < 0) {
			ret = hte.buf_len;
			hte_release_ts(hte.desc);
			hte.desc = NULL;
			goto error;
		}
	} else if (val == 0) {
		if (!hte.desc) {
			ret = -EINVAL;
			goto error;
		}
		/*
		 * Ideally, you never need to call this API, simply removing
		 * this module should be enough, it is being called here just
		 * for demonstration.
		 */
		ret = devm_hte_release_ts(hte.pdev, hte.desc);
		if (ret)
			goto error;

		hte.desc = NULL;
	}

	ret = count;

error:
	return ret;
}

struct kobj_attribute en_dis_attr =
		__ATTR(en_dis, 0220, NULL, store_en_dis);

/*
 * Sysfs attribute to set/get watermark.
 */
static ssize_t store_watermark(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	int ret = count;
	size_t val = 0;

	if (kstrtoul(buf, 10, &val) < 0) {
		ret = -EINVAL;
		goto error;
	}

	if (hte.desc) {
		ret = hte_set_buf_watermark(hte.desc, val);
		if (ret < 0)
			goto error;
	} else {
		ret = -EINVAL;
		goto error;
	}

	ret = count;

error:
	return ret;
}

static ssize_t show_watermark(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	size_t ret;

	if (hte.desc) {
		ret = hte_get_buf_watermark(hte.desc);
		if (!ret)
			goto error;
	} else {
		goto error;
	}

	return scnprintf(buf, PAGE_SIZE, "%lu\n", ret);

error:
	return -EINVAL;
}

struct kobj_attribute watermark_attr =
		__ATTR(watermark, 0660, show_watermark, store_watermark);

/*
 * Sysfs attribute to set/get buffer.
 */
static ssize_t store_buf_len(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	int ret = count;
	size_t val = 0;

	if (kstrtoul(buf, 10, &val) < 0) {
		ret = -EINVAL;
		goto error;
	}

	if (hte.desc) {
		ret = hte_set_buf_len(hte.desc, val);
		if (ret < 0)
			goto error;
	} else {
		ret = -EINVAL;
		goto error;
	}

	ret = count;

error:
	return ret;
}

static ssize_t show_buf_len(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    char *buf)
{
	size_t ret;

	if (hte.desc) {
		ret = hte_get_buf_len(hte.desc);
		if (!ret)
			goto error;
	} else {
		goto error;
	}

	return scnprintf(buf, PAGE_SIZE, "%lu\n", ret);

error:
	return -EINVAL;
}

struct kobj_attribute buf_len_attr =
		__ATTR(buf_len, 0660, show_buf_len, store_buf_len);

static struct attribute *attrs[] = {
	&en_dis_attr.attr,
	&watermark_attr.attr,
	&buf_len_attr.attr,
	NULL,
};

static struct attribute_group tegra_hte_test_attr_group = {
	.attrs = attrs,
};

static int tegra_hte_test_sysfs_create(void)
{
	int ret;

	/* Creates /sys/kernel/tegra_hte_irq_test */
	hte.kobj = kobject_create_and_add("tegra_hte_irq_test", kernel_kobj);
	if (!hte.kobj)
		return -ENOMEM;

	ret = sysfs_create_group(hte.kobj, &tegra_hte_test_attr_group);
	if (ret)
		kobject_put(hte.kobj);
	return ret;
}

static const struct of_device_id tegra_hte_irq_test_of_match[] = {
	{ .compatible = "nvidia,tegra194-hte-irq-test"},
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_hte_irq_test_of_match);

static int tegra_hte_test_probe(struct platform_device *pdev)
{
	int ret;

	dev_set_drvdata(&pdev->dev, &hte);
	hte.pdev = &pdev->dev;

	ret = tegra_hte_test_sysfs_create();
	if (ret != 0) {
		dev_err(hte.pdev, "sysfs creation failed\n");
		return -ENXIO;
	}

	INIT_WORK(&hte.ev_work, hte_ts_work);

	return 0;
}

static int tegra_hte_test_remove(struct platform_device *pdev)
{
	cancel_work_sync(&hte.ev_work);
	kobject_put(hte.kobj);

	return 0;
}

static struct platform_driver tegra_hte_irq_test_driver = {
	.probe = tegra_hte_test_probe,
	.remove = tegra_hte_test_remove,
	.driver = {
		.name = "tegra_hte_irq_test",
		.of_match_table = tegra_hte_irq_test_of_match,
	},
};
module_platform_driver(tegra_hte_irq_test_driver);

MODULE_AUTHOR("Dipen Patel <dipenp@nvidia.com>");
MODULE_LICENSE("GPL v2");
