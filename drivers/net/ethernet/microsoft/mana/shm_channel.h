/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2021, Microsoft Corporation. */

#ifndef _SHM_CHANNEL_H
#define _SHM_CHANNEL_H

struct shm_channel {
	void __iomem *base;
};

void shm_channel_init(struct shm_channel *sc, void __iomem *base);

int shm_channel_setup_hwc(struct shm_channel *sc, bool reset_vf, u64 eq_addr,
			  u64 cq_addr, u64 rq_addr, u64 sq_addr,
			  u32 eq_msix_index);

int shm_channel_teardown_hwc(struct shm_channel *sc, bool reset_vf);

#endif /* _SHM_CHANNEL_H */
