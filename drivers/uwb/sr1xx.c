// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI driver for UWB SR1xx
 * Copyright (C) 2018-2022 NXP.
 *
 * Author: Manjunatha Venkatesh <manjunatha.venkatesh@nxp.com>
 */
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#define SR1XX_MAGIC 0xEA
#define SR1XX_SET_PWR _IOW(SR1XX_MAGIC, 0x01, long)
#define SR1XX_SET_DBG _IOW(SR1XX_MAGIC, 0x02, long)
#define SR1XX_SET_POLL _IOW(SR1XX_MAGIC, 0x03, long)
#define SR1XX_SET_FWD _IOW(SR1XX_MAGIC, 0x04, long)
#define SR1XX_ESE_RESET _IOW(SR1XX_MAGIC, 0x06, long)

#define UCI_HEADER_LEN 4
#define HBCI_HEADER_LEN 4
#define UCI_PAYLOAD_LEN_OFFSET 3

#define UCI_EXT_PAYLOAD_LEN_IND_OFFSET 1
#define UCI_EXT_PAYLOAD_LEN_IND_OFFSET_MASK 0x80
#define UCI_EXT_PAYLOAD_LEN_OFFSET 2

#define SR1XX_TXBUF_SIZE 4200
#define SR1XX_RXBUF_SIZE 4200
#define SR1XX_MAX_TX_BUF_SIZE 4200

#define MAX_RETRY_COUNT_FOR_IRQ_CHECK 100
#define MAX_RETRY_COUNT_FOR_HANDSHAKE 1000

/* Macro to define SPI clock frequency */
#define SR1XX_SPI_CLOCK 16000000L
#define WAKEUP_SRC_TIMEOUT (2000)

/* Maximum UCI packet size supported from the driver */
#define MAX_UCI_PKT_SIZE 4200

struct sr1xx_spi_platform_data {
	unsigned int irq_gpio; /* SR1XX will interrupt host for any ntf */
	unsigned int ce_gpio; /* SW reset gpio */
	unsigned int spi_handshake_gpio; /* Host ready to read data */
};

/* Device specific macro and structure */
struct sr1xx_dev {
	wait_queue_head_t read_wq; /* Wait queue for read interrupt */
	struct spi_device *spi; /* Spi device structure */
	struct miscdevice sr1xx_device; /* Char device as misc driver */
	unsigned int ce_gpio; /* SW reset gpio */
	unsigned int irq_gpio; /* SR1XX will interrupt host for any ntf */
	unsigned int spi_handshake_gpio; /* Host ready to read data */
	bool irq_enabled; /* Flag to indicate disable/enable irq sequence */
	bool irq_received; /* Flag to indicate that irq is received */
	spinlock_t irq_enabled_lock; /* Spin lock for read irq */
	unsigned char *tx_buffer; /* Transmit buffer */
	unsigned char *rx_buffer; /* Receive buffer */
	unsigned int write_count; /* Holds nubers of byte written */
	unsigned int read_count; /* Hold nubers of byte read */
	struct mutex
		sr1xx_access_lock; /* Lock used to synchronize read and write */
	size_t total_bytes_to_read; /* Total bytes read from the device */
	bool is_extended_len_bit_set; /* Variable to check ext payload Len */
	bool read_abort_requested; /* Used to indicate read abort request */
	bool is_fw_dwnld_enabled; /* Used to indicate fw download mode */
	int mode; /* Indicate write or read mode */
	long timeout_in_ms; /* Wait event interrupt timeout in ms */
};

/* Power enable/disable and read abort ioctl arguments */
enum { PWR_DISABLE = 0, PWR_ENABLE, ABORT_READ_PENDING };

enum spi_status_codes {
	TRANSCEIVE_SUCCESS,
	TRANSCEIVE_FAIL,
	IRQ_WAIT_REQUEST,
	IRQ_WAIT_TIMEOUT
};

/* Spi write/read operation mode */
enum spi_operation_modes { SR1XX_WRITE_MODE, SR1XX_READ_MODE };

static int sr1xx_dev_open(struct inode *inode, struct file *filp)
{
	struct sr1xx_dev *sr1xx_dev = container_of(
		filp->private_data, struct sr1xx_dev, sr1xx_device);

	filp->private_data = sr1xx_dev;
	return 0;
}

static void sr1xx_disable_irq(struct sr1xx_dev *sr1xx_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&sr1xx_dev->irq_enabled_lock, flags);
	if ((sr1xx_dev->irq_enabled)) {
		disable_irq_nosync(sr1xx_dev->spi->irq);
		sr1xx_dev->irq_received = true;
		sr1xx_dev->irq_enabled = false;
	}
	spin_unlock_irqrestore(&sr1xx_dev->irq_enabled_lock, flags);
}

static void sr1xx_enable_irq(struct sr1xx_dev *sr1xx_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&sr1xx_dev->irq_enabled_lock, flags);
	if (!sr1xx_dev->irq_enabled) {
		enable_irq(sr1xx_dev->spi->irq);
		sr1xx_dev->irq_enabled = true;
		sr1xx_dev->irq_received = false;
	}
	spin_unlock_irqrestore(&sr1xx_dev->irq_enabled_lock, flags);
}

static irqreturn_t sr1xx_dev_irq_handler(int irq, void *dev_id)
{
	struct sr1xx_dev *sr1xx_dev = dev_id;

	sr1xx_disable_irq(sr1xx_dev);
	/* Wake up waiting readers */
	wake_up(&sr1xx_dev->read_wq);
	if (device_may_wakeup(&sr1xx_dev->spi->dev))
		pm_wakeup_event(&sr1xx_dev->spi->dev, WAKEUP_SRC_TIMEOUT);
	return IRQ_HANDLED;
}

static long sr1xx_dev_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	int ret = 0;
	struct sr1xx_dev *sr1xx_dev = NULL;

	sr1xx_dev = filp->private_data;
	switch (cmd) {
	case SR1XX_SET_PWR:
		if (arg == PWR_ENABLE) {
			gpio_set_value(sr1xx_dev->ce_gpio, 1);
			usleep_range(10000, 12000);
		} else if (arg == PWR_DISABLE) {
			gpio_set_value(sr1xx_dev->ce_gpio, 0);
			sr1xx_disable_irq(sr1xx_dev);
			usleep_range(10000, 12000);
		} else if (arg == ABORT_READ_PENDING) {
			sr1xx_dev->read_abort_requested = true;
			sr1xx_disable_irq(sr1xx_dev);
			/* Wake up waiting readers */
			wake_up(&sr1xx_dev->read_wq);
		}
		break;
	case SR1XX_SET_FWD:
		if (arg == 1) {
			sr1xx_dev->is_fw_dwnld_enabled = true;
			sr1xx_dev->read_abort_requested = false;
		} else if (arg == 0) {
			sr1xx_dev->is_fw_dwnld_enabled = false;
		}
		break;
	default:
		dev_err(&sr1xx_dev->spi->dev, " Error case");
		ret = -EINVAL;
	}
	return ret;
}

/**
 * sr1xx_wait_for_irq_gpio_low
 *
 * Function to wait till irq gpio goes low state
 *
 */
void sr1xx_wait_for_irq_gpio_low(struct sr1xx_dev *sr1xx_dev)
{
	int retry_count = 0;

	do {
		udelay(10);
		retry_count++;
		if (retry_count == MAX_RETRY_COUNT_FOR_HANDSHAKE) {
			dev_info(&sr1xx_dev->spi->dev,
				 "Slave not released the IRQ even after 10ms");
			break;
		}
	} while (gpio_get_value(sr1xx_dev->irq_gpio));
}

/**
 * sr1xx_dev_transceive
 * @op_mode indicates write/read operation
 *
 * Write and Read logic implemented under same api with
 * mutex lock protection so write and read synchronized
 *
 * During Uwb ranging sequence(read) need to block write sequence
 * in order to avoid some race condition scenarios.
 *
 * Returns     : Number of bytes write/read if read is success else (-1)
 *               otherwise indicate each error code
 */
static int sr1xx_dev_transceive(struct sr1xx_dev *sr1xx_dev, int op_mode,
				int count)
{
	int ret, retry_count;

	mutex_lock(&sr1xx_dev->sr1xx_access_lock);
	sr1xx_dev->mode = op_mode;
	sr1xx_dev->total_bytes_to_read = 0;
	sr1xx_dev->is_extended_len_bit_set = 0;
	ret = -1;
	retry_count = 0;

	switch (sr1xx_dev->mode) {
	case SR1XX_WRITE_MODE: {
		sr1xx_dev->write_count = 0;
		/* UCI Header write */
		ret = spi_write(sr1xx_dev->spi, sr1xx_dev->tx_buffer,
				UCI_HEADER_LEN);
		if (ret < 0) {
			ret = -EIO;
			dev_err(&sr1xx_dev->spi->dev,
				"spi_write header : Failed.\n");
			goto transceive_end;
		} else {
			count -= UCI_HEADER_LEN;
		}
		if (count > 0) {
			/* In between header write and payload write slave need some time */
			usleep_range(30, 50);
			/* UCI Payload write */
			ret = spi_write(sr1xx_dev->spi,
					sr1xx_dev->tx_buffer + UCI_HEADER_LEN,
					count);
			if (ret < 0) {
				ret = -EIO;
				dev_err(&sr1xx_dev->spi->dev,
					"spi_write payload : Failed.\n");
				goto transceive_end;
			}
		}
		sr1xx_dev->write_count = count + UCI_HEADER_LEN;
		ret = TRANSCEIVE_SUCCESS;
	} break;
	case SR1XX_READ_MODE: {
		if (!gpio_get_value(sr1xx_dev->irq_gpio)) {
			dev_err(&sr1xx_dev->spi->dev,
				"IRQ might have gone low due to write ");
			ret = IRQ_WAIT_REQUEST;
			goto transceive_end;
		}
		retry_count = 0;
		gpio_set_value(sr1xx_dev->spi_handshake_gpio, 1);
		while (gpio_get_value(sr1xx_dev->irq_gpio)) {
			if (retry_count == MAX_RETRY_COUNT_FOR_IRQ_CHECK)
				break;
			udelay(10);
			retry_count++;
		}
		sr1xx_enable_irq(sr1xx_dev);
		sr1xx_dev->read_count = 0;
		retry_count = 0;
		/* Wait for inetrrupt upto 500ms */
		ret = wait_event_interruptible_timeout(
			sr1xx_dev->read_wq, sr1xx_dev->irq_received,
			sr1xx_dev->timeout_in_ms);
		if (ret == 0) {
			dev_err(&sr1xx_dev->spi->dev,
				"wait_event_interruptible timeout() : Failed.\n");
			ret = IRQ_WAIT_TIMEOUT;
			goto transceive_end;
		}
		if (!gpio_get_value(sr1xx_dev->irq_gpio)) {
			dev_err(&sr1xx_dev->spi->dev, "Second IRQ is Low");
			ret = -1;
			goto transceive_end;
		}
		ret = spi_read(sr1xx_dev->spi, (void *)sr1xx_dev->rx_buffer,
			       UCI_HEADER_LEN);
		if (ret < 0) {
			dev_err(&sr1xx_dev->spi->dev,
				"sr1xx_dev_read: spi read error %d\n ", ret);
			goto transceive_end;
		}
		sr1xx_dev->is_extended_len_bit_set =
			(sr1xx_dev->rx_buffer[UCI_EXT_PAYLOAD_LEN_IND_OFFSET] &
			 UCI_EXT_PAYLOAD_LEN_IND_OFFSET_MASK);
		sr1xx_dev->total_bytes_to_read =
			sr1xx_dev->rx_buffer[UCI_PAYLOAD_LEN_OFFSET];
		if (sr1xx_dev->is_extended_len_bit_set) {
			sr1xx_dev->total_bytes_to_read =
				((sr1xx_dev->total_bytes_to_read << 8) |
				 sr1xx_dev
					 ->rx_buffer[UCI_EXT_PAYLOAD_LEN_OFFSET]);
		}
		if (sr1xx_dev->total_bytes_to_read >
		    (MAX_UCI_PKT_SIZE - UCI_HEADER_LEN)) {
			dev_err(&sr1xx_dev->spi->dev,
				"Length %d  exceeds the max limit %d....",
				(int)sr1xx_dev->total_bytes_to_read,
				(int)MAX_UCI_PKT_SIZE);
			ret = -1;
			goto transceive_end;
		}
		if (sr1xx_dev->total_bytes_to_read > 0) {
			ret = spi_read(
				sr1xx_dev->spi,
				(void *)(sr1xx_dev->rx_buffer + UCI_HEADER_LEN),
				sr1xx_dev->total_bytes_to_read);
			if (ret < 0) {
				dev_err(&sr1xx_dev->spi->dev,
					"sr1xx_dev_read: spi read error.. %d\n ",
					ret);
				goto transceive_end;
			}
		}
		sr1xx_dev->read_count =
			(unsigned int)(sr1xx_dev->total_bytes_to_read +
				       UCI_HEADER_LEN);
		sr1xx_wait_for_irq_gpio_low(sr1xx_dev);
		ret = TRANSCEIVE_SUCCESS;
		gpio_set_value(sr1xx_dev->spi_handshake_gpio, 0);
	} break;
	default:
		dev_err(&sr1xx_dev->spi->dev, "invalid operation .....");
		break;
	}
transceive_end:
	if (sr1xx_dev->mode == SR1XX_READ_MODE)
		gpio_set_value(sr1xx_dev->spi_handshake_gpio, 0);

	mutex_unlock(&sr1xx_dev->sr1xx_access_lock);
	return ret;
}

/**
 * sr1xx_hbci_write
 *
 * Used to write hbci(SR1xx BootROM Command Interface) packets
 * during firmware download sequence.
 *
 * Returns: TRANSCEIVE_SUCCESS on success or -1 on fail
 */
static int sr1xx_hbci_write(struct sr1xx_dev *sr1xx_dev, int count)
{
	int ret;

	sr1xx_dev->write_count = 0;
	/* HBCI write */
	ret = spi_write(sr1xx_dev->spi, sr1xx_dev->tx_buffer, count);
	if (ret < 0) {
		ret = -EIO;
		dev_err(&sr1xx_dev->spi->dev,
			"spi_write fw download : Failed.\n");
		goto hbci_write_fail;
	}
	sr1xx_dev->write_count = count;
	sr1xx_enable_irq(sr1xx_dev);
	ret = TRANSCEIVE_SUCCESS;
	return ret;
hbci_write_fail:
	dev_err(&sr1xx_dev->spi->dev, "%s failed...%d", __func__, ret);
	return ret;
}

static ssize_t sr1xx_dev_write(struct file *filp, const char *buf, size_t count,
			       loff_t *offset)
{
	int ret;
	struct sr1xx_dev *sr1xx_dev;

	sr1xx_dev = filp->private_data;
	if (count > SR1XX_MAX_TX_BUF_SIZE || count > SR1XX_TXBUF_SIZE) {
		dev_err(&sr1xx_dev->spi->dev, "%s : Write Size Exceeds\n",
			__func__);
		ret = -ENOBUFS;
		goto write_end;
	}
	if (copy_from_user(sr1xx_dev->tx_buffer, buf, count)) {
		dev_err(&sr1xx_dev->spi->dev,
			"%s : failed to copy from user space\n", __func__);
		return -EFAULT;
	}
	if (sr1xx_dev->is_fw_dwnld_enabled)
		ret = sr1xx_hbci_write(sr1xx_dev, count);
	else
		ret = sr1xx_dev_transceive(sr1xx_dev, SR1XX_WRITE_MODE, count);
	if (ret == TRANSCEIVE_SUCCESS)
		ret = sr1xx_dev->write_count;
	else
		dev_err(&sr1xx_dev->spi->dev, "write failed......");
write_end:
	return ret;
}

/**
 * sr1xx_hbci_read
 *
 * Function used to read data from sr1xx on SPI line
 * as part of firmware download sequence.
 *
 * Returns: Number of bytes read if read is success else (-1)
 *               otherwise indicate each error code
 */
static ssize_t sr1xx_hbci_read(struct sr1xx_dev *sr1xx_dev, char *buf,
			       size_t count)
{
	int ret = -EIO;

	if (count > SR1XX_RXBUF_SIZE) {
		dev_err(&sr1xx_dev->spi->dev, "count(%zu) out of range(0-%d)\n",
			count, SR1XX_RXBUF_SIZE);
		ret = -EINVAL;
		goto hbci_fail;
	}
	/* Wait for inetrrupt upto 500ms */
	ret = wait_event_interruptible_timeout(sr1xx_dev->read_wq,
					       sr1xx_dev->irq_received,
					       sr1xx_dev->timeout_in_ms);
	if (ret == 0) {
		dev_err(&sr1xx_dev->spi->dev,
			"hbci wait_event_interruptible timeout() : Failed.\n");
		ret = -1;
		goto hbci_fail;
	}
	if (sr1xx_dev->read_abort_requested) {
		sr1xx_dev->read_abort_requested = false;
		dev_err(&sr1xx_dev->spi->dev, "HBCI Abort Read pending......");
		return ret;
	}
	if (!gpio_get_value(sr1xx_dev->irq_gpio)) {
		dev_err(&sr1xx_dev->spi->dev,
			"IRQ is low during firmware download");
		goto hbci_fail;
	}
	ret = spi_read(sr1xx_dev->spi, (void *)sr1xx_dev->rx_buffer, count);
	if (ret < 0) {
		dev_err(&sr1xx_dev->spi->dev,
			"sr1xx_dev_read: spi read error %d\n ", ret);
		goto hbci_fail;
	}
	ret = count;
	if (copy_to_user(buf, sr1xx_dev->rx_buffer, count)) {
		dev_err(&sr1xx_dev->spi->dev,
			"sr1xx_dev_read: copy to user failed\n");
		ret = -EFAULT;
	}
	return ret;
hbci_fail:
	dev_err(&sr1xx_dev->spi->dev, "Error sr1xx_fw_download ret %d Exit\n",
		ret);
	return ret;
}

static ssize_t sr1xx_dev_read(struct file *filp, char *buf, size_t count,
			      loff_t *offset)
{
	struct sr1xx_dev *sr1xx_dev = filp->private_data;
	int ret = -EIO;

	/* 500ms timeout in jiffies */
	sr1xx_dev->timeout_in_ms = ((500 * HZ) / 1000);
	memset(sr1xx_dev->rx_buffer, 0x00, SR1XX_RXBUF_SIZE);
	if (!gpio_get_value(sr1xx_dev->irq_gpio)) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto read_end;
		}
	}
	/* HBCI packet read */
	if (sr1xx_dev->is_fw_dwnld_enabled)
		return sr1xx_hbci_read(sr1xx_dev, buf, count);
	/* UCI packet read */
first_irq_wait:
	sr1xx_enable_irq(sr1xx_dev);
	if (!sr1xx_dev->read_abort_requested) {
		ret = wait_event_interruptible(sr1xx_dev->read_wq,
					       sr1xx_dev->irq_received);
		if (ret) {
			dev_err(&sr1xx_dev->spi->dev,
				"wait_event_interruptible() : Failed.\n");
			goto read_end;
		}
	}
	if (sr1xx_dev->read_abort_requested) {
		sr1xx_dev->read_abort_requested = false;
		dev_err(&sr1xx_dev->spi->dev, "Abort Read pending......");
		return ret;
	}
	ret = sr1xx_dev_transceive(sr1xx_dev, SR1XX_READ_MODE, count);
	if (ret == TRANSCEIVE_SUCCESS) {
		if (copy_to_user(buf, sr1xx_dev->rx_buffer,
				 sr1xx_dev->read_count)) {
			dev_err(&sr1xx_dev->spi->dev,
				"%s: copy to user failed\n", __func__);
			ret = -EFAULT;
			goto read_end;
		}
		ret = sr1xx_dev->read_count;
	} else if (ret == IRQ_WAIT_REQUEST) {
		dev_err(&sr1xx_dev->spi->dev,
			"Irg is low due to write hence irq is requested again...");
		goto first_irq_wait;
	} else if (ret == IRQ_WAIT_TIMEOUT) {
		dev_err(&sr1xx_dev->spi->dev,
			"Second irq is not received..Time out...");
		ret = -1;
	} else {
		dev_err(&sr1xx_dev->spi->dev, "spi read failed...%d", ret);
		ret = -1;
	}
read_end:
	return ret;
}

static int sr1xx_hw_setup(struct device *dev,
			  struct sr1xx_spi_platform_data *platform_data)
{
	int ret;

	ret = gpio_request(platform_data->irq_gpio, "sr1xx irq");
	if (ret < 0) {
		dev_err(dev, "gpio request failed gpio = 0x%x\n",
			platform_data->irq_gpio);
		goto fail;
	}

	ret = gpio_direction_input(platform_data->irq_gpio);
	if (ret < 0) {
		dev_err(dev, "gpio request failed gpio = 0x%x\n",
			platform_data->irq_gpio);
		goto fail_irq;
	}

	ret = gpio_request(platform_data->ce_gpio, "sr1xx ce");
	if (ret < 0) {
		dev_err(dev, "gpio request failed gpio = 0x%x\n",
			platform_data->ce_gpio);
		goto fail_gpio;
	}

	ret = gpio_direction_output(platform_data->ce_gpio, 0);
	if (ret < 0) {
		dev_err(dev, "sr1xx - Failed setting ce gpio - %d\n",
			platform_data->ce_gpio);
		goto fail_ce_gpio;
	}

	ret = gpio_request(platform_data->spi_handshake_gpio, "sr1xx ri");
	if (ret < 0) {
		dev_err(dev, "sr1xx - Failed requesting ri gpio - %d\n",
			platform_data->spi_handshake_gpio);
		goto fail_gpio;
	}

	ret = gpio_direction_output(platform_data->spi_handshake_gpio, 0);
	if (ret < 0) {
		dev_err(dev,
			"sr1xx - Failed setting spi handeshake gpio - %d\n",
			platform_data->spi_handshake_gpio);
		goto fail_handshake_gpio;
	}

	ret = 0;
	return ret;

fail_gpio:
fail_handshake_gpio:
	gpio_free(platform_data->spi_handshake_gpio);
fail_ce_gpio:
	gpio_free(platform_data->ce_gpio);
fail_irq:
	gpio_free(platform_data->irq_gpio);
fail:
	dev_err(dev, "%s failed\n", __func__);
	return ret;
}

static inline void sr1xx_set_data(struct spi_device *spi, void *data)
{
	dev_set_drvdata(&spi->dev, data);
}

static inline void *sr1xx_get_data(const struct spi_device *spi)
{
	return dev_get_drvdata(&spi->dev);
}

/* Possible fops on the sr1xx device */
static const struct file_operations sr1xx_dev_fops = {
	.owner = THIS_MODULE,
	.read = sr1xx_dev_read,
	.write = sr1xx_dev_write,
	.open = sr1xx_dev_open,
	.unlocked_ioctl = sr1xx_dev_ioctl,
};

static int sr1xx_parse_dt(struct device *dev,
			  struct sr1xx_spi_platform_data *pdata)
{
	struct device_node *np = dev->of_node;

	pdata->irq_gpio = of_get_named_gpio(np, "nxp,sr1xx-irq", 0);
	if (!gpio_is_valid(pdata->irq_gpio))
		return -EINVAL;
	pdata->ce_gpio = of_get_named_gpio(np, "nxp,sr1xx-ce", 0);
	if (!gpio_is_valid(pdata->ce_gpio))
		return -EINVAL;
	pdata->spi_handshake_gpio = of_get_named_gpio(np, "nxp,sr1xx-ri", 0);
	if (!gpio_is_valid(pdata->spi_handshake_gpio))
		return -EINVAL;
	dev_info(
		dev,
		"sr1xx : irq_gpio = %d, ce_gpio = %d, spi_handshake_gpio = %d\n",
		pdata->irq_gpio, pdata->ce_gpio, pdata->spi_handshake_gpio);
	return 0;
}

/**
 * sr1xx_gpio_cleanup
 *
 * Release requested gpios
 *
 */
static void sr1xx_gpio_cleanup(struct sr1xx_spi_platform_data *pdata)
{
	if (gpio_is_valid(pdata->spi_handshake_gpio))
		gpio_free(pdata->spi_handshake_gpio);
	if (gpio_is_valid(pdata->ce_gpio))
		gpio_free(pdata->ce_gpio);
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);
}

static int sr1xx_probe(struct spi_device *spi)
{
	int ret;
	struct sr1xx_spi_platform_data *platform_data = NULL;
	struct sr1xx_spi_platform_data platform_data1;
	struct sr1xx_dev *sr1xx_dev = NULL;
	unsigned int irq_flags;

	dev_info(&spi->dev, "%s chip select : %d , bus number = %d\n", __func__,
		 spi->chip_select, spi->master->bus_num);

	ret = sr1xx_parse_dt(&spi->dev, &platform_data1);
	if (ret) {
		dev_err(&spi->dev, "%s - Failed to parse DT\n", __func__);
		goto err_exit;
	}
	platform_data = &platform_data1;

	sr1xx_dev = kzalloc(sizeof(*sr1xx_dev), GFP_KERNEL);
	if (sr1xx_dev == NULL) {
		ret = -ENOMEM;
		goto err_exit;
	}
	ret = sr1xx_hw_setup(&spi->dev, platform_data);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to sr1xx_enable_SR1XX_IRQ_ENABLE\n");
		goto err_setup;
	}

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	spi->max_speed_hz = SR1XX_SPI_CLOCK;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to do spi_setup()\n");
		goto err_setup;
	}

	sr1xx_dev->spi = spi;
	sr1xx_dev->sr1xx_device.minor = MISC_DYNAMIC_MINOR;
	sr1xx_dev->sr1xx_device.name = "sr1xx";
	sr1xx_dev->sr1xx_device.fops = &sr1xx_dev_fops;
	sr1xx_dev->sr1xx_device.parent = &spi->dev;
	sr1xx_dev->irq_gpio = platform_data->irq_gpio;
	sr1xx_dev->ce_gpio = platform_data->ce_gpio;
	sr1xx_dev->spi_handshake_gpio = platform_data->spi_handshake_gpio;

	dev_set_drvdata(&spi->dev, sr1xx_dev);

	/* init mutex and queues */
	init_waitqueue_head(&sr1xx_dev->read_wq);
	mutex_init(&sr1xx_dev->sr1xx_access_lock);

	spin_lock_init(&sr1xx_dev->irq_enabled_lock);

	ret = misc_register(&sr1xx_dev->sr1xx_device);
	if (ret < 0) {
		dev_err(&spi->dev, "misc_register failed! %d\n", ret);
		goto err_setup;
	}

	sr1xx_dev->tx_buffer = kzalloc(SR1XX_TXBUF_SIZE, GFP_KERNEL);
	sr1xx_dev->rx_buffer = kzalloc(SR1XX_RXBUF_SIZE, GFP_KERNEL);
	if (sr1xx_dev->tx_buffer == NULL) {
		ret = -ENOMEM;
		goto exit_free_dev;
	}
	if (sr1xx_dev->rx_buffer == NULL) {
		ret = -ENOMEM;
		goto exit_free_dev;
	}

	sr1xx_dev->spi->irq = gpio_to_irq(platform_data->irq_gpio);

	if (sr1xx_dev->spi->irq < 0) {
		dev_err(&spi->dev, "gpio_to_irq request failed gpio = 0x%x\n",
			platform_data->irq_gpio);
		goto err_irq_request;
	}
	/* request irq. The irq is set whenever the chip has data available
	 * for reading. It is cleared when all data has been read.
	 */
	irq_flags = IRQ_TYPE_LEVEL_HIGH;
	sr1xx_dev->irq_enabled = true;
	sr1xx_dev->irq_received = false;

	ret = request_irq(sr1xx_dev->spi->irq, sr1xx_dev_irq_handler, irq_flags,
			  sr1xx_dev->sr1xx_device.name, sr1xx_dev);
	if (ret) {
		dev_err(&spi->dev, "request_irq failed\n");
		goto err_irq_request;
	}
	sr1xx_disable_irq(sr1xx_dev);

	return ret;
err_irq_request:
exit_free_dev:
	if (sr1xx_dev != NULL) {
		kfree(sr1xx_dev->tx_buffer);
		kfree(sr1xx_dev->rx_buffer);
		misc_deregister(&sr1xx_dev->sr1xx_device);
	}
err_setup:
	if (sr1xx_dev != NULL)
		mutex_destroy(&sr1xx_dev->sr1xx_access_lock);
err_exit:
	sr1xx_gpio_cleanup(platform_data);
	kfree(sr1xx_dev);
	dev_err(&spi->dev, "ERROR: Exit : %s ret %d\n", __func__, ret);
	return ret;
}

static int sr1xx_remove(struct spi_device *spi)
{
	struct sr1xx_dev *sr1xx_dev = sr1xx_get_data(spi);

	gpio_free(sr1xx_dev->ce_gpio);
	mutex_destroy(&sr1xx_dev->sr1xx_access_lock);
	free_irq(sr1xx_dev->spi->irq, sr1xx_dev);
	gpio_free(sr1xx_dev->irq_gpio);
	gpio_free(sr1xx_dev->spi_handshake_gpio);
	misc_deregister(&sr1xx_dev->sr1xx_device);
	if (sr1xx_dev != NULL) {
		kfree(sr1xx_dev->tx_buffer);
		kfree(sr1xx_dev->rx_buffer);
		kfree(sr1xx_dev);
	}
	return 0;
}

/**
 * sr1xx_dev_suspend
 *
 * Executed before putting the system into a sleep state
 *
 */
int sr1xx_dev_suspend(struct device *dev)
{
	struct sr1xx_dev *sr1xx_dev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(sr1xx_dev->spi->irq);
	return 0;
}

/**
 * sr1xx_dev_resume
 *
 * Executed after waking the system up from a sleep state
 *
 */

int sr1xx_dev_resume(struct device *dev)
{
	struct sr1xx_dev *sr1xx_dev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(sr1xx_dev->spi->irq);

	return 0;
}

static const struct of_device_id sr1xx_dt_match[] = {
	{
		.compatible = "nxp,sr1xx",
	},
	{}
};

static const struct dev_pm_ops sr1xx_dev_pm_ops = { SET_SYSTEM_SLEEP_PM_OPS(
	sr1xx_dev_suspend, sr1xx_dev_resume) };

static struct spi_driver sr1xx_driver = {
	.driver = {
		   .name = "sr1xx",
		   .pm = &sr1xx_dev_pm_ops,
		   .bus = &spi_bus_type,
		   .owner = THIS_MODULE,
		   .of_match_table = sr1xx_dt_match,
		    },
	.probe = sr1xx_probe,
	.remove = (sr1xx_remove),
};

static int __init sr1xx_dev_init(void)
{
	return spi_register_driver(&sr1xx_driver);
}

module_init(sr1xx_dev_init);

static void __exit sr1xx_dev_exit(void)
{
	spi_unregister_driver(&sr1xx_driver);
}

module_exit(sr1xx_dev_exit);

MODULE_AUTHOR("Manjunatha Venkatesh <manjunatha.venkatesh@nxp.com>");
MODULE_DESCRIPTION("NXP SR1XX SPI driver");
MODULE_LICENSE("GPL v2");
