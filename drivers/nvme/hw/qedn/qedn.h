/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#ifndef _QEDN_H_
#define _QEDN_H_

/* Driver includes */
#include "../../host/tcp-offload.h"

#define QEDN_MODULE_NAME "qedn"

struct qedn_ctx {
	struct pci_dev *pdev;
	struct nvme_tcp_ofld_dev qedn_ofld_dev;
};

#endif /* _QEDN_H_ */
