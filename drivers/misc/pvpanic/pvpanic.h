// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic Device Support
 *
 *  Copyright (C) 2021 Oracle.
 */

#ifndef PVPANIC_H_
#define PVPANIC_H_

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

int pvpanic_probe(void __iomem *base, unsigned int dev_cap);
int pvpanic_remove(void __iomem *base);
void pvpanic_set_events(void __iomem *base, unsigned int dev_events);

#endif /* PVPANIC_H_ */
