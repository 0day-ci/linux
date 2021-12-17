/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved
 */

#ifndef __TEGRA_CBBERR_H
#define __TEGRA_CBBERR_H

struct tegra_noc_errors {
	char *errcode;
	char *src;
	char *type;
};

struct tegra_cbb {
	struct tegra_cbb_err_ops *ops;
	struct platform_device *pdev;
	void *err_rec;
};

struct tegra_cbb_err_ops {
	int (*cbb_err_debugfs_show)(struct tegra_cbb *cbb, struct seq_file *s, void *v);
	int (*cbb_intr_enable)(struct tegra_cbb *cbb);
	void (*cbb_err_enable)(struct tegra_cbb *cbb);
	void (*faulten)(struct tegra_cbb *cbb);
	void (*stallen)(struct tegra_cbb *cbb);
	void (*errclr)(struct tegra_cbb *cbb);
	u32 (*errvld)(struct tegra_cbb *cbb);
};

int tegra_cbb_err_getirq(struct platform_device *pdev, int *nonsec_irq, int *sec_irq);
void print_cbb_err(struct seq_file *file, const char *fmt, ...);
int tegra_cbberr_en_register_isr(struct tegra_cbb *cbb);

void print_cache(struct seq_file *file, u32 cache);
void print_prot(struct seq_file *file, u32 prot);

void tegra_cbb_faulten(struct tegra_cbb *cbb);
void tegra_cbb_stallen(struct tegra_cbb *cbb);
void tegra_cbb_errclr(struct tegra_cbb *cbb);
u32 tegra_cbb_errvld(struct tegra_cbb *cbb);

#endif /* __TEGRA_CBBERR_H */
