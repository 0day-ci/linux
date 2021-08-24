// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Time-Aware GPIO Controller Driver
 * Copyright (C) 2021 Intel Corporation
 */

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
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
	bool output_high;
	unsigned int systime_index;
	struct system_time_snapshot systime_snapshot[INPUT_SNAPSHOT_COUNT];
	u64 last_event_count;
	u64 last_art_timestamp;
	u64 last_art_period;
	u32 half_period;
};

struct intel_pmc_tio_pwm {
	struct pwm_chip pch;
	struct intel_pmc_tio_chip *tio;
	struct gpio_desc *gpiod;
};

struct intel_pmc_tio_get_time_arg {
	struct intel_pmc_tio_chip *tio;
	u32 eflags;
	u32 event_id;
	u32 event_count;
	u64 abs_event_count;
};

#define pch_to_intel_pmc_tio_pwm(i) \
	(container_of((i), struct intel_pmc_tio_pwm, pch))

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

/* Must hold mutex */
static u32 intel_pmc_tio_disable(struct intel_pmc_tio_chip *tio)
{
	u32 ctrl;
	u64 art;

	ctrl = INTEL_PMC_TIO_RD_REG(TGPIOCTL);
	if (!(ctrl & TGPIOCTL_DIR) && ctrl & TGPIOCTL_EN) {
		/* 'compare' value is invalid */
		art = read_art_time();
		--art;
		INTEL_PMC_TIO_WR_REG(TGPIOCOMPV31_0, art & 0xFFFFFFFF);
		INTEL_PMC_TIO_WR_REG(TGPIOCOMPV63_32, art >> 32);
		udelay(1);
		tio->output_high = (INTEL_PMC_TIO_RD_REG(TGPIOEC31_0) & 0x1);
	}

	if (ctrl & TGPIOCTL_EN) {
		ctrl &= ~TGPIOCTL_EN;
		INTEL_PMC_TIO_WR_REG(TGPIOCTL, ctrl);
	}

	return ctrl;
}

static void intel_pmc_tio_enable(struct intel_pmc_tio_chip *tio, u32 ctrl)
{
	INTEL_PMC_TIO_WR_REG(TGPIOCTL, ctrl);
	ctrl |= TGPIOCTL_EN;
	INTEL_PMC_TIO_WR_REG(TGPIOCTL, ctrl);
}

static void intel_pmc_tio_enable_input(struct intel_pmc_tio_chip *tio,
				       u32 eflags)
{
	bool rising, falling;
	u32 ctrl;

	/* Disable */
	ctrl = INTEL_PMC_TIO_RD_REG(TGPIOCTL);

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
	intel_pmc_tio_enable(tio, ctrl);
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
	arg->event_count = rel_event_count;
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
			data->event_count = arg.event_count;
		}
		if (!err || err == -EAGAIN)
			break;
		i = (i + (INPUT_SNAPSHOT_COUNT - 1)) % INPUT_SNAPSHOT_COUNT;
	} while (i != stop);

	mutex_unlock(&tio->lock);

	return err;
}

static int intel_pmc_tio_insert_edge(struct intel_pmc_tio_chip *tio, u32 *ctrl)
{
	struct system_counterval_t sys_counter;
	ktime_t trigger;
	int err;
	u64 art;

	trigger = ktime_get_real();
	trigger = ktime_add_ns(trigger, NSEC_PER_SEC / 20);

	err = ktime_convert_real_to_system_counter(trigger, &sys_counter);
	if (err)
		return err;

	err = convert_tsc_to_art(&sys_counter, &art);
	if (err)
		return err;

	/* In disabled state */
	*ctrl &= ~(TGPIOCTL_DIR | TGPIOCTL_PM);
	*ctrl &= ~TGPIOCTL_EP;
	*ctrl |= TGPIOCTL_EP_TOGGLE_EDGE;

	INTEL_PMC_TIO_WR_REG(TGPIOCOMPV31_0, art & 0xFFFFFFFF);
	INTEL_PMC_TIO_WR_REG(TGPIOCOMPV63_32, art >> 32);

	intel_pmc_tio_enable(tio, *ctrl);

	/* sleep for 100 milli-second */
	msleep(2 * (MSEC_PER_SEC / 20));

	*ctrl = intel_pmc_tio_disable(tio);

	return 0;
}

static int _intel_pmc_tio_direction_output(struct intel_pmc_tio_chip *tio,
					   u32 offset, int value,
					   u64 period)
{
	u32 ctrl;
	int err;
	u64 art;

	if (value)
		return -EINVAL;

	ctrl = intel_pmc_tio_disable(tio);

	/*
	 * Make sure the output is zero'ed by inserting an edge as needed
	 * Only need to worry about this when restarting output
	 */
	if (tio->output_high) {
		err = intel_pmc_tio_insert_edge(tio, &ctrl);
		if (err)
			return err;
		tio->output_high = false;
	}

	/* Enable the device, be sure that the 'compare(COMPV)' value is invalid */
	art = read_art_time();
	--art;
	INTEL_PMC_TIO_WR_REG(TGPIOCOMPV31_0, art & 0xFFFFFFFF);
	INTEL_PMC_TIO_WR_REG(TGPIOCOMPV63_32, art >> 32);

	ctrl &= ~(TGPIOCTL_DIR | TGPIOCTL_PM);
	if (period != 0) {
		ctrl |= TGPIOCTL_PM;
		INTEL_PMC_TIO_WR_REG(TGPIOPIV31_0, period & 0xFFFFFFFF);
		INTEL_PMC_TIO_WR_REG(TGPIOPIV63_32, period >> 32);
	}

	ctrl &= ~TGPIOCTL_EP;
	ctrl |= TGPIOCTL_EP_TOGGLE_EDGE;

	intel_pmc_tio_enable(tio, ctrl);

	return 0;
}

static int intel_pmc_tio_direction_output(struct gpio_chip *chip,
					  unsigned int offset, int value)
{
	struct intel_pmc_tio_chip *tio = gch_to_intel_pmc_tio(chip);
	int ret;

	mutex_lock(&tio->lock);
	ret =  _intel_pmc_tio_direction_output(tio, offset, value, 0);
	mutex_unlock(&tio->lock);

	return ret;
}

static int _intel_pmc_tio_generate_output(struct intel_pmc_tio_chip *tio,
					  unsigned int offset, u64 timestamp)
{
	struct system_counterval_t sys_counter;
	ktime_t sys_realtime;
	u64 art_timestamp;
	int err;

	if (timestamp != 0) {
		sys_realtime = ns_to_ktime(timestamp);
	} else {
		sys_realtime = ktime_get_real();
		sys_realtime = ktime_add_ns(sys_realtime, NSEC_PER_SEC / 20);
	}

	err = ktime_convert_real_to_system_counter(sys_realtime, &sys_counter);
	if (err)
		return err;

	err = convert_tsc_to_art(&sys_counter, &art_timestamp);
	if (err)
		return err;

	INTEL_PMC_TIO_WR_REG(TGPIOCOMPV63_32, art_timestamp >> 32);
	INTEL_PMC_TIO_WR_REG(TGPIOCOMPV31_0, art_timestamp);

	return 0;
}

static int intel_pmc_tio_generate_output(struct gpio_chip *chip,
					 unsigned int offset,
					 struct gpio_output_event_data *output_data)
{
	struct intel_pmc_tio_chip *tio = gch_to_intel_pmc_tio(chip);
	int ret;

	mutex_lock(&tio->lock);
	ret =  _intel_pmc_tio_generate_output
		(tio, offset, output_data->timestamp);
	mutex_unlock(&tio->lock);

	return ret;
}

static int intel_pmc_tio_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct intel_pmc_tio_pwm *tio_pwm = pch_to_intel_pmc_tio_pwm(chip);
	struct intel_pmc_tio_chip *tio = tio_pwm->tio;
	int ret = 0;

	mutex_lock(&tio->lock);

	if (tio_pwm->gpiod) {
		ret = -EBUSY;
	} else {
		struct gpio_desc *gpiod;

		gpiod = gpiochip_request_own_desc
			(&tio->gch, pwm->hwpwm, "intel-pmc-tio-pwm", 0, 0);
		if (IS_ERR(gpiod)) {
			ret = PTR_ERR(gpiod);
			goto out;
		}

		tio_pwm->gpiod = gpiod;
	}

out:
	mutex_unlock(&tio->lock);
	return ret;
}

#define MIN_ART_PERIOD (3)

static int intel_pmc_tio_pwm_apply(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   const struct pwm_state *state)
{
	struct intel_pmc_tio_pwm *tio_pwm = pch_to_intel_pmc_tio_pwm(chip);
	struct intel_pmc_tio_chip *tio = tio_pwm->tio;
	bool start_output, change_period;
	u64 art_period;
	int ret = 0;

	/* Only support 'normal' polarity */
	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	mutex_lock(&tio->lock);

	if (!state->enabled) {
		if (pwm->state.enabled) {
			intel_pmc_tio_disable(tio);
			pwm->state.enabled = false;
		}
	}

	/* 50% duty cycle only */
	if (pwm->state.period != state->period &&
	    pwm->state.duty_cycle != state->duty_cycle &&
	    state->duty_cycle != state->period / 2) {
		ret = -EINVAL;
		goto out;
	}

	change_period = state->period != pwm->state.period ||
		state->duty_cycle != pwm->state.duty_cycle ? state->enabled : false;

	if (pwm->state.period != state->period) {
		pwm->state.period = state->period;
		pwm->state.duty_cycle = state->period / 2;
	} else if (pwm->state.duty_cycle != state->duty_cycle) {
		pwm->state.duty_cycle = state->duty_cycle;
		pwm->state.period = state->duty_cycle * 2;
	}

	start_output = state->enabled && !pwm->state.enabled;
	if (start_output || change_period) {
		art_period = convert_art_ns_to_art(pwm->state.duty_cycle);
		if (art_period < MIN_ART_PERIOD) {
			ret = -EINVAL;
			goto out;
		}
		tio->half_period = pwm->state.duty_cycle;
	}

	if (start_output) {
		u64 start_time;
		u32 nsec;

		pwm->state.enabled = true;
		start_time = ktime_get_real_ns();
		div_u64_rem(start_time, NSEC_PER_SEC, &nsec);
		start_time -= nsec;
		start_time += 2 * NSEC_PER_SEC;
		_intel_pmc_tio_direction_output(tio, pwm->hwpwm, 0, art_period);
		ret = _intel_pmc_tio_generate_output(tio, pwm->hwpwm,
						     start_time);
		if (ret)
			goto out;
	} else if (change_period && tio->last_art_period != art_period) {
		INTEL_PMC_TIO_WR_REG(TGPIOPIV31_0, art_period & 0xFFFFFFFF);
		INTEL_PMC_TIO_WR_REG(TGPIOPIV63_32, art_period >> 32);
		tio->last_art_period = art_period;
	}

out:
	mutex_unlock(&tio->lock);

	return ret;
}

/* Get initial state */
static void intel_pmc_tio_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
					struct pwm_state *state)
{
	struct intel_pmc_tio_pwm *tio_pwm = pch_to_intel_pmc_tio_pwm(chip);
	struct intel_pmc_tio_chip *tio = tio_pwm->tio;
	u32 ctrl;

	mutex_lock(&tio->lock);

	ctrl = INTEL_PMC_TIO_RD_REG(TGPIOCTL);
	state->enabled = ctrl & TGPIOCTL_EN && ctrl & TGPIOCTL_PM &&
			!(ctrl & TGPIOCTL_DIR) ? true : false;

	state->duty_cycle = tio->half_period;
	state->period = state->duty_cycle * 2;

	mutex_unlock(&tio->lock);
}

static void intel_pmc_tio_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct intel_pmc_tio_pwm *tio_pwm = pch_to_intel_pmc_tio_pwm(chip);
	struct intel_pmc_tio_chip *tio = tio_pwm->tio;

	tio->half_period = pwm->state.duty_cycle;

	gpiochip_free_own_desc(tio_pwm->gpiod);
	tio_pwm->gpiod = NULL;
}

static const struct pwm_ops intel_pmc_tio_pwm_ops = {
	.request = intel_pmc_tio_pwm_request,
	.free = intel_pmc_tio_pwm_free,
	.apply = intel_pmc_tio_pwm_apply,
	.get_state = intel_pmc_tio_pwm_get_state,
	.owner = THIS_MODULE,
};

static int intel_pmc_tio_probe(struct platform_device *pdev)
{
	struct intel_pmc_tio_pwm *tio_pwm;
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
	tio->gch.generate_output = intel_pmc_tio_generate_output;
	tio->gch.direction_output = intel_pmc_tio_direction_output;

	platform_set_drvdata(pdev, tio);
	mutex_init(&tio->lock);
	INIT_DELAYED_WORK(&tio->input_work, intel_pmc_tio_input_work);
	tio->output_high = false;

	err = devm_gpiochip_add_data(&pdev->dev, &tio->gch, tio);
	if (err < 0)
		goto out_recurse_remove_tio_root;

	tio_pwm = devm_kzalloc(&pdev->dev, sizeof(*tio_pwm), GFP_KERNEL);
	if (!tio_pwm) {
		err = -ENOMEM;
		goto out_recurse_remove_tio_root;
	}

	tio_pwm->tio = tio;
	tio_pwm->pch.dev = &pdev->dev;
	tio_pwm->pch.ops = &intel_pmc_tio_pwm_ops;
	tio_pwm->pch.npwm = GPIO_COUNT;
	tio_pwm->pch.base = -1;

	err = pwmchip_add(&tio_pwm->pch);
	if (err)
		goto out_recurse_remove_tio_root;

	/* Make sure tio and device state are sync'd to a reasonable value */
	tio->half_period = NSEC_PER_SEC / 2;

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
