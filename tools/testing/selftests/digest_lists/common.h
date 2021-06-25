/* SPDX-License-Identifier: GPL-2.0 */
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
 * File: common.h
 *      Header of common.c
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
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/hash_info.h>

#define BUFFER_SIZE 1024

int write_buffer(char *path, char *buffer, size_t buffer_len);
int read_buffer(char *path, char **buffer, size_t *buffer_len, bool alloc,
		bool is_char);
