// SPDX-License-Identifier: GPL-2.0-only
/*
 * Advantech AHC1EC0 Embedded Controller Core
 *
 * Copyright 2021 Advantech IIoT Group
 */

#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/platform_data/ahc1ec0.h>
#include <linux/module.h>

/* Wait IBF (Input Buffer Full) clear */
static int ec_wait_write(void)
{
	int i;

	for (i = 0; i < EC_MAX_TIMEOUT_COUNT; i++) {
		if ((inb(EC_COMMAND_PORT) & EC_COMMAND_BIT_IBF) == 0)
			return 0;

		udelay(EC_RETRY_UDELAY);
	}

	return -ETIMEDOUT;
}

/* Wait OBF (Output Buffer Full) data ready */
static int ec_wait_read(void)
{
	int i;

	for (i = 0; i < EC_MAX_TIMEOUT_COUNT; i++) {
		if ((inb(EC_COMMAND_PORT) & EC_COMMAND_BIT_OBF) != 0)
			return 0;

		udelay(EC_RETRY_UDELAY);
	}

	return -ETIMEDOUT;
}

/* Read data from EC HW RAM, the process is the following:
 * Step 0. Wait IBF clear to send command
 * Step 1. Send read command to EC command port
 * Step 2. Wait IBF clear that means command is got by EC
 * Step 3. Send read address to EC data port
 * Step 4. Wait OBF data ready
 * Step 5. Get data from EC data port
 */
int ahc1ec_read_hw_ram(struct adv_ec_ddata *ddata,
		       unsigned char addr, unsigned char *data)
{
	int ret;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_HW_RAM_READ, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(addr, EC_STATUS_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;
	*data = inb(EC_STATUS_PORT);

	mutex_unlock(&ddata->lock);

	return ret;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_read_hw_ram);

/* Write data to EC HW RAM
 * Step 0. Wait IBF clear to send command
 * Step 1. Send write command to EC command port
 * Step 2. Wait IBF clear that means command is got by EC
 * Step 3. Send write address to EC data port
 * Step 4. Wait IBF clear that means command is got by EC
 * Step 5. Send data to EC data port
 */
int ahc1ec_write_hw_ram(struct adv_ec_ddata *ddata,
			unsigned char addr, unsigned char data)
{
	int ret;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_HW_RAM_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(addr, EC_STATUS_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(data, EC_STATUS_PORT);

	mutex_unlock(&ddata->lock);

	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_write_hw_ram);

/* Get dynamic control table */
int adv_get_dynamic_tab(struct adv_ec_ddata *ddata)
{
	int i, ret;
	unsigned char pin_tmp, device_id;

	mutex_lock(&ddata->lock);

	for (i = 0; i < EC_MAX_TBL_NUM; i++) {
		ddata->dym_tbl[i].device_id = EC_TBL_NOTFOUND;
		ddata->dym_tbl[i].hw_pin_num = EC_TBL_NOTFOUND;
	}

	for (i = 0; i < EC_MAX_TBL_NUM; i++) {
		ret = ec_wait_write();
		if (ret)
			goto error;
		outb(EC_TBL_WRITE_ITEM, EC_COMMAND_PORT);

		ret = ec_wait_write();
		if (ret)
			goto error;
		outb(i, EC_STATUS_PORT);

		ret = ec_wait_read();
		if (ret)
			goto error;

		/*
		 *  If item is defined, EC will return item number.
		 *  If table item is not defined, EC will return EC_TBL_NOTFOUND(0xFF).
		 */
		pin_tmp = inb(EC_STATUS_PORT);
		if (pin_tmp == EC_TBL_NOTFOUND)
			goto pass;

		ret = ec_wait_write();
		if (ret)
			goto error;
		outb(EC_TBL_GET_PIN, EC_COMMAND_PORT);

		ret = ec_wait_read();
		if (ret)
			goto error;
		pin_tmp = inb(EC_STATUS_PORT) & EC_STATUS_BIT;
		if (pin_tmp == EC_TBL_NOTFOUND)
			goto pass;

		ret = ec_wait_write();
		if (ret)
			goto error;
		outb(EC_TBL_GET_DEVID, EC_COMMAND_PORT);

		ret = ec_wait_read();
		if (ret)
			goto error;
		device_id = inb(EC_STATUS_PORT) & EC_STATUS_BIT;

		ddata->dym_tbl[i].device_id = device_id;
		ddata->dym_tbl[i].hw_pin_num = pin_tmp;
	}

pass:
	mutex_unlock(&ddata->lock);
	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");

	return ret;
}
EXPORT_SYMBOL(adv_get_dynamic_tab);

int adv_ec_get_productname(struct adv_ec_ddata *ddata, char *product)
{
	const char *vendor, *device;
	int length = 0;

	/* Check it is Advantech board */
	vendor = dmi_get_system_info(DMI_SYS_VENDOR);
	if (memcmp(vendor, "Advantech", sizeof("Advantech")) != 0)
		return -ENODEV;

	/* Get product model name */
	device = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (device) {
		while ((device[length] != ' ') && (length < AMI_ADVANTECH_BOARD_ID_LENGTH))
			length++;
		memset(product, 0, AMI_ADVANTECH_BOARD_ID_LENGTH);
		memmove(product, device, length);

		dev_info(ddata->dev, "BIOS Product Name = %s\n", product);

		return 0;
	}

	dev_warn(ddata->dev, "This device is not Advantech Devices (%s)!\n", product);

	return -ENODEV;
}
EXPORT_SYMBOL(adv_ec_get_productname);

int ahc1ec_read_adc_value(struct adv_ec_ddata *ddata, unsigned char hwpin,
			  unsigned char multi)
{
	int ret;
	u32 ret_val;
	unsigned int LSB, MSB;

	mutex_lock(&ddata->lock);
	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_ADC_INDEX_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(hwpin, EC_STATUS_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;

	if (inb(EC_STATUS_PORT) == EC_TBL_NOTFOUND) {
		mutex_unlock(&ddata->lock);
		return -1;
	}

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_ADC_LSB_READ, EC_COMMAND_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;
	LSB = inb(EC_STATUS_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_ADC_MSB_READ, EC_COMMAND_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;
	MSB = inb(EC_STATUS_PORT);
	ret_val = ((MSB << 8) | LSB) & EC_ADC_VALID_BIT;
	ret_val = ret_val * multi * 100;

	mutex_unlock(&ddata->lock);
	return ret_val;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_read_adc_value);

int ahc1ec_read_acpi_value(struct adv_ec_ddata *ddata,
			   unsigned char addr, unsigned char *pvalue)
{
	int ret;
	unsigned char value;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_ACPI_RAM_READ, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(addr, EC_STATUS_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;
	value = inb(EC_STATUS_PORT);
	*pvalue = value;

	mutex_unlock(&ddata->lock);

	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_read_acpi_value);

int ahc1ec_write_acpi_value(struct adv_ec_ddata *ddata,
			    unsigned char addr, unsigned char value)
{
	int ret;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_ACPI_DATA_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(addr, EC_STATUS_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(value, EC_STATUS_PORT);

	mutex_unlock(&ddata->lock);
	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_write_acpi_value);

int ahc1ec_read_gpio_status(struct adv_ec_ddata *ddata, unsigned char pin_number,
			    unsigned char *pvalue)
{
	int ret;

	unsigned char gpio_status_value;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_GPIO_INDEX_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(pin_number, EC_STATUS_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;

	if (inb(EC_STATUS_PORT) == EC_TBL_NOTFOUND) {
		ret = -1;
		goto error;
	}

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_GPIO_STATUS_READ, EC_COMMAND_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;
	gpio_status_value = inb(EC_STATUS_PORT);

	*pvalue = gpio_status_value;
	mutex_unlock(&ddata->lock);
	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_read_gpio_status);

int ahc1ec_write_gpio_status(struct adv_ec_ddata *ddata, unsigned char pin_number,
			     unsigned char value)
{
	int ret;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_GPIO_INDEX_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(pin_number, EC_STATUS_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;

	if (inb(EC_STATUS_PORT) == EC_TBL_NOTFOUND) {
		ret = -1;
		goto error;
	}

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_GPIO_STATUS_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(value, EC_STATUS_PORT);

	mutex_unlock(&ddata->lock);
	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_write_gpio_status);

int ahc1ec_read_gpio_dir(struct adv_ec_ddata *ddata, unsigned char pin_number,
			 unsigned char *pvalue)
{
	int ret;
	unsigned char gpio_dir_value;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_GPIO_INDEX_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(pin_number, EC_STATUS_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;

	if (inb(EC_STATUS_PORT) == EC_TBL_NOTFOUND) {
		ret = -1;
		goto error;
	}

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_GPIO_DIR_READ, EC_COMMAND_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;
	gpio_dir_value = inb(EC_STATUS_PORT);
	*pvalue = gpio_dir_value;

	mutex_unlock(&ddata->lock);
	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_read_gpio_dir);

int ahc1ec_write_gpio_dir(struct adv_ec_ddata *ddata, unsigned char pin_number,
			  unsigned char value)
{
	int ret;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_GPIO_INDEX_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(pin_number, EC_STATUS_PORT);

	ret = ec_wait_read();
	if (ret)
		goto error;

	if (inb(EC_STATUS_PORT) == EC_TBL_NOTFOUND) {
		mutex_unlock(&ddata->lock);
		return -1;
	}

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(EC_GPIO_DIR_WRITE, EC_COMMAND_PORT);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(value, EC_STATUS_PORT);

	mutex_unlock(&ddata->lock);
	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_write_gpio_dir);

int ahc1ec_write_hwram_command(struct adv_ec_ddata *ddata, unsigned char data)
{
	int ret;

	mutex_lock(&ddata->lock);

	ret = ec_wait_write();
	if (ret)
		goto error;
	outb(data, EC_COMMAND_PORT);

	mutex_unlock(&ddata->lock);
	return 0;

error:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "Wait for IBF or OBF too long.\n");
	return ret;
}
EXPORT_SYMBOL(ahc1ec_write_hwram_command);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ahc1ec0-core");
MODULE_DESCRIPTION("Advantech AHC1EC0 Embedded Controller Core");
MODULE_AUTHOR("Campion Kang <campion.kang@advantech.com.tw>");
MODULE_VERSION("1.0");
