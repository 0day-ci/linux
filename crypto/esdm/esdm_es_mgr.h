/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _ESDM_ES_MGR_H
#define _ESDM_ES_MGR_H

#include "esdm_es_mgr_cb.h"

/*************************** General ESDM parameter ***************************/

#define ESDM_DRNG_BLOCKSIZE 64		/* Maximum of DRNG block sizes */

/* Helper to concatenate a macro with an integer type */
#define ESDM_PASTER(x, y) x ## y
#define ESDM_UINT32_C(x) ESDM_PASTER(x, U)

/************************* Entropy sources management *************************/

extern struct esdm_es_cb *esdm_es[];

#define for_each_esdm_es(ctr)		\
	for ((ctr) = 0; (ctr) < esdm_ext_es_last; (ctr)++)

bool esdm_state_min_seeded(void);
int esdm_rand_initialize(void);
bool esdm_state_operational(void);

extern u32 esdm_write_wakeup_bits;
void esdm_set_entropy_thresh(u32 new);
u32 esdm_avail_entropy(void);
void esdm_reset_state(void);

bool esdm_state_fully_seeded(void);

int esdm_pool_trylock(void);
void esdm_pool_unlock(void);
void esdm_pool_all_numa_nodes_seeded(bool set);

bool esdm_fully_seeded_eb(bool fully_seeded, struct entropy_buf *eb);
void esdm_unset_fully_seeded(struct esdm_drng *drng);
void esdm_fill_seed_buffer(struct entropy_buf *eb, u32 requested_bits);
void esdm_init_ops(struct entropy_buf *eb);

#endif /* _ESDM_ES_MGR_H */
