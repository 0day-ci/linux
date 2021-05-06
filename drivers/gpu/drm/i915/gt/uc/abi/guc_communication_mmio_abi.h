/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_COMMUNICATION_MMIO_ABI_H
#define _ABI_GUC_COMMUNICATION_MMIO_ABI_H

/**
 * DOC: GuC MMIO based communication
 *
 * The MMIO based communication between Host and GuC relies on special
 * hardware registers which format could be defined by the software
 * (so called scratch registers).
 *
 * Each MMIO based message, both Host to GuC (H2G) and GuC to Host (G2H)
 * messages, which maximum length depends on number of available scratch
 * registers, is directly written into those scratch registers.
 *
 * For Gen9+, there are 16 software scratch registers 0xC180-0xC1B8,
 * but no H2G command takes more than 8 parameters and the GuC firmware
 * itself uses an 8-element array to store the H2G message.
 *
 * For Gen11+, there are additional 4 registers 0x190240-0x19024C, which
 * are, regardless on lower count, preffered over legacy ones.
 *
 * The MMIO based communication is mainly used during driver initialization
 * phase to setup the CTB based communication that will be used afterwards.
 *
 * Format of the MMIO messages follows definitions of `HXG Message`_.
 */

#define GUC_MAX_MMIO_MSG_LEN		8

#endif /* _ABI_GUC_COMMUNICATION_MMIO_ABI_H */
