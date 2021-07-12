/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __NXP_EZPORT_H
#define __NXP_EZPORT_H

void ezport_reset(struct gpio_desc *reset);
int ezport_flash(struct spi_device *spi, struct gpio_desc *reset, const char *fwname);
int ezport_verify(struct spi_device *spi, struct gpio_desc *reset, const char *fwname);

#endif
