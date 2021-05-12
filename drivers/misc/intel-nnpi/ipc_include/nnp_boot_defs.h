/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNP_BOOT_DEFS_H
#define _NNP_BOOT_DEFS_H

#include <linux/bits.h>

/*
 * Value fields of card->host doorbell status register HOST_PCI_DOORBELL_VALUE
 */
#define NNP_CARD_BOOT_STATE_MASK            GENMASK(7, 0)
#define NNP_CARD_BIOS_UPDATE_COUNTER_MASK   GENMASK(11, 8)
#define NNP_CARD_ERROR_MASK                 GENMASK(15, 12)
#define NNP_CARD_KEEP_ALIVE_MASK            GENMASK(23, 20)

/* Possible values for card boot state */
/* BIOS has not yet initialized */
#define NNP_CARD_BOOT_STATE_NOT_READY       0
/* BIOS initilaized and waiting for os boot image over PCIe */
#define NNP_CARD_BOOT_STATE_BIOS_READY      1
/* recovery BIOS initilaized and waiting for capsule update over PCIe */
#define NNP_CARD_BOOT_STATE_RECOVERY_BIOS_READY 2
/* BIOS copied boot image successfully, os boot has started */
#define NNP_CARD_BOOT_STATE_BOOT_STARTED    3
/* card has booted and card driver has loaded */
#define NNP_CARD_BOOT_STATE_DRV_READY       4
/* card driver finished initialization and user space daemon has started */
#define NNP_CARD_BOOT_STATE_CARD_READY      8
/* BIOS copied data into the system info structure */
#define NNP_CARD_BOOT_STATE_BIOS_SYSINFO_READY 10
/* BIOS capsule update has started flashing the BIOS image */
#define NNP_CARD_BOOT_STATE_BIOS_FLASH_STARTED 32

/* Possible card error values */
#define NNP_CARD_ERROR_HOST_ERROR           1
#define NNP_CARD_ERROR_BOOT_PARAMS          2
#define NNP_CARD_ERROR_IMAGE_COPY           3
#define NNP_CARD_ERROR_CORRUPTED_IMAGE      4
#define NNP_CARD_ERROR_NOT_CAPSULE          8
#define NNP_CARD_ERROR_CAPSULE_FAILED       9
/*
 * Value fields of host->card doorbell status register PCI_HOST_DOORBELL_VALUE
 */
#define NNP_HOST_BOOT_STATE_MASK              GENMASK(3, 0)
#define NNP_HOST_ERROR_MASK                   GENMASK(7, 4)
#define NNP_HOST_DRV_STATE_MASK               GENMASK(11, 8)
#define NNP_HOST_DRV_REQUEST_SELF_RESET_MASK  BIT(16)
#define NNP_HOST_KEEP_ALIVE_MASK              GENMASK(23, 20)
#define NNP_HOSY_P2P_POKE_MASK                GENMASK(31, 24)

/* Possible values for host boot state */
/* boot/capsule image is not loaded yet to memory */
#define NNP_HOST_BOOT_STATE_NOT_READY               0
/* host driver is up and ready */
#define NNP_HOST_BOOT_STATE_DRV_READY               (BIT(3) | BIT(0))
/* debug os image is loaded and ready in memory */
#define NNP_HOST_BOOT_STATE_DEBUG_OS_IMAGE_READY    (BIT(3) | BIT(1))

/* Possible values for host error */
#define NNP_HOST_ERROR_CANNOT_LOAD_IMAGE     1

/* Possible values for host driver state */
/* driver did not detected the device yet */
#define NNP_HOST_DRV_STATE_NOT_READY         0
/* driver initialized and ready */
#define NNP_HOST_DRV_STATE_READY             1
/* host/card protocol version mismatch */
#define NNP_HOST_DRV_STATE_VERSION_ERROR     2

#endif // of _NNP_BOOT_DEFS_H
