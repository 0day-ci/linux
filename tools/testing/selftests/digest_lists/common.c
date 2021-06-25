// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 * Copyright (C) 2017-2021 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: common.c
 *      Common functions.
 */

#include <sys/random.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/hash_info.h>

#include "common.h"

int write_buffer(char *path, char *buffer, size_t buffer_len)
{
	ssize_t to_write = buffer_len, written = 0;
	int ret = 0, fd;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -errno;

	while (to_write) {
		written = write(fd, buffer + buffer_len - to_write, to_write);
		if (written <= 0) {
			ret = -errno;
			break;
		}

		to_write -= written;
	}

	close(fd);
	return ret;
}

int read_buffer(char *path, char **buffer, size_t *buffer_len, bool alloc,
		bool is_char)
{
	ssize_t len = 0, read_len;
	int ret = 0, fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	if (alloc) {
		*buffer = NULL;
		*buffer_len = 0;
	}

	while (1) {
		if (alloc) {
			if (*buffer_len == len) {
				*buffer_len += BUFFER_SIZE;
				*buffer = realloc(*buffer, *buffer_len + 1);
				if (!*buffer) {
					ret = -ENOMEM;
					goto out;
				}
			}
		}

		read_len = read(fd, *buffer + len, *buffer_len - len);
		if (read_len < 0) {
			ret = -errno;
			goto out;
		}

		if (!read_len)
			break;

		len += read_len;
	}

	*buffer_len = len;
	if (is_char)
		(*buffer)[(*buffer_len)++] = '\0';
out:
	close(fd);
	if (ret < 0) {
		if (alloc) {
			free(*buffer);
			*buffer = NULL;
		}
	}

	return ret;
}
