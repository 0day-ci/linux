/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, Linaro Ltd. All rights reserved.
 */

#ifndef _DT_BINDINGS_TCPM_QCOM_PMIC_USB_PDPHY_H
#define _DT_BINDINGS_TCPM_QCOM_PMIC_USB_PDPHY_H

#define PMIC_PDPHY_SIG_TX_IRQ		0x0
#define PMIC_PDPHY_SIG_RX_IRQ		0x1
#define PMIC_PDPHY_MSG_TX_IRQ		0x2
#define PMIC_PDPHY_MSG_RX_IRQ		0x3
#define PMIC_PDPHY_MSG_TX_FAIL_IRQ	0x4
#define PMIC_PDPHY_MSG_TX_DISCARD_IRQ	0x5
#define PMIC_PDPHY_MSG_RX_DISCARD_IRQ	0x6
#define PMIC_PDPHY_FR_SWAP_IRQ		0x7

#endif
