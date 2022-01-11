// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support code for Analog Devices Sigma-Delta ADCs
 *
 * Copyright 2012 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/list.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/adc/ad_sigma_delta.h>

#include <asm/unaligned.h>

static irqreturn_t ad_sd_data_rdy_trig_poll(int irq, void *private);

struct ad_sigma_delta_interrupt {
	/* Constant from time of creation */
	int			irq;
	struct spi_master	*master;

	/* Protected by global lock */
	struct list_head	list;
	int			refcnt;

	/* Protected by lock on corresponding SPI bus */
	struct ad_sigma_delta	*active;
};

static DEFINE_MUTEX(interrupt_lock);
static LIST_HEAD(interrupt_list);

static void adsdi_enable(struct ad_sigma_delta *a)
{
	struct ad_sigma_delta_interrupt *intr = a->interrupt;

	WARN_ON(intr->active);
	intr->active = a;
	pr_debug("ad_sigma_delta: enable %d for %p\n", intr->irq, a);
	enable_irq(intr->irq);
}

static void adsdi_disable(struct ad_sigma_delta *a, int nosync)
{
	struct ad_sigma_delta_interrupt *intr = a->interrupt;

	if (nosync)
		disable_irq_nosync(intr->irq);
	else
		disable_irq(intr->irq);

	pr_debug("ad_sigma_delta: disable %d for %p\n", intr->irq, intr->active);

	/* In the case of a timeout, it's possible for adsdi_disable to
	 * be called twice by the same instance (if the interrupt runs
	 * between the call to check and the call to disable).
	 *
	 * We still need to disable first before checking intr->active.
	 * Then we can roll back if we've done it twice.
	 */
	if (intr->active != a) {
		WARN_ON(intr->active);
		pr_debug("ad_sigma_delta: double-disable\n");
		enable_irq(intr->irq);
	}

	intr->active = NULL;
}

static int adsdi_get(struct ad_sigma_delta_interrupt **intr_ret,
		     int irq, struct spi_master *master,
		     int flags)
{
	struct ad_sigma_delta_interrupt *intr = NULL;
	struct list_head *ptr;
	int ret = 0;

	mutex_lock(&interrupt_lock);

	/* Try to find an existing instance */
	list_for_each(ptr, &interrupt_list) {
		struct ad_sigma_delta_interrupt *i = list_entry(ptr,
			struct ad_sigma_delta_interrupt, list);

		if (i->irq == irq) {
			/* No instance will attempt to wait for the
			 * interrupt without the SPI bus locked, which
			 * we can rely on to ensure correct operation.
			 * However, we would like to detect
			 * misconfiguration that would lead to unsafe
			 * access.
			 */
			if (i->master != master) {
				pr_err(
				    "ad_sigma_delta: SPI master mismatch on IRQ %d\n",
				    irq);
				ret = -EINVAL;
				goto fail_search;
			}

			intr = i;
			break;
		}
	}

	/* Allocate a new one if necessary */
	if (!intr) {
		intr = kmalloc(sizeof(*intr), GFP_KERNEL);
		if (!intr) {
			ret = -ENOMEM;
			pr_err("ad_sigma_delta: can't allocate memory\n");
			goto fail_search;
		}

		intr->irq = irq;
		intr->refcnt = 0;
		intr->active = NULL;
		intr->master = master;

		ret = request_irq(irq,
				  ad_sd_data_rdy_trig_poll,
				  flags | IRQF_NO_AUTOEN,
				  "ad_sigma_delta",
				  intr);
		if (ret)
			goto fail_search;

		pr_debug("ad_sigma_delta: sharing interrupt %d\n", irq);
		list_add(&intr->list, &interrupt_list);
	}

	intr->refcnt++;
	*intr_ret = intr;

fail_search:
	mutex_unlock(&interrupt_lock);
	return ret;
}

static void adsdi_put(struct ad_sigma_delta_interrupt *intr)
{
	mutex_lock(&interrupt_lock);
	if (!--intr->refcnt) {
		pr_debug("ad_sigma_delta: interrupt %d deallocated\n",
			intr->irq);
		free_irq(intr->irq, intr);
		list_del(&intr->list);
		kfree(intr);
	}
	mutex_unlock(&interrupt_lock);
}

static void devm_adsdi_release(void *arg)
{
	adsdi_put(arg);
}

static int devm_adsdi_get(struct device *dev,
			  struct ad_sigma_delta_interrupt **intr_ret,
			  int irq, struct spi_master *master,
			  int flags)
{
	const int ret = adsdi_get(intr_ret, irq, master, flags);

	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_adsdi_release, *intr_ret);
}

#define AD_SD_COMM_CHAN_MASK	0x3

#define AD_SD_REG_COMM		0x00
#define AD_SD_REG_DATA		0x03

/**
 * ad_sd_set_comm() - Set communications register
 *
 * @sigma_delta: The sigma delta device
 * @comm: New value for the communications register
 */
void ad_sd_set_comm(struct ad_sigma_delta *sigma_delta, uint8_t comm)
{
	/* Some variants use the lower two bits of the communications register
	 * to select the channel */
	sigma_delta->comm = comm & AD_SD_COMM_CHAN_MASK;
}
EXPORT_SYMBOL_GPL(ad_sd_set_comm);

/**
 * ad_sd_write_reg() - Write a register
 *
 * @sigma_delta: The sigma delta device
 * @reg: Address of the register
 * @size: Size of the register (0-3)
 * @val: Value to write to the register
 *
 * Returns 0 on success, an error code otherwise.
 **/
int ad_sd_write_reg(struct ad_sigma_delta *sigma_delta, unsigned int reg,
	unsigned int size, unsigned int val)
{
	uint8_t *data = sigma_delta->tx_buf;
	struct spi_transfer t = {
		.tx_buf		= data,
		.len		= size + 1,
		.cs_change	= sigma_delta->keep_cs_asserted,
	};
	struct spi_message m;
	int ret;

	data[0] = (reg << sigma_delta->info->addr_shift) | sigma_delta->comm;

	switch (size) {
	case 3:
		put_unaligned_be24(val, &data[1]);
		break;
	case 2:
		put_unaligned_be16(val, &data[1]);
		break;
	case 1:
		data[1] = val;
		break;
	case 0:
		break;
	default:
		return -EINVAL;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	if (sigma_delta->bus_locked)
		ret = spi_sync_locked(sigma_delta->spi, &m);
	else
		ret = spi_sync(sigma_delta->spi, &m);

	return ret;
}
EXPORT_SYMBOL_GPL(ad_sd_write_reg);

static int ad_sd_read_reg_raw(struct ad_sigma_delta *sigma_delta,
	unsigned int reg, unsigned int size, uint8_t *val)
{
	uint8_t *data = sigma_delta->tx_buf;
	int ret;
	struct spi_transfer t[] = {
		{
			.tx_buf = data,
			.len = 1,
		}, {
			.rx_buf = val,
			.len = size,
			.cs_change = sigma_delta->bus_locked,
		},
	};
	struct spi_message m;

	spi_message_init(&m);

	if (sigma_delta->info->has_registers) {
		data[0] = reg << sigma_delta->info->addr_shift;
		data[0] |= sigma_delta->info->read_mask;
		data[0] |= sigma_delta->comm;
		spi_message_add_tail(&t[0], &m);
	}
	spi_message_add_tail(&t[1], &m);

	if (sigma_delta->bus_locked)
		ret = spi_sync_locked(sigma_delta->spi, &m);
	else
		ret = spi_sync(sigma_delta->spi, &m);

	return ret;
}

/**
 * ad_sd_read_reg() - Read a register
 *
 * @sigma_delta: The sigma delta device
 * @reg: Address of the register
 * @size: Size of the register (1-4)
 * @val: Read value
 *
 * Returns 0 on success, an error code otherwise.
 **/
int ad_sd_read_reg(struct ad_sigma_delta *sigma_delta,
	unsigned int reg, unsigned int size, unsigned int *val)
{
	int ret;

	ret = ad_sd_read_reg_raw(sigma_delta, reg, size, sigma_delta->rx_buf);
	if (ret < 0)
		goto out;

	switch (size) {
	case 4:
		*val = get_unaligned_be32(sigma_delta->rx_buf);
		break;
	case 3:
		*val = get_unaligned_be24(sigma_delta->rx_buf);
		break;
	case 2:
		*val = get_unaligned_be16(sigma_delta->rx_buf);
		break;
	case 1:
		*val = sigma_delta->rx_buf[0];
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(ad_sd_read_reg);

/**
 * ad_sd_reset() - Reset the serial interface
 *
 * @sigma_delta: The sigma delta device
 * @reset_length: Number of SCLKs with DIN = 1
 *
 * Returns 0 on success, an error code otherwise.
 **/
int ad_sd_reset(struct ad_sigma_delta *sigma_delta,
	unsigned int reset_length)
{
	uint8_t *buf;
	unsigned int size;
	int ret;

	size = DIV_ROUND_UP(reset_length, 8);
	buf = kcalloc(size, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memset(buf, 0xff, size);
	ret = spi_write(sigma_delta->spi, buf, size);
	kfree(buf);

	return ret;
}
EXPORT_SYMBOL_GPL(ad_sd_reset);

int ad_sd_calibrate(struct ad_sigma_delta *sigma_delta,
	unsigned int mode, unsigned int channel)
{
	int ret;
	unsigned long timeout;

	ret = ad_sigma_delta_set_channel(sigma_delta, channel);
	if (ret)
		return ret;

	spi_bus_lock(sigma_delta->spi->master);
	sigma_delta->bus_locked = true;
	sigma_delta->keep_cs_asserted = true;
	reinit_completion(&sigma_delta->completion);

	ret = ad_sigma_delta_set_mode(sigma_delta, mode);
	if (ret < 0)
		goto out;

	sigma_delta->irq_dis = false;
	adsdi_enable(sigma_delta);
	timeout = wait_for_completion_timeout(&sigma_delta->completion, 2 * HZ);
	if (timeout == 0) {
		sigma_delta->irq_dis = true;
		adsdi_disable(sigma_delta, 0);
		ret = -EIO;
	} else {
		ret = 0;
	}
out:
	sigma_delta->keep_cs_asserted = false;
	ad_sigma_delta_set_mode(sigma_delta, AD_SD_MODE_IDLE);
	sigma_delta->bus_locked = false;
	spi_bus_unlock(sigma_delta->spi->master);

	return ret;
}
EXPORT_SYMBOL_GPL(ad_sd_calibrate);

/**
 * ad_sd_calibrate_all() - Performs channel calibration
 * @sigma_delta: The sigma delta device
 * @cb: Array of channels and calibration type to perform
 * @n: Number of items in cb
 *
 * Returns 0 on success, an error code otherwise.
 **/
int ad_sd_calibrate_all(struct ad_sigma_delta *sigma_delta,
	const struct ad_sd_calib_data *cb, unsigned int n)
{
	unsigned int i;
	int ret;

	for (i = 0; i < n; i++) {
		ret = ad_sd_calibrate(sigma_delta, cb[i].mode, cb[i].channel);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ad_sd_calibrate_all);

/**
 * ad_sigma_delta_single_conversion() - Performs a single data conversion
 * @indio_dev: The IIO device
 * @chan: The conversion is done for this channel
 * @val: Pointer to the location where to store the read value
 *
 * Returns: 0 on success, an error value otherwise.
 */
int ad_sigma_delta_single_conversion(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *val)
{
	struct ad_sigma_delta *sigma_delta = iio_device_get_drvdata(indio_dev);
	unsigned int sample, raw_sample;
	unsigned int data_reg;
	int ret = 0;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	mutex_lock(&indio_dev->mlock);
	ad_sigma_delta_set_channel(sigma_delta, chan->address);

	spi_bus_lock(sigma_delta->spi->master);
	sigma_delta->bus_locked = true;
	sigma_delta->keep_cs_asserted = true;
	reinit_completion(&sigma_delta->completion);

	ad_sigma_delta_set_mode(sigma_delta, AD_SD_MODE_SINGLE);

	sigma_delta->irq_dis = false;
	adsdi_enable(sigma_delta);
	ret = wait_for_completion_interruptible_timeout(
			&sigma_delta->completion, HZ);

	if (ret == 0)
		ret = -EIO;
	if (ret < 0)
		goto out;

	if (sigma_delta->info->data_reg != 0)
		data_reg = sigma_delta->info->data_reg;
	else
		data_reg = AD_SD_REG_DATA;

	ret = ad_sd_read_reg(sigma_delta, data_reg,
		DIV_ROUND_UP(chan->scan_type.realbits + chan->scan_type.shift, 8),
		&raw_sample);

out:
	if (!sigma_delta->irq_dis) {
		adsdi_disable(sigma_delta, 0);
		sigma_delta->irq_dis = true;
	}

	sigma_delta->keep_cs_asserted = false;
	ad_sigma_delta_set_mode(sigma_delta, AD_SD_MODE_IDLE);
	sigma_delta->bus_locked = false;
	spi_bus_unlock(sigma_delta->spi->master);
	mutex_unlock(&indio_dev->mlock);

	if (ret)
		return ret;

	sample = raw_sample >> chan->scan_type.shift;
	sample &= (1 << chan->scan_type.realbits) - 1;
	*val = sample;

	ret = ad_sigma_delta_postprocess_sample(sigma_delta, raw_sample);
	if (ret)
		return ret;

	return IIO_VAL_INT;
}
EXPORT_SYMBOL_GPL(ad_sigma_delta_single_conversion);

static int ad_sd_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad_sigma_delta *sigma_delta = iio_device_get_drvdata(indio_dev);
	unsigned int channel;
	int ret;

	channel = find_first_bit(indio_dev->active_scan_mask,
				 indio_dev->masklength);
	ret = ad_sigma_delta_set_channel(sigma_delta,
		indio_dev->channels[channel].address);
	if (ret)
		return ret;

	spi_bus_lock(sigma_delta->spi->master);
	sigma_delta->bus_locked = true;
	sigma_delta->keep_cs_asserted = true;

	ret = ad_sigma_delta_set_mode(sigma_delta, AD_SD_MODE_CONTINUOUS);
	if (ret)
		goto err_unlock;

	sigma_delta->irq_dis = false;
	adsdi_enable(sigma_delta);

	return 0;

err_unlock:
	spi_bus_unlock(sigma_delta->spi->master);

	return ret;
}

static int ad_sd_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad_sigma_delta *sigma_delta = iio_device_get_drvdata(indio_dev);

	reinit_completion(&sigma_delta->completion);
	wait_for_completion_timeout(&sigma_delta->completion, HZ);

	if (!sigma_delta->irq_dis) {
		adsdi_disable(sigma_delta, 0);
		sigma_delta->irq_dis = true;
	}

	sigma_delta->keep_cs_asserted = false;
	ad_sigma_delta_set_mode(sigma_delta, AD_SD_MODE_IDLE);

	sigma_delta->bus_locked = false;
	return spi_bus_unlock(sigma_delta->spi->master);
}

static irqreturn_t ad_sd_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad_sigma_delta *sigma_delta = iio_device_get_drvdata(indio_dev);
	uint8_t *data = sigma_delta->rx_buf;
	unsigned int reg_size;
	unsigned int data_reg;

	reg_size = indio_dev->channels[0].scan_type.realbits +
			indio_dev->channels[0].scan_type.shift;
	reg_size = DIV_ROUND_UP(reg_size, 8);

	if (sigma_delta->info->data_reg != 0)
		data_reg = sigma_delta->info->data_reg;
	else
		data_reg = AD_SD_REG_DATA;

	switch (reg_size) {
	case 4:
	case 2:
	case 1:
		ad_sd_read_reg_raw(sigma_delta, data_reg, reg_size, &data[0]);
		break;
	case 3:
		/* We store 24 bit samples in a 32 bit word. Keep the upper
		 * byte set to zero. */
		ad_sd_read_reg_raw(sigma_delta, data_reg, reg_size, &data[1]);
		break;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data, pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);
	sigma_delta->irq_dis = false;
	adsdi_enable(sigma_delta);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops ad_sd_buffer_setup_ops = {
	.postenable = &ad_sd_buffer_postenable,
	.postdisable = &ad_sd_buffer_postdisable,
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

static irqreturn_t ad_sd_data_rdy_trig_poll(int irq, void *private)
{
	struct ad_sigma_delta_interrupt *intr = private;
	struct ad_sigma_delta *sigma_delta = intr->active;

	WARN_ON(!sigma_delta);
	if (!sigma_delta)
		return IRQ_NONE;

	pr_debug("ad_sigma_delta: interrupt %d fired for %p\n",
		intr->irq, sigma_delta);
	complete(&sigma_delta->completion);
	adsdi_disable(sigma_delta, 1);
	sigma_delta->irq_dis = true;
	iio_trigger_poll(sigma_delta->trig);

	return IRQ_HANDLED;
}

/**
 * ad_sd_validate_trigger() - validate_trigger callback for ad_sigma_delta devices
 * @indio_dev: The IIO device
 * @trig: The new trigger
 *
 * Returns: 0 if the 'trig' matches the trigger registered by the ad_sigma_delta
 * device, -EINVAL otherwise.
 */
int ad_sd_validate_trigger(struct iio_dev *indio_dev, struct iio_trigger *trig)
{
	struct ad_sigma_delta *sigma_delta = iio_device_get_drvdata(indio_dev);

	if (sigma_delta->trig != trig)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(ad_sd_validate_trigger);

static int devm_ad_sd_probe_trigger(struct device *dev, struct iio_dev *indio_dev)
{
	struct ad_sigma_delta *sigma_delta = iio_device_get_drvdata(indio_dev);
	int ret;

	if (dev != &sigma_delta->spi->dev) {
		dev_err(dev, "Trigger parent should be '%s', got '%s'\n",
			dev_name(dev), dev_name(&sigma_delta->spi->dev));
		return -EFAULT;
	}

	sigma_delta->trig = devm_iio_trigger_alloc(dev, "%s-dev%d", indio_dev->name,
						   iio_device_id(indio_dev));
	if (sigma_delta->trig == NULL)
		return -ENOMEM;

	init_completion(&sigma_delta->completion);

	sigma_delta->irq_dis = true;
	ret = devm_adsdi_get(dev,
			&sigma_delta->interrupt,
			sigma_delta->spi->irq,
			sigma_delta->spi->master,
			sigma_delta->info->irq_flags);
	if (ret)
		return ret;

	iio_trigger_set_drvdata(sigma_delta->trig, sigma_delta);

	ret = devm_iio_trigger_register(dev, sigma_delta->trig);
	if (ret)
		return ret;

	/* select default trigger */
	indio_dev->trig = iio_trigger_get(sigma_delta->trig);

	return 0;
}

/**
 * devm_ad_sd_setup_buffer_and_trigger() - Device-managed buffer & trigger setup
 * @dev: Device object to which to bind the life-time of the resources attached
 * @indio_dev: The IIO device
 */
int devm_ad_sd_setup_buffer_and_trigger(struct device *dev, struct iio_dev *indio_dev)
{
	int ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      &iio_pollfunc_store_time,
					      &ad_sd_trigger_handler,
					      &ad_sd_buffer_setup_ops);
	if (ret)
		return ret;

	return devm_ad_sd_probe_trigger(dev, indio_dev);
}
EXPORT_SYMBOL_GPL(devm_ad_sd_setup_buffer_and_trigger);

/**
 * ad_sd_init() - Initializes a ad_sigma_delta struct
 * @sigma_delta: The ad_sigma_delta device
 * @indio_dev: The IIO device which the Sigma Delta device is used for
 * @spi: The SPI device for the ad_sigma_delta device
 * @info: Device specific callbacks and options
 *
 * This function needs to be called before any other operations are performed on
 * the ad_sigma_delta struct.
 */
int ad_sd_init(struct ad_sigma_delta *sigma_delta, struct iio_dev *indio_dev,
	struct spi_device *spi, const struct ad_sigma_delta_info *info)
{
	sigma_delta->spi = spi;
	sigma_delta->info = info;
	iio_device_set_drvdata(indio_dev, sigma_delta);

	return 0;
}
EXPORT_SYMBOL_GPL(ad_sd_init);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices Sigma-Delta ADCs");
MODULE_LICENSE("GPL v2");
