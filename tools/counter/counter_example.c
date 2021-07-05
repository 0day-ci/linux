// SPDX-License-Identifier: GPL-2.0-only
/* Counter - example userspace application
 *
 * The userspace application opens /dev/counter0, configures the
 * COUNTER_EVENT_INDEX event channel 0 to gather Count 0 count and Count
 * 1 count, and prints out the data as it becomes available on the
 * character device node.
 *
 * Copyright (C) 2021 William Breathitt Gray
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/counter.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct counter_watch watches[2] = {
	{
		/* Component data: Count 0 count */
		.component.type = COUNTER_COMPONENT_COUNT,
		.component.scope = COUNTER_SCOPE_COUNT,
		.component.parent = 0,
		/* Event type: Index */
		.event = COUNTER_EVENT_INDEX,
		/* Device event channel 0 */
		.channel = 0,
	},
	{
		/* Component data: Count 1 count */
		.component.type = COUNTER_COMPONENT_COUNT,
		.component.scope = COUNTER_SCOPE_COUNT,
		.component.parent = 1,
		/* Event type: Index */
		.event = COUNTER_EVENT_INDEX,
		/* Device event channel 0 */
		.channel = 0,
	},
};

int main(void)
{
	int fd;
	int ret;
	struct counter_event event_data[2];

	fd = open("/dev/counter0", O_RDWR);
	if (fd == -1) {
		perror("Unable to open /dev/counter0");
		return -errno;
	}

	ret = ioctl(fd, COUNTER_ADD_WATCH_IOCTL, watches);
	if (ret == -1) {
		perror("Error adding watches[0]");
		return -errno;
	}
	ret = ioctl(fd, COUNTER_ADD_WATCH_IOCTL, watches + 1);
	if (ret == -1) {
		perror("Error adding watches[1]");
		return -errno;
	}
	ret = ioctl(fd, COUNTER_ENABLE_EVENTS_IOCTL);
	if (ret == -1) {
		perror("Error enabling events");
		return -errno;
	}

	for (;;) {
		ret = read(fd, event_data, sizeof(event_data));
		if (ret == -1) {
			perror("Failed to read event data");
			return -errno;
		}

		if (ret != sizeof(event_data)) {
			fprintf(stderr, "Failed to read event data\n");
			return -EIO;
		}

		printf("Timestamp 0: %llu\tCount 0: %llu\n"
		       "Error Message 0: %s\n"
		       "Timestamp 1: %llu\tCount 1: %llu\n"
		       "Error Message 1: %s\n",
		       (unsigned long long)event_data[0].timestamp,
		       (unsigned long long)event_data[0].value,
		       strerror(event_data[0].status),
		       (unsigned long long)event_data[1].timestamp,
		       (unsigned long long)event_data[1].value,
		       strerror(event_data[1].status));
	}

	return 0;
}
