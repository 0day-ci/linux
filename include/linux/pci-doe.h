/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data Object Exchange was added to the PCI spec as an ECN to 5.0.
 *
 * Copyright (C) 2021 Huawei
 *     Jonathan Cameron <Jonathan.Cameron@huawei.com>
 */

#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mutex.h>

#ifndef LINUX_PCI_DOE_H
#define LINUX_PCI_DOE_H

struct pci_doe_prot {
	u16 vid;
	u8 type;
};

struct workqueue_struct;

enum pci_doe_state {
	DOE_IDLE,
	DOE_WAIT_RESP,
	DOE_WAIT_ABORT,
	DOE_WAIT_ABORT_ON_ERR,
};

struct pci_doe_exchange {
	u16 vid;
	u8 protocol;
	u32 *request_pl;
	size_t request_pl_sz;
	u32 *response_pl;
	size_t response_pl_sz;
};

/**
 * struct pci_doe - State to support use of DOE mailbox
 * @cap: Config space offset to base of DOE capability.
 * @pdev: PCI device that hosts this DOE.
 * @abort_c: Completion used for initial abort handling.
 * @irq: Interrupt used for signaling DOE ready or abort.
 * @prots: Cache of identifiers for protocols supported.
 * @num_prots: Size of prots cache.
 * @h: Used for DOE instance lifetime management.
 * @wq: Workqueue used to handle state machine and polling / timeouts.
 * @tasks: List of task in flight + pending.
 * @tasks_lock: Protect the tasks list.
 * @statemachine: Work item for the DOE state machine.
 * @state: Current state of this DOE.
 * @timeout_jiffies: 1 second after GO set.
 * @busy_retries: Count of retry attempts.
 * @abort: Request a manual abort (e.g. on init).
 * @dead: Used to mark a DOE for which an ABORT has timed out. Further messages
 *        will immediately be aborted with error.
 */
struct pci_doe {
	int cap;
	struct pci_dev *pdev;
	struct completion abort_c;
	int irq;
	struct pci_doe_prot *prots;
	int num_prots;
	struct list_head h;

	struct workqueue_struct *wq;
	struct list_head tasks;
	struct mutex tasks_lock;
	struct delayed_work statemachine;
	enum pci_doe_state state;
	unsigned long timeout_jiffies;
	unsigned int busy_retries;
	unsigned int abort:1;
	unsigned int dead:1;
};

int pci_doe_register_all(struct pci_dev *pdev);
void pci_doe_unregister_all(struct pci_dev *pdev);
struct pci_doe *pci_doe_find(struct pci_dev *pdev, u16 vid, u8 type);

int pci_doe_exchange_sync(struct pci_doe *doe, struct pci_doe_exchange *ex);

#endif
