// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021-2022 NVIDIA Corporation
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
 *
 * Device tree snippet to activate this driver:
 *	tegra_hte_irq_test {
 *		compatible = "nvidia,tegra194-hte-irq-test";
 *		hardware-timestamps = <&tegra_hte_lic 0x19>;
 *		hardware-timestamp-names = "hte-lic";
 *		status = "okay";
 *	};
 *
 * How to run this test driver (It is tested on Jetson AGX Xavier platform):
 *
 * - From above device tree snippet, it uses 0x19 interrupt which is for
 *   i2c controller 1.
 * - Once loaded, echo 1 >/sys/kernel/tegra_hte_irq_test/en_dis, it makes HTE
 *   request on above IRQ line.
 * - Run i2cdetect -y 1 1>/dev/null, this command will generate i2c bus
 *   transactions which creates timestamp data. The driver print timestamp data
 *   on console as IRQ HW timestamp(1): <timestamp>.
 * - Unloading the driver or echo 0 >/sys/kernel/tegra_hte_irq_test/en_dis
 *   disables the HTE.
 */

static struct tegra_hte_test {
	struct hte_ts_desc desc;
	struct kobject *kobj;
	struct device *pdev;
} hte;

static hte_return_t process_hw_ts(struct hte_ts_data *ts, void *p)
{
	(void)p;

	if (!ts)
		return HTE_CB_HANDLED;

	dev_info(hte.pdev, "IRQ HW timestamp(%llu): %llu\n", ts->seq, ts->tsc);

	return HTE_CB_HANDLED;
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
	(void)kobj;
	(void)attr;

	if (kstrtoul(buf, 10, &val) < 0) {
		ret = -EINVAL;
		goto error;
	}

	if (val == 1) {
		ret = devm_of_hte_request_ts_ns(hte.pdev, &hte.desc,
						process_hw_ts, NULL, NULL);
		if (ret)
			goto error;

		hte_get_clk_src_info(&hte.desc, &ci);
		dev_info(hte.pdev, "clk rate:%llu, clk type: %d\n",
			 ci.hz, ci.type);
	} else if (val == 0) {
		ret = hte_release_ts(&hte.desc);
		if (ret)
			goto error;
	}

	ret = count;

error:
	return ret;
}

struct kobj_attribute en_dis_attr =
		__ATTR(en_dis, 0220, NULL, store_en_dis);

static struct attribute *attrs[] = {
	&en_dis_attr.attr,
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

	if (of_property_read_string(hte.pdev->of_node,
	    "hardware-timestamp-names", &hte.desc.attr.name))
		hte.desc.attr.name = NULL;

	ret = tegra_hte_test_sysfs_create();
	if (ret != 0) {
		dev_err(hte.pdev, "sysfs creation failed\n");
		return -ENXIO;
	}

	return 0;
}

static int tegra_hte_test_remove(struct platform_device *pdev)
{
	(void)pdev;

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
