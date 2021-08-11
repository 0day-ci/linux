/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _UAPI__LINUX_IMX_VPU_H
#define _UAPI__LINUX_IMX_VPU_H

#include <linux/videodev2.h>

/*imx v4l2 event*/
//error happened in dec/enc
#define V4L2_EVENT_CODEC_ERROR		(V4L2_EVENT_PRIVATE_START + 1)
//frame loss in dec/enc
#define V4L2_EVENT_SKIP			(V4L2_EVENT_PRIVATE_START + 2)

#endif	//#ifndef _UAPI__LINUX_IMX_VPU_H
