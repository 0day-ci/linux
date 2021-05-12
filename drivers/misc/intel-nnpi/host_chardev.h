/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNPDRV_INFERENCE_H
#define _NNPDRV_INFERENCE_H

int nnp_init_host_interface(void);
void nnp_release_host_interface(void);

struct file *nnp_host_file_get(int host_fd);

#endif
