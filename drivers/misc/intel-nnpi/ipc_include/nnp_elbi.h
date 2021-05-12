/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNP_ELBI_H
#define _NNP_ELBI_H

#include <linux/bits.h>

#define ELBI_BASE                             0  /* offset of ELBI registers */
#define ELBI_LINE_BDF                         (ELBI_BASE + 0x004)

/*
 * COMMAND FIFO registers
 */
#define ELBI_COMMAND_WRITE_WO_MSI_LOW         (ELBI_BASE + 0x050)
#define ELBI_COMMAND_WRITE_WO_MSI_HIGH        (ELBI_BASE + 0x054)
#define ELBI_COMMAND_WRITE_W_MSI_LOW          (ELBI_BASE + 0x058)
#define ELBI_COMMAND_WRITE_W_MSI_HIGH         (ELBI_BASE + 0x05C)

#define ELBI_COMMAND_FIFO_0_LOW		 (ELBI_BASE + 0x080)
#define ELBI_COMMAND_FIFO_LOW(i)         (ELBI_COMMAND_FIFO_0_LOW + (i) * 8 + 0)
#define ELBI_COMMAND_FIFO_HIGH(i)        (ELBI_COMMAND_FIFO_0_LOW + (i) * 8 + 4)
#define ELBI_COMMAND_FIFO_DEPTH          16

#define ELBI_COMMAND_IOSF_CONTROL        (ELBI_BASE + 0x044)
#define CMDQ_READ_PTR_MASK               GENMASK(3, 0)
#define CMDQ_WRITE_PTR_MASK              GENMASK(12, 8)

#define ELBI_COMMAND_PCI_CONTROL                          (ELBI_BASE + 0x048)
#define ELBI_COMMAND_PCI_CONTROL_ALMOST_EMPTY_TH_MASK     GENMASK(3, 0)
#define ELBI_COMMAND_PCI_CONTROL_FLUSH_MASK               BIT(8)

/*
 * RESPONSE FIFO registers
 */
#define ELBI_RESPONSE_FIFO_0_LOW        (ELBI_BASE + 0x100)
#define ELBI_RESPONSE_FIFO_LOW(i)       (ELBI_RESPONSE_FIFO_0_LOW + (i) * 8 + 0)
#define ELBI_RESPONSE_FIFO_HIGH(i)      (ELBI_RESPONSE_FIFO_0_LOW + (i) * 8 + 4)
#define ELBI_RESPONSE_FIFO_DEPTH        16

#define ELBI_RESPONSE_PCI_CONTROL       (ELBI_BASE + 0x060)
#define RESPQ_READ_PTR_MASK             GENMASK(3, 0)
#define RESPQ_WRITE_PTR_MASK            GENMASK(12, 8)

/*
 * Host side interrupt status & mask register
 */
#define ELBI_PCI_STATUS                       (ELBI_BASE + 0x008)
#define ELBI_PCI_MSI_MASK                     (ELBI_BASE + 0x00C)
#define ELBI_PCI_STATUS_CMDQ_EMPTY                    BIT(0)
#define ELBI_PCI_STATUS_CMDQ_ALMOST_EMPTY             BIT(1)
#define ELBI_PCI_STATUS_CMDQ_READ_UPDATE              BIT(2)
#define ELBI_PCI_STATUS_CMDQ_FLUSH                    BIT(3)
#define ELBI_PCI_STATUS_CMDQ_WRITE_ERROR              BIT(4)
#define ELBI_PCI_STATUS_RESPQ_FULL                    BIT(5)
#define ELBI_PCI_STATUS_RESPQ_ALMOST_FULL             BIT(6)
#define ELBI_PCI_STATUS_RESPQ_NEW_RESPONSE            BIT(7)
#define ELBI_PCI_STATUS_RESPQ_FLUSH                   BIT(8)
#define ELBI_PCI_STATUS_RESPQ_READ_ERROR              BIT(9)
#define ELBI_PCI_STATUS_RESPQ_READ_POINTER_ERROR      BIT(10)
#define ELBI_PCI_STATUS_DOORBELL                      BIT(11)
#define ELBI_PCI_STATUS_DOORBELL_READ                 BIT(12)
#define ELBI_PCI_STATUS_FLR_REQUEST                   BIT(13)
#define ELBI_PCI_STATUS_LOCAL_D3                      BIT(14)
#define ELBI_PCI_STATUS_LOCAL_FLR                     BIT(15)

/* DOORBELL registers */
#define ELBI_PCI_HOST_DOORBELL_VALUE                  (ELBI_BASE + 0x034)
#define ELBI_HOST_PCI_DOORBELL_VALUE                  (ELBI_BASE + 0x038)
#define CARD_DOORBELL_REG                             ELBI_HOST_PCI_DOORBELL_VALUE
#define HOST_DOORBELL_REG                             ELBI_PCI_HOST_DOORBELL_VALUE

/* CPU_STATUS registers */
/* CPU_STATUS_0 - Updated by BIOS with postcode */
#define ELBI_CPU_STATUS_0                             (ELBI_BASE + 0x1B8)
/* CPU_STATUS_1 - Updated by BIOS with BIOS flash progress */
#define ELBI_CPU_STATUS_1                             (ELBI_BASE + 0x1BC)
/* CPU_STATUS_2 - Updated by card driver - bitfields below */
#define ELBI_CPU_STATUS_2                             (ELBI_BASE + 0x1C0)
/* CPU_STATUS_3 - not used */
#define ELBI_CPU_STATUS_3                             (ELBI_BASE + 0x1C4)

/* Bitfields updated in ELBI_CPU_STATUS_2 indicating card driver states */
#define ELBI_CPU_STATUS_2_FLR_MODE_MASK               GENMASK(1, 0)

/* values for FLR_MODE */
#define FLR_MODE_WARN_RESET  0
#define FLR_MODE_COLD_RESET  1
#define FLR_MODE_IGNORE      3

#endif
