/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NXP_EZPORT_H
#define __NXP_EZPORT_H

extern void ezport_reset(struct gpio_desc *reset);
extern int ezport_flash(struct spi_device *spi, struct gpio_desc *reset, const char *fwname);
extern int ezport_verify(struct spi_device *spi, struct gpio_desc *reset, const char *fwname);

#endif
