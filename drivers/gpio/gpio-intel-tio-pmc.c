// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Time-Aware GPIO Controller Driver
 * Copyright (C) 2021 Intel Corporation
 */

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <uapi/linux/gpio.h>

#define TGPIOCTL		0x00
#define TGPIOCOMPV31_0		0x10
#define TGPIOCOMPV63_32		0x14
#define TGPIOPIV31_0		0x18
#define TGPIOPIV63_32		0x1c
#define TGPIOTCV31_0		0x20
#define TGPIOTCV63_32		0x24 /* Not used */
#define TGPIOECCV31_0		0x28
#define TGPIOECCV63_32		0x2c
#define TGPIOEC31_0		0x30
#define TGPIOEC63_32		0x34

/* Control Register */
#define TGPIOCTL_EN			BIT(0)
#define TGPIOCTL_DIR			BIT(1)
#define TGPIOCTL_EP			GENMASK(3, 2)
#define TGPIOCTL_EP_RISING_EDGE		(0 << 2)
#define TGPIOCTL_EP_FALLING_EDGE	BIT(2)
#define TGPIOCTL_EP_TOGGLE_EDGE		BIT(3)
#define TGPIOCTL_PM			BIT(4)

#define DRIVER_NAME		"intel-pmc-tio"
#define GPIO_COUNT		1
#define INPUT_SNAPSHOT_FREQ	8
#define INPUT_SNAPSHOT_COUNT	3

struct intel_pmc_tio_chip {
	struct gpio_chip gch;
	struct platform_device *pdev;
	struct dentry *root;
	struct debugfs_regset32 *regset;
	void __iomem *base;
	struct mutex lock;		/* Protects 'ctrl', time */
	struct delayed_work input_work;
	bool input_work_running;
	bool systime_valid;
	unsigned int systime_index;
	struct system_time_snapshot systime_snapshot[INPUT_SNAPSHOT_COUNT];
	u64 last_event_count;
	u64 last_art_timestamp;
};

struct intel_pmc_tio_get_time_arg {
	struct intel_pmc_tio_chip *tio;
	u32 eflags;
	u32 event_id;
	u64 abs_event_count;
};

#define gch_to_intel_pmc_tio(i)					\
	(container_of((i), struct intel_pmc_tio_chip, gch))

#define inws_to_intel_pmc_tio(i)					\
	(container_of((i), struct intel_pmc_tio_chip, input_work.work))

static const struct debugfs_reg32 intel_pmc_tio_regs[] = {
	{
		.name = "TGPIOCTL",
		.offset = TGPIOCTL
	},
	{
		.name = "TGPIOCOMPV31_0",
		.offset = TGPIOCOMPV31_0
	},
	{
		.name = "TGPIOCOMPV63_32",
		.offset = TGPIOCOMPV63_32
	},
	{
		.name = "TGPIOPIV31_0",
		.offset = TGPIOPIV31_0
	},
	{
		.name = "TGPIOPIV63_32",
		.offset = TGPIOPIV63_32
	},
	{
		.name = "TGPIOECCV31_0",
		.offset = TGPIOECCV31_0
	},
	{
		.name = "TGPIOECCV63_32",
		.offset = TGPIOECCV63_32
	},
	{
		.name = "TGPIOEC31_0",
		.offset = TGPIOEC31_0
	},
	{
		.name = "TGPIOEC63_32",
		.offset = TGPIOEC63_32
	},
};

static inline u32 intel_pmc_tio_readl(struct intel_pmc_tio_chip *tio,
				      u32 offset)
{
	return readl(tio->base + offset);
}

static inline void intel_pmc_tio_writel(struct intel_pmc_tio_chip *tio,
					u32 offset, u32 value)
{
	writel(value, tio->base + offset);
}

#define INTEL_PMC_TIO_RD_REG(offset)(			\
		intel_pmc_tio_readl((tio), (offset)))
#define INTEL_PMC_TIO_WR_REG(offset, value)(			\
		intel_pmc_tio_writel((tio), (offset), (value)))

static void intel_pmc_tio_enable_input(struct intel_pmc_tio_chip *tio,
				       u32 eflags)
{
	bool rising, falling;
	u32 ctrl;

	/* Disable */
	ctrl = INTEL_PMC_TIO_RD_REG(TGPIOCTL);
	ctrl &= ~TGPIOCTL_EN;
	INTEL_PMC_TIO_WR_REG(TGPIOCTL, ctrl);

	tio->last_event_count = 0;

	/* Configure Input */
	ctrl |= TGPIOCTL_DIR;
	ctrl &= ~TGPIOCTL_EP;

	rising = eflags & GPIO_V2_LINE_FLAG_EDGE_RISING;
	falling = eflags & GPIO_V2_LINE_FLAG_EDGE_FALLING;
	if (rising && falling)
		ctrl |= TGPIOCTL_EP_TOGGLE_EDGE;
	else if (rising)
		ctrl |= TGPIOCTL_EP_RISING_EDGE;
	else
		ctrl |= TGPIOCTL_EP_FALLING_EDGE;

	/* Enable */
	INTEL_PMC_TIO_WR_REG(TGPIOCTL, ctrl);
	ctrl |= TGPIOCTL_EN;
	INTEL_PMC_TIO_WR_REG(TGPIOCTL, ctrl);
}

static void intel_pmc_tio_input_work(struct work_struct *input_work)
{
	struct intel_pmc_tio_chip *tio = inws_to_intel_pmc_tio(input_work);

	mutex_lock(&tio->lock);

	tio->systime_index = (tio->systime_index + 1) % INPUT_SNAPSHOT_COUNT;
	if (tio->systime_index == INPUT_SNAPSHOT_COUNT - 1)
		tio->systime_valid = true;
	ktime_get_snapshot(&tio->systime_snapshot[tio->systime_index]);
	schedule_delayed_work(&tio->input_work, HZ / INPUT_SNAPSHOT_FREQ);

	mutex_unlock(&tio->lock);
}

static void intel_pmc_tio_start_input_work(struct intel_pmc_tio_chip *tio)
{
	if (tio->input_work_running)
		return;

	tio->systime_index = 0;
	tio->systime_valid = false;
	ktime_get_snapshot(&tio->systime_snapshot[tio->systime_index]);

	schedule_delayed_work(&tio->input_work, HZ / INPUT_SNAPSHOT_FREQ);
	tio->input_work_running = true;
}

static void intel_pmc_tio_stop_input_work(struct intel_pmc_tio_chip *tio)
{
	if (!tio->input_work_running)
		return;

	cancel_delayed_work_sync(&tio->input_work);
	tio->input_work_running = false;
}

static int intel_pmc_tio_setup_poll(struct gpio_chip *chip, unsigned int offset,
				    u32 *eflags)
{
	struct intel_pmc_tio_chip *tio;

	if (offset != 0)
		return -EINVAL;

	tio = gch_to_intel_pmc_tio(chip);

	mutex_lock(&tio->lock);
	intel_pmc_tio_start_input_work(tio);
	intel_pmc_tio_enable_input(tio, *eflags);
	mutex_unlock(&tio->lock);

	return 0;
}

static int intel_pmc_tio_get_time(ktime_t *device_time,
				  struct system_counterval_t *system_counterval,
				  void *ctx)
{
	struct intel_pmc_tio_get_time_arg *arg = (typeof(arg))ctx;
	struct intel_pmc_tio_chip *tio = arg->tio;
	u32 flags = arg->eflags;
	u64 abs_event_count;
	u32 rel_event_count;
	u64 art_timestamp;
	u32 dt_hi_s;
	u32 dt_hi_e;
	int err = 0;
	u32 dt_lo;

	/* Upper 64 bits of TCV are unlocked, don't use */
	dt_hi_s = read_art_time() >> 32;
	dt_lo = INTEL_PMC_TIO_RD_REG(TGPIOTCV31_0);
	abs_event_count = INTEL_PMC_TIO_RD_REG(TGPIOECCV63_32);
	abs_event_count <<= 32;
	abs_event_count |= INTEL_PMC_TIO_RD_REG(TGPIOECCV31_0);
	dt_hi_e = read_art_time() >> 32;

	art_timestamp = ((dt_hi_e != dt_hi_s) && !(dt_lo & 0x80000000)) ?
			 dt_hi_e : dt_hi_s;
	art_timestamp <<= 32;
	art_timestamp |= dt_lo;

	rel_event_count = abs_event_count - tio->last_event_count;
	if (rel_event_count == 0 || art_timestamp == tio->last_art_timestamp) {
		err = -EAGAIN;
		goto out;
	}

	tio->last_art_timestamp = art_timestamp;

	*system_counterval = convert_art_to_tsc(art_timestamp);
	arg->abs_event_count = abs_event_count;
	arg->event_id = 0;
	arg->event_id |= (flags & GPIO_V2_LINE_FLAG_EDGE_RISING) ?
		GPIO_V2_LINE_EVENT_RISING_EDGE : 0;
	arg->event_id |= (flags & GPIO_V2_LINE_FLAG_EDGE_FALLING) ?
		GPIO_V2_LINE_EVENT_FALLING_EDGE : 0;

out:
	return err;
}

static int intel_pmc_tio_do_poll(struct gpio_chip *chip, unsigned int offset,
				 u32 eflags, struct gpioevent_poll_data *data)
{
	struct intel_pmc_tio_chip *tio = gch_to_intel_pmc_tio(chip);
	struct intel_pmc_tio_get_time_arg arg = {
		.eflags = eflags, .tio = tio };
	struct system_device_crosststamp xtstamp;
	unsigned int i, stop;
	int err = -EAGAIN;

	mutex_lock(&tio->lock);

	i = tio->systime_index;
	stop = tio->systime_valid ?
		tio->systime_index : INPUT_SNAPSHOT_COUNT - 1;
	do {
		err = get_device_system_crosststamp(intel_pmc_tio_get_time,
						    &arg,
						    &tio->systime_snapshot[i],
						    &xtstamp);
		if (!err) {
			data->timestamp = ktime_to_ns(xtstamp.sys_realtime);
			data->id = arg.event_id;
			tio->last_event_count = arg.abs_event_count;
		}
		if (!err || err == -EAGAIN)
			break;
		i = (i + (INPUT_SNAPSHOT_COUNT - 1)) % INPUT_SNAPSHOT_COUNT;
	} while (i != stop);

	mutex_unlock(&tio->lock);

	return err;
}

static int intel_pmc_tio_probe(struct platform_device *pdev)
{
	struct intel_pmc_tio_chip *tio;
	int err;

	tio = devm_kzalloc(&pdev->dev, sizeof(*tio), GFP_KERNEL);
	if (!tio)
		return -ENOMEM;
	tio->pdev = pdev;

	tio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tio->base))
		return PTR_ERR(tio->base);

	tio->regset = devm_kzalloc
		(&pdev->dev, sizeof(*tio->regset), GFP_KERNEL);
	if (!tio->regset)
		return -ENOMEM;

	tio->regset->regs = intel_pmc_tio_regs;
	tio->regset->nregs = ARRAY_SIZE(intel_pmc_tio_regs);
	tio->regset->base = tio->base;

	tio->root = debugfs_create_dir(pdev->name, NULL);
	if (IS_ERR(tio->root))
		return PTR_ERR(tio->root);

	debugfs_create_regset32("regdump", 0444, tio->root, tio->regset);

	tio->gch.label = pdev->name;
	tio->gch.ngpio = GPIO_COUNT;
	tio->gch.base = -1;
	tio->gch.setup_poll = intel_pmc_tio_setup_poll;
	tio->gch.do_poll = intel_pmc_tio_do_poll;

	platform_set_drvdata(pdev, tio);
	mutex_init(&tio->lock);
	INIT_DELAYED_WORK(&tio->input_work, intel_pmc_tio_input_work);

	err = devm_gpiochip_add_data(&pdev->dev, &tio->gch, tio);
	if (err < 0)
		goto out_recurse_remove_tio_root;

	return 0;

out_recurse_remove_tio_root:
	debugfs_remove_recursive(tio->root);

	return err;
}

static int intel_pmc_tio_remove(struct platform_device *pdev)
{
	struct intel_pmc_tio_chip *tio;

	tio = platform_get_drvdata(pdev);
	if (!tio)
		return -ENODEV;

	intel_pmc_tio_stop_input_work(tio);
	mutex_destroy(&tio->lock);
	debugfs_remove_recursive(tio->root);

	return 0;
}

static const struct acpi_device_id intel_pmc_tio_acpi_match[] = {
	{ "INTC1021", 0 }, /* EHL */
	{ "INTC1022", 0 }, /* EHL */
	{ "INTC1023", 0 }, /* TGL */
	{ "INTC1024", 0 }, /* TGL */
	{  }
};

static struct platform_driver intel_pmc_tio_driver = {
	.probe          = intel_pmc_tio_probe,
	.remove         = intel_pmc_tio_remove,
	.driver         = {
		.name                   = DRIVER_NAME,
		.acpi_match_table       = intel_pmc_tio_acpi_match,
	},
};

static int intel_pmc_tio_init(void)
{
	/* To ensure ART to TSC conversion is correct */
	if (!boot_cpu_has(X86_FEATURE_TSC_KNOWN_FREQ))
		return -ENXIO;

	return platform_driver_register(&intel_pmc_tio_driver);
}

static void intel_pmc_tio_exit(void)
{
	platform_driver_unregister(&intel_pmc_tio_driver);
}

module_init(intel_pmc_tio_init);
module_exit(intel_pmc_tio_exit);

MODULE_AUTHOR("Christopher Hall <christopher.s.hall@intel.com>");
MODULE_AUTHOR("Tamal Saha <tamal.saha@intel.com>");
MODULE_AUTHOR("Lakshmi Sowjanya D <lakshmi.sowjanya.d@intel.com>");
MODULE_DESCRIPTION("Intel PMC Time-Aware GPIO Controller Driver");
MODULE_LICENSE("GPL v2");
