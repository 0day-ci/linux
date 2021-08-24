// SPDX-License-Identifier: GPL-2.0-only
/*
 * gpio-event-gen - generate GPIO line events from userspace
 *
 * Copyright (C) 2020 Intel Corporation
 * Author: Christopher S Hall <christopher.s.hall@intel.com>
 *
 * Adapted from gpio-event-mon.c
 * Copyright (C) 2016 Linus Walleij
 *
 * Usage:
 *	gpio-event-gen -n <device-name> -o <offset>
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/gpio.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NSEC_PER_SEC (1000000000ULL)
#define TIMESPEC_TO_U64(x) (((uint64_t)(x).tv_sec) * NSEC_PER_SEC + (x).tv_nsec)
#define U64_TO_TIMESPEC(x)						\
	((struct timespec){	.tv_sec = (x) / NSEC_PER_SEC,		\
				.tv_nsec = (x) % NSEC_PER_SEC})

int sleep_until(uint64_t systime)
{
	struct timespec wait_until;

	wait_until = U64_TO_TIMESPEC(systime);
	return clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &wait_until, NULL);
}

int generate_events(const char *device_name,
		    unsigned int line[],
		    unsigned int num_lines,
		    uint32_t flags,
		    unsigned int loops)
{
	struct gpio_v2_line_request req;
	struct gpio_v2_line_config config;
	uint64_t trigger_time;
	struct timespec now;
	char *chrdev_name;
	int ret, fd;
	int i = 0;

	ret = asprintf(&chrdev_name, "/dev/%s", device_name);
	if (ret < 0)
		return -ENOMEM;

	fd = open(chrdev_name, 0);
	if (fd == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s\n", chrdev_name);
		goto exit_close_error;
	}

	memset(&config, 0, sizeof(config));
	config.flags = flags;

	memset(&req, 0, sizeof(req));

	for (i = 0; i < num_lines; i++)
		req.offsets[i] = line[i];
	req.num_lines = num_lines;

	req.config = config;
	strcpy(req.consumer, "gpio-event-gen");

	ret = ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req);
	if (ret == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to issue GET EVENT IOCTL (%d)\n", ret);
		goto exit_close_error;
	}

	if (req.num_lines == 1) {
		fprintf(stdout, "Generating events on line %u on %s\n",
			line[0], device_name);
	} else {
		fprintf(stdout, "Generating events on %s for line %u",
			device_name, line[0]);
		for (i = 1; i < num_lines; i++)
			fprintf(stdout, " line %u", line[i]);
	}

	clock_gettime(CLOCK_REALTIME, &now);
	trigger_time = TIMESPEC_TO_U64(now);
	trigger_time -= trigger_time % NSEC_PER_SEC;
	trigger_time += 2 * NSEC_PER_SEC;
	i = 0;
	while (1) {
		struct gpio_output_event out_event;

		out_event.timestamp = trigger_time;
		printf("GPIO EVENT TRIGGER: %llu\n", trigger_time);
		ret = write(req.fd, &out_event, sizeof(out_event));
		if (ret == -1) {
			ret = -errno;
			fprintf(stderr, "Failed to write event spec(%s)\n",
				strerror(-ret));
			break;
		}

		if (ret != sizeof(out_event)) {
			fprintf(stderr, "Writing event spec failed\n");
			ret = -EIO;
			break;
		}

		sleep_until(trigger_time + NSEC_PER_SEC / 10);
		trigger_time += NSEC_PER_SEC / 2;

		i++;
		if (i == loops)
			break;
	}

exit_close_error:
	if (close(fd) == -1)
		perror("Failed to close GPIO character device file");
	free(chrdev_name);
	return ret;
}

void print_usage(void)
{
	fprintf(stderr, "Usage: gpio-event-gen [options]...;"
		"Listen to events on GPIO lines, 0->1 1->0;"
		"  -n <name>  Listen on GPIOs on a named device;"
		"(must be stated);"
		"  -o <n>     Offset to monitor;"
		" [-c <n>]    Do <n> loops;"
		"(optional, infinite loop if not stated);"
		"  -?         This helptext;"
		"Example: gpio-event-gen -n gpiochip0 -o 0"
	);
}

int main(int argc, char **argv)
{
	uint32_t flags = GPIO_V2_LINE_FLAG_OUTPUT;
	const char *device_name = NULL;
	unsigned int lines[GPIO_V2_LINES_MAX];
	unsigned int loops = 0;
	int num_lines = 0;
	int c;

	while ((c = getopt(argc, argv, "c:n:o:dsrf?")) != -1) {
		switch (c) {
		case 'c':
			loops = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			device_name = optarg;
			break;
		case 'o':
			if (num_lines >= GPIO_V2_LINES_MAX) {
				print_usage();
				return -1;
			}
			lines[num_lines] = strtoul(optarg, NULL, 10);
			num_lines++;
			break;
		case '?':
			print_usage();
			return -1;
		}
	}

	if (!device_name || num_lines == -1) {
		print_usage();
		return -1;
	}

	return generate_events(device_name, lines, num_lines,
			       flags, loops);
}
