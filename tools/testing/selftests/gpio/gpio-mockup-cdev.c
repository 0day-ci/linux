// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO mockup cdev test helper
 *
 * Copyright (C) 2020 Kent Gibson
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <linux/const.h>
#include <linux/ioctl.h>
#include <linux/types.h>

#define CONSUMER	"gpio-mockup-cdev"

#define GPIO_V2_LINE_NUM_ATTRS_MAX 10
#define GPIO_V2_LINES_MAX 64
#define GPIO_MAX_NAME_SIZE 32

#define GPIOHANDLE_REQUEST_BIAS_PULL_UP	(1UL << 5)
#define GPIOHANDLE_REQUEST_BIAS_DISABLE	(1UL << 7)
#define GPIOHANDLE_REQUEST_BIAS_PULL_DOWN	(1UL << 6)
#define GPIO_V2_LINE_GET_VALUES_IOCTL _IOWR(0xB4, 0x0E, struct gpio_v2_line_values)
#define GPIO_V2_GET_LINE_IOCTL _IOWR(0xB4, 0x07, struct gpio_v2_line_request)

enum gpio_v2_line_flag {
	GPIO_V2_LINE_FLAG_ACTIVE_LOW = _BITULL(1),
	GPIO_V2_LINE_FLAG_INPUT	= _BITULL(2),
	GPIO_V2_LINE_FLAG_OUTPUT = _BITULL(3),
	GPIO_V2_LINE_FLAG_BIAS_PULL_UP = _BITULL(8),
	GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN = _BITULL(9),
	GPIO_V2_LINE_FLAG_BIAS_DISABLED	= _BITULL(10),
};
enum gpio_v2_line_attr_id {
	GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES = 2,
};
struct gpio_v2_line_values {
	__aligned_u64 bits;
	__aligned_u64 mask;
};
struct gpio_v2_line_attribute {
	__u32 id;
	__u32 padding;
	union {
		__aligned_u64 flags;
		__aligned_u64 values;
		__u32 debounce_period_us;
	};
};
struct gpio_v2_line_config_attribute {
	struct gpio_v2_line_attribute attr;
	__aligned_u64 mask;
};
struct gpio_v2_line_config {
	__aligned_u64 flags;
	__u32 num_attrs;
	__u32 padding[5];
	struct gpio_v2_line_config_attribute attrs[GPIO_V2_LINE_NUM_ATTRS_MAX];
};
struct gpio_v2_line_request {
	__u32 offsets[GPIO_V2_LINES_MAX];
	char consumer[GPIO_MAX_NAME_SIZE];
	struct gpio_v2_line_config config;
	__u32 num_lines;
	__u32 event_buffer_size;
	__u32 padding[5];
	__s32 fd;
};

static int request_line_v2(int cfd, unsigned int offset,
			   uint64_t flags, unsigned int val)
{
	struct gpio_v2_line_request req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.num_lines = 1;
	req.offsets[0] = offset;
	req.config.flags = flags;
	strcpy(req.consumer, CONSUMER);
	if (flags & GPIO_V2_LINE_FLAG_OUTPUT) {
		req.config.num_attrs = 1;
		req.config.attrs[0].mask = 1;
		req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
		if (val)
			req.config.attrs[0].attr.values = 1;
	}
	ret = ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req);
	if (ret == -1)
		return -errno;
	return req.fd;
}


static int get_value_v2(int lfd)
{
	struct gpio_v2_line_values vals;
	int ret;

	memset(&vals, 0, sizeof(vals));
	vals.mask = 1;
	ret = ioctl(lfd, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals);
	if (ret == -1)
		return -errno;
	return vals.bits & 0x1;
}

static int request_line_v1(int cfd, unsigned int offset,
			   uint32_t flags, unsigned int val)
{
	struct gpiohandle_request req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.lines = 1;
	req.lineoffsets[0] = offset;
	req.flags = flags;
	strcpy(req.consumer_label, CONSUMER);
	if (flags & GPIOHANDLE_REQUEST_OUTPUT)
		req.default_values[0] = val;

	ret = ioctl(cfd, GPIO_GET_LINEHANDLE_IOCTL, &req);
	if (ret == -1)
		return -errno;
	return req.fd;
}

static int get_value_v1(int lfd)
{
	struct gpiohandle_data vals;
	int ret;

	memset(&vals, 0, sizeof(vals));
	ret = ioctl(lfd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &vals);
	if (ret == -1)
		return -errno;
	return vals.values[0];
}

static void usage(char *prog)
{
	printf("Usage: %s [-l] [-b <bias>] [-s <value>] [-u <uAPI>] <gpiochip> <offset>\n", prog);
	printf("        -b: set line bias to one of pull-down, pull-up, disabled\n");
	printf("               (default is to leave bias unchanged):\n");
	printf("        -l: set line active low (default is active high)\n");
	printf("        -s: set line value (default is to get line value)\n");
	printf("        -u: uAPI version to use (default is 2)\n");
	exit(-1);
}

static int wait_signal(void)
{
	int sig;
	sigset_t wset;

	sigemptyset(&wset);
	sigaddset(&wset, SIGHUP);
	sigaddset(&wset, SIGINT);
	sigaddset(&wset, SIGTERM);
	sigwait(&wset, &sig);

	return sig;
}

int main(int argc, char *argv[])
{
	char *chip;
	int opt, ret, cfd, lfd;
	unsigned int offset, val, abiv;
	uint32_t flags_v1;
	uint64_t flags_v2;

	abiv = 2;
	ret = 0;
	flags_v1 = GPIOHANDLE_REQUEST_INPUT;
	flags_v2 = GPIO_V2_LINE_FLAG_INPUT;

	while ((opt = getopt(argc, argv, "lb:s:u:")) != -1) {
		switch (opt) {
		case 'l':
			flags_v1 |= GPIOHANDLE_REQUEST_ACTIVE_LOW;
			flags_v2 |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
			break;
		case 'b':
			if (strcmp("pull-up", optarg) == 0) {
				flags_v1 |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
				flags_v2 |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
			} else if (strcmp("pull-down", optarg) == 0) {
				flags_v1 |= GPIOHANDLE_REQUEST_BIAS_PULL_DOWN;
				flags_v2 |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
			} else if (strcmp("disabled", optarg) == 0) {
				flags_v1 |= GPIOHANDLE_REQUEST_BIAS_DISABLE;
				flags_v2 |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
			}
			break;
		case 's':
			val = atoi(optarg);
			flags_v1 &= ~GPIOHANDLE_REQUEST_INPUT;
			flags_v1 |= GPIOHANDLE_REQUEST_OUTPUT;
			flags_v2 &= ~GPIO_V2_LINE_FLAG_INPUT;
			flags_v2 |= GPIO_V2_LINE_FLAG_OUTPUT;
			break;
		case 'u':
			abiv = atoi(optarg);
			break;
		default:
			usage(argv[0]);
		}
	}

	if (argc < optind + 2)
		usage(argv[0]);

	chip = argv[optind];
	offset = atoi(argv[optind + 1]);

	cfd = open(chip, 0);
	if (cfd == -1) {
		fprintf(stderr, "Failed to open %s: %s\n", chip, strerror(errno));
		return -errno;
	}

	if (abiv == 1)
		lfd = request_line_v1(cfd, offset, flags_v1, val);
	else
		lfd = request_line_v2(cfd, offset, flags_v2, val);

	close(cfd);

	if (lfd < 0) {
		fprintf(stderr, "Failed to request %s:%d: %s\n", chip, offset, strerror(-lfd));
		return lfd;
	}

	if (flags_v2 & GPIO_V2_LINE_FLAG_OUTPUT) {
		wait_signal();
	} else {
		if (abiv == 1)
			ret = get_value_v1(lfd);
		else
			ret = get_value_v2(lfd);
	}

	close(lfd);

	return ret;
}
