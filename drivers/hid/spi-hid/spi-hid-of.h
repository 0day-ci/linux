/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spi-hid-of.h
 *
 * Copyright (c) 2021 Microsoft Corporation
 */

#ifndef SPI_HID_OF_H
#define SPI_HID_OF_H

extern const struct of_device_id spi_hid_of_match[];

/* Config structure is filled with data from Device Tree */
struct spi_hid_of_config {
	u32 input_report_header_address;
	u32 input_report_body_address;
	u32 output_report_address;
	u8 read_opcode;
	u8 write_opcode;
	u32 post_power_on_delay_ms;
	u32 minimal_reset_delay_ms;
	struct gpio_desc *reset_gpio;
	struct regulator *supply;
};

int spi_hid_of_populate_config(struct spi_hid_of_config *conf,
				struct device *dev);
int spi_hid_of_power_down(struct spi_hid_of_config *conf);
int spi_hid_of_power_up(struct spi_hid_of_config *conf);
void spi_hid_of_assert_reset(struct spi_hid_of_config *conf);
void spi_hid_of_deassert_reset(struct spi_hid_of_config *conf);
void spi_hid_of_sleep_minimal_reset_delay(struct spi_hid_of_config *conf);

#endif
