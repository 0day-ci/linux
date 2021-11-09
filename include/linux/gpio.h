/* SPDX-License-Identifier: GPL-2.0 */
/*
 * <linux/gpio.h>
 *
 * This is the LEGACY GPIO bulk include file, including legacy APIs. It is
 * used for GPIO drivers still referencing the global GPIO numberspace,
 * and should not be included in new code.
 *
 * If you're implementing a GPIO driver, only include <linux/gpio/driver.h>
 * If you're implementing a GPIO consumer, only include <linux/gpio/consumer.h>
 */
#ifndef __LINUX_GPIO_H
#define __LINUX_GPIO_H

#include <linux/errno.h>

/* see Documentation/driver-api/gpio/legacy.rst */

/* make these flag values available regardless of GPIO kconfig options */
#define GPIOF_DIR_OUT	(0 << 0)
#define GPIOF_DIR_IN	(1 << 0)

#define GPIOF_INIT_LOW	(0 << 1)
#define GPIOF_INIT_HIGH	(1 << 1)

#define GPIOF_IN		(GPIOF_DIR_IN)
#define GPIOF_OUT_INIT_LOW	(GPIOF_DIR_OUT | GPIOF_INIT_LOW)
#define GPIOF_OUT_INIT_HIGH	(GPIOF_DIR_OUT | GPIOF_INIT_HIGH)

/* Gpio pin is active-low */
#define GPIOF_ACTIVE_LOW        (1 << 2)

/* Gpio pin is open drain */
#define GPIOF_OPEN_DRAIN	(1 << 3)

/* Gpio pin is open source */
#define GPIOF_OPEN_SOURCE	(1 << 4)

#define GPIOF_EXPORT		(1 << 5)
#define GPIOF_EXPORT_CHANGEABLE	(1 << 6)
#define GPIOF_EXPORT_DIR_FIXED	(GPIOF_EXPORT)
#define GPIOF_EXPORT_DIR_CHANGEABLE (GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE)

/**
 * struct gpio - a structure describing a GPIO with configuration
 * @gpio:	the GPIO number
 * @flags:	GPIO configuration as specified by GPIOF_*
 * @label:	a literal description string of this GPIO
 */
struct gpio {
	unsigned	gpio;
	unsigned long	flags;
	const char	*label;
};

#ifdef CONFIG_GPIOLIB

#include <linux/compiler.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>

/* Platforms may implement their GPIO interface with library code,
 * at a small performance cost for non-inlined operations and some
 * extra memory (for code and for per-GPIO table entries).
 *
 * While the GPIO programming interface defines valid GPIO numbers
 * to be in the range 0..MAX_INT, this library restricts them to the
 * smaller range 0..ARCH_NR_GPIOS-1.
 *
 * ARCH_NR_GPIOS is somewhat arbitrary; it usually reflects the sum of
 * builtin/SoC GPIOs plus a number of GPIOs on expanders; the latter is
 * actually an estimate of a board-specific value.
 */

#ifndef ARCH_NR_GPIOS
#if defined(CONFIG_ARCH_NR_GPIO) && CONFIG_ARCH_NR_GPIO > 0
#define ARCH_NR_GPIOS CONFIG_ARCH_NR_GPIO
#else
#define ARCH_NR_GPIOS		512
#endif
#endif

/*
 * "valid" GPIO numbers are nonnegative and may be passed to
 * setup routines like gpio_request().  only some valid numbers
 * can successfully be requested and used.
 *
 * Invalid GPIO numbers are useful for indicating no-such-GPIO in
 * platform data and other tables.
 */

static inline bool gpio_is_valid(int number)
{
	return number >= 0 && number < ARCH_NR_GPIOS;
}

struct device;

/* caller holds gpio_lock *OR* gpio is marked as requested */
static inline struct gpio_chip *gpio_to_chip(unsigned gpio)
{
	return gpiod_to_chip(gpio_to_desc(gpio));
}

/* Always use the library code for GPIO management calls,
 * or when sleeping may be involved.
 */
extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);

static inline int gpio_direction_input(unsigned gpio)
{
	return gpiod_direction_input(gpio_to_desc(gpio));
}
static inline int gpio_direction_output(unsigned gpio, int value)
{
	return gpiod_direction_output_raw(gpio_to_desc(gpio), value);
}

static inline int gpio_get_value_cansleep(unsigned gpio)
{
	return gpiod_get_raw_value_cansleep(gpio_to_desc(gpio));
}
static inline void gpio_set_value_cansleep(unsigned gpio, int value)
{
	return gpiod_set_raw_value_cansleep(gpio_to_desc(gpio), value);
}

static inline int gpio_get_value(unsigned gpio)
{
	return gpiod_get_raw_value(gpio_to_desc(gpio));
}
static inline void gpio_set_value(unsigned gpio, int value)
{
	return gpiod_set_raw_value(gpio_to_desc(gpio), value);
}

static inline int gpio_to_irq(unsigned gpio)
{
	return gpiod_to_irq(gpio_to_desc(gpio));
}

extern int gpio_request_one(unsigned gpio, unsigned long flags, const char *label);
extern int gpio_request_array(const struct gpio *array, size_t num);
extern void gpio_free_array(const struct gpio *array, size_t num);

/* CONFIG_GPIOLIB: bindings for managed devices that want to request gpios */

struct device;

int devm_gpio_request(struct device *dev, unsigned gpio, const char *label);
int devm_gpio_request_one(struct device *dev, unsigned gpio,
			  unsigned long flags, const char *label);
#else /* ! CONFIG_GPIOLIB */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bug.h>

struct device;
struct gpio_chip;

static inline bool gpio_is_valid(int number)
{
	return false;
}

static inline int gpio_request(unsigned gpio, const char *label)
{
	return -ENOSYS;
}

static inline int gpio_request_one(unsigned gpio,
					unsigned long flags, const char *label)
{
	return -ENOSYS;
}

static inline int gpio_request_array(const struct gpio *array, size_t num)
{
	return -ENOSYS;
}

static inline void gpio_free(unsigned gpio)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(1);
}

static inline void gpio_free_array(const struct gpio *array, size_t num)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(1);
}

static inline int gpio_direction_input(unsigned gpio)
{
	return -ENOSYS;
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	return -ENOSYS;
}

static inline int gpio_get_value(unsigned gpio)
{
	/* GPIO can never have been requested or set as {in,out}put */
	WARN_ON(1);
	return 0;
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	/* GPIO can never have been requested or set as output */
	WARN_ON(1);
}

static inline int gpio_get_value_cansleep(unsigned gpio)
{
	/* GPIO can never have been requested or set as {in,out}put */
	WARN_ON(1);
	return 0;
}

static inline void gpio_set_value_cansleep(unsigned gpio, int value)
{
	/* GPIO can never have been requested or set as output */
	WARN_ON(1);
}

static inline int gpio_to_irq(unsigned gpio)
{
	/* GPIO can never have been requested or set as input */
	WARN_ON(1);
	return -EINVAL;
}

static inline int devm_gpio_request(struct device *dev, unsigned gpio,
				    const char *label)
{
	WARN_ON(1);
	return -EINVAL;
}

static inline int devm_gpio_request_one(struct device *dev, unsigned gpio,
					unsigned long flags, const char *label)
{
	WARN_ON(1);
	return -EINVAL;
}

#endif /* ! CONFIG_GPIOLIB */

#endif /* __LINUX_GPIO_H */
