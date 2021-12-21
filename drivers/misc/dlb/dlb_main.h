/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#ifndef __DLB_MAIN_H
#define __DLB_MAIN_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/types.h>

/*
 * Hardware related #defines and data structures.
 *
 */
#define DLB_MAX_NUM_VDEVS			16
#define DLB_MAX_NUM_DOMAINS			32
#define DLB_MAX_NUM_LDB_QUEUES			32 /* LDB == load-balanced */
#define DLB_MAX_NUM_DIR_QUEUES			64 /* DIR == directed */
#define DLB_MAX_NUM_LDB_PORTS			64
#define DLB_MAX_NUM_DIR_PORTS			DLB_MAX_NUM_DIR_QUEUES
#define DLB_MAX_NUM_LDB_CREDITS			8192
#define DLB_MAX_NUM_DIR_CREDITS			2048
#define DLB_MAX_NUM_HIST_LIST_ENTRIES		2048
#define DLB_MAX_NUM_AQED_ENTRIES		2048
#define DLB_MAX_NUM_QIDS_PER_LDB_CQ		8
#define DLB_MAX_NUM_SEQUENCE_NUMBER_GROUPS	2
#define DLB_MAX_NUM_SEQUENCE_NUMBER_MODES	5
#define DLB_QID_PRIORITIES			8
#define DLB_NUM_ARB_WEIGHTS			8
#define DLB_MAX_WEIGHT				255
#define DLB_NUM_COS_DOMAINS			4
#define DLB_MAX_CQ_COMP_CHECK_LOOPS		409600
#define DLB_MAX_QID_EMPTY_CHECK_LOOPS		(32 * 64 * 1024 * (800 / 30))
#define DLB_HZ					800000000
#define DLB_FUNC_BAR				0
#define DLB_CSR_BAR				2

#define PCI_DEVICE_ID_INTEL_DLB_PF		0x2710

/*
 * The dlb driver uses a different minor number for each device file, of which
 * there are:
 * - 33 per device (PF or VF/VDEV): 1 for the device, 32 for scheduling domains
 * - Up to 17 devices per PF: 1 PF and up to 16 VFs/VDEVs
 * - Up to 16 PFs per system
 */
#define DLB_MAX_NUM_PFS	  16
#define DLB_NUM_FUNCS_PER_DEVICE (1 + DLB_MAX_NUM_VDEVS)
#define DLB_MAX_NUM_DEVICES	 (DLB_MAX_NUM_PFS * DLB_NUM_FUNCS_PER_DEVICE)

enum dlb_device_type {
	DLB_PF,
};

struct dlb {
	struct pci_dev *pdev;
	int id;
};

#endif /* __DLB_MAIN_H */
