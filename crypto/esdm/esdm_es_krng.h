/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _ESDM_ES_RANDOM_H
#define _ESDM_ES_RANDOM_H

#include "esdm_es_mgr_cb.h"

#ifdef CONFIG_CRYPTO_ESDM_KERNEL_RNG

extern struct esdm_es_cb esdm_es_krng;

#endif /* CONFIG_CRYPTO_ESDM_KERNEL_RNG */

#endif /* _ESDM_ES_RANDOM_H */
