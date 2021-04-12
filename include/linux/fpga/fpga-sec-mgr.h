/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for FPGA Security Manager
 *
 * Copyright (C) 2019-2020 Intel Corporation, Inc.
 */
#ifndef _LINUX_FPGA_SEC_MGR_H
#define _LINUX_FPGA_SEC_MGR_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct fpga_sec_mgr;

/**
 * struct fpga_sec_mgr_ops - device specific operations
 */
struct fpga_sec_mgr_ops {
};

struct fpga_sec_mgr {
	const char *name;
	struct device dev;
	const struct fpga_sec_mgr_ops *sops;
	struct mutex lock;		/* protect data structure contents */
	void *priv;
};

struct fpga_sec_mgr *
fpga_sec_mgr_create(struct device *dev, const char *name,
		    const struct fpga_sec_mgr_ops *sops, void *priv);

struct fpga_sec_mgr *
devm_fpga_sec_mgr_create(struct device *dev, const char *name,
			 const struct fpga_sec_mgr_ops *sops, void *priv);

int fpga_sec_mgr_register(struct fpga_sec_mgr *smgr);
int devm_fpga_sec_mgr_register(struct device *dev,
			       struct fpga_sec_mgr *smgr);
void fpga_sec_mgr_unregister(struct fpga_sec_mgr *smgr);
void fpga_sec_mgr_free(struct fpga_sec_mgr *smgr);

#endif
