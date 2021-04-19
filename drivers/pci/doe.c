// SPDX-License-Identifier: GPL-2.0
/*
 * Data Object Exchange ECN
 * https://members.pcisig.com/wg/PCI-SIG/document/14143
 *
 * Copyright (C) 2021 Huawei
 *     Jonathan Cameron <Jonathan.Cameron@huawei.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci-doe.h>
#include <linux/workqueue.h>

#define PCI_DOE_PROTOCOL_DISCOVERY 0

#define PCI_DOE_BUSY_MAX_RETRIES 16
#define PCI_DOE_POLL_INTERVAL (HZ / 128)

/* Timeout of 1 second from 6.xx.1 (Operation), ECN - Data Object Exchange */
#define PCI_DOE_TIMEOUT HZ

static irqreturn_t pci_doe_irq(int irq, void *data)
{
	struct pci_doe *doe = data;
	struct pci_dev *pdev = doe->pdev;
	u32 val;

	pci_read_config_dword(pdev, doe->cap + PCI_DOE_STATUS, &val);
	if (FIELD_GET(PCI_DOE_STATUS_INT_STATUS, val)) {
		pci_write_config_dword(pdev, doe->cap + PCI_DOE_STATUS, val);
		mod_delayed_work(system_wq, &doe->statemachine, 0);
		return IRQ_HANDLED;
	}
	/* Leave the error case to be handled outside IRQ */
	if (FIELD_GET(PCI_DOE_STATUS_ERROR, val)) {
		mod_delayed_work(system_wq, &doe->statemachine, 0);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * Only call when safe to directly access the DOE, either because no tasks yet
 * queued, or called from doe_statemachine_work() which has exclusive access to
 * the DOE config space.
 */
static void pci_doe_abort_start(struct pci_doe *doe)
{
	struct pci_dev *pdev = doe->pdev;

	pci_write_config_dword(pdev, doe->cap + PCI_DOE_CTRL,
			       PCI_DOE_CTRL_ABORT);

	doe->timeout_jiffies = jiffies + HZ;
	schedule_delayed_work(&doe->statemachine, HZ);
}

static int pci_doe_send_req(struct pci_doe *doe, struct pci_doe_exchange *ex)
{
	struct pci_dev *pdev = doe->pdev;
	u32 val;
	int i;

	/*
	 * Check the DOE busy bit is not set. If it is set, this could indicate
	 * someone other than Linux (e.g. firmware) is using the mailbox. Note
	 * it is expected that firmware and OS will negotiate access rights via
	 * an, as yet to be defined method.
	 */
	pci_read_config_dword(pdev, doe->cap + PCI_DOE_STATUS, &val);
	if (FIELD_GET(PCI_DOE_STATUS_BUSY, val))
		return -EBUSY;

	if (FIELD_GET(PCI_DOE_STATUS_ERROR, val))
		return -EIO;

	/* Write DOE Header */
	val = FIELD_PREP(PCI_DOE_DATA_OBJECT_HEADER_1_VID, ex->vid) |
		FIELD_PREP(PCI_DOE_DATA_OBJECT_HEADER_1_TYPE, ex->protocol);
	pci_write_config_dword(pdev, doe->cap + PCI_DOE_WRITE, val);
	/* Length is 2 DW of header + length of payload in DW */
	pci_write_config_dword(pdev, doe->cap + PCI_DOE_WRITE,
			       FIELD_PREP(PCI_DOE_DATA_OBJECT_HEADER_2_LENGTH,
					  2 + ex->request_pl_sz / sizeof(u32)));
	for (i = 0; i < ex->request_pl_sz / sizeof(u32); i++)
		pci_write_config_dword(pdev, doe->cap + PCI_DOE_WRITE,
				       ex->request_pl[i]);

	pci_write_config_dword(pdev, doe->cap + PCI_DOE_CTRL, PCI_DOE_CTRL_GO);
	/* Request is sent - now wait for poll or IRQ */
	return 0;
}

static int pci_doe_recv_resp(struct pci_doe *doe, struct pci_doe_exchange *ex)
{
	struct pci_dev *pdev = doe->pdev;
	size_t length;
	u32 val;
	int i;

	/* Read the first two dwords to get the length and protocol */
	pci_read_config_dword(pdev, doe->cap + PCI_DOE_READ, &val);
	if ((FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_1_VID, val) != ex->vid) ||
	    (FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_1_TYPE, val) != ex->protocol)) {
		pci_err(pdev,
			"Expected [VID, Protocol] = [%x, %x], got [%x, %x]\n",
			ex->vid, ex->protocol,
			FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_1_VID, val),
			FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_1_TYPE, val));
		return -EIO;
	}

	pci_write_config_dword(pdev, doe->cap + PCI_DOE_READ, 0);
	pci_read_config_dword(pdev, doe->cap + PCI_DOE_READ, &val);
	pci_write_config_dword(pdev, doe->cap + PCI_DOE_READ, 0);

	length = FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_2_LENGTH, val);
	if (length > SZ_1M)
		return -EIO;

	/* Read the rest of the response payload */
	for (i = 0; i < min(length, ex->response_pl_sz / sizeof(u32)); i++) {
		pci_read_config_dword(pdev, doe->cap + PCI_DOE_READ,
				      &ex->response_pl[i]);
		pci_write_config_dword(pdev, doe->cap + PCI_DOE_READ, 0);
	}

	/* Flush excess length */
	for (; i < length; i++) {
		pci_read_config_dword(pdev, doe->cap + PCI_DOE_READ, &val);
		pci_write_config_dword(pdev, doe->cap + PCI_DOE_READ, 0);
	}
	/* Final error check to pick up on any since Data Object Ready */
	pci_read_config_dword(pdev, doe->cap + PCI_DOE_STATUS, &val);
	if (FIELD_GET(PCI_DOE_STATUS_ERROR, val))
		return -EIO;

	return min(length, ex->response_pl_sz / sizeof(u32)) * sizeof(u32);
}

static void pci_doe_task_complete(void *private)
{
	complete(private);
}

/**
 * struct pci_doe_task - description of a query / response task.
 * @h: Head to add it to the list of outstanding tasks.
 * @ex: The details of the task to be done.
 * @rv: Return value.  Length of received response or error.
 * @cb: Callback for completion of task.
 * @private: Private data passed to callback on completion.
 */
struct pci_doe_task {
	struct list_head h;
	struct pci_doe_exchange *ex;
	int rv;
	void (*cb)(void *private);
	void *private;
};

/**
 * pci_doe_exchange_sync() - Send a request, then wait for and receive response.
 * @doe: DOE mailbox state structure.
 * @ex: Description of the buffers and Vendor ID + type used in this
 *      request/response pair,
 *
 * Excess data will be discarded.
 *
 * RETURNS: payload in bytes on success, < 0 on error.
 */
int pci_doe_exchange_sync(struct pci_doe *doe, struct pci_doe_exchange *ex)
{
	struct pci_doe_task task;
	DECLARE_COMPLETION_ONSTACK(c);
	int only_task;

	/* DOE requests must be a whole number of DW */
	if (ex->request_pl_sz % sizeof(u32))
		return -EINVAL;

	task.ex = ex;
	task.cb = pci_doe_task_complete;
	task.private = &c;

	mutex_lock(&doe->tasks_lock);
	if (doe->dead) {
		mutex_unlock(&doe->tasks_lock);
		return -EIO;
	}
	only_task = list_empty(&doe->tasks);
	list_add_tail(&task.h, &doe->tasks);
	if (only_task)
		schedule_delayed_work(&doe->statemachine, 0);
	mutex_unlock(&doe->tasks_lock);
	wait_for_completion(&c);

	return task.rv;
}
EXPORT_SYMBOL_GPL(pci_doe_exchange_sync);

static void doe_statemachine_work(struct work_struct *work)
{
	struct delayed_work *w = to_delayed_work(work);
	struct pci_doe *doe = container_of(w, struct pci_doe, statemachine);
	struct pci_dev *pdev = doe->pdev;
	struct pci_doe_task *task;
	bool abort;
	u32 val;
	int rc;

	mutex_lock(&doe->tasks_lock);
	task = list_first_entry_or_null(&doe->tasks, struct pci_doe_task, h);
	abort = doe->abort;
	doe->abort = false;
	mutex_unlock(&doe->tasks_lock);

	if (abort) {
		/*
		 * Currently only used during init - care needed if we want to
		 * generally expose pci_doe_abort() as it would impact queries
		 * in flight.
		 */
		WARN_ON(task);
		doe->state = DOE_WAIT_ABORT;
		pci_doe_abort_start(doe);
		return;
	}

	switch (doe->state) {
	case DOE_IDLE:
		if (task == NULL)
			return;

		/* Nothing currently in flight so queue a task */
		rc = pci_doe_send_req(doe, task->ex);
		/*
		 * The specification does not provide any guidance on how long
		 * some other entity could keep the DOE busy, so try for 1
		 * second then fail. Busy handling is best effort only, because
		 * there is no way of avoiding racing against another user of
		 * the DOE.
		 */
		if (rc == -EBUSY) {
			doe->busy_retries++;
			if (doe->busy_retries == PCI_DOE_BUSY_MAX_RETRIES) {
				/* Long enough, fail this request */
				doe->busy_retries = 0;
				goto err_busy;
			}
			schedule_delayed_work(w, HZ / PCI_DOE_BUSY_MAX_RETRIES);
			return;
		}
		if (rc)
			goto err_abort;
		doe->busy_retries = 0;

		doe->state = DOE_WAIT_RESP;
		doe->timeout_jiffies = jiffies + HZ;
		/* Now poll or wait for IRQ with timeout */
		if (doe->irq > 0)
			schedule_delayed_work(w, PCI_DOE_TIMEOUT);
		else
			schedule_delayed_work(w, PCI_DOE_POLL_INTERVAL);
		return;

	case DOE_WAIT_RESP:
		/* Not possible to get here with NULL task */
		pci_read_config_dword(pdev, doe->cap + PCI_DOE_STATUS, &val);
		if (FIELD_GET(PCI_DOE_STATUS_ERROR, val)) {
			rc = -EIO;
			goto err_abort;
		}

		if (!FIELD_GET(PCI_DOE_STATUS_DATA_OBJECT_READY, val)) {
			/* If not yet at timeout reschedule otherwise abort */
			if (time_after(jiffies, doe->timeout_jiffies)) {
				rc = -ETIMEDOUT;
				goto err_abort;
			}
			schedule_delayed_work(w, PCI_DOE_POLL_INTERVAL);
			return;
		}

		rc  = pci_doe_recv_resp(doe, task->ex);
		if (rc < 0)
			goto err_abort;

		doe->state = DOE_IDLE;

		mutex_lock(&doe->tasks_lock);
		list_del(&task->h);
		if (!list_empty(&doe->tasks))
			schedule_delayed_work(w, 0);
		mutex_unlock(&doe->tasks_lock);

		/* Set the return value to the length of received payload */
		task->rv = rc;
		task->cb(task->private);
		return;

	case DOE_WAIT_ABORT:
	case DOE_WAIT_ABORT_ON_ERR:
		pci_read_config_dword(pdev, doe->cap + PCI_DOE_STATUS, &val);

		if (!FIELD_GET(PCI_DOE_STATUS_ERROR, val) &&
		    !FIELD_GET(PCI_DOE_STATUS_BUSY, val)) {
			/* Back to normal state - carry on */
			mutex_lock(&doe->tasks_lock);
			if (!list_empty(&doe->tasks))
				schedule_delayed_work(w, 0);
			mutex_unlock(&doe->tasks_lock);

			/*
			 * For deliberately triggered abort, someone is
			 * waiting.
			 */
			if (doe->state == DOE_WAIT_ABORT)
				complete(&doe->abort_c);
			doe->state = DOE_IDLE;

			return;
		}
		if (time_after(jiffies, doe->timeout_jiffies)) {
			struct pci_doe_task *t, *n;

			/* We are dead - abort all queued tasks */
			pci_err(pdev, "DOE ABORT timed out\n");
			mutex_lock(&doe->tasks_lock);
			doe->dead = true;
			list_for_each_entry_safe(t, n, &doe->tasks, h) {
				t->rv = -EIO;
				t->cb(t->private);
				list_del(&t->h);
			}

			mutex_unlock(&doe->tasks_lock);
			if (doe->state == DOE_WAIT_ABORT)
				complete(&doe->abort_c);
		}
		return;
	}

err_abort:
	pci_doe_abort_start(doe);
	doe->state = DOE_WAIT_ABORT_ON_ERR;
err_busy:
	mutex_lock(&doe->tasks_lock);
	list_del(&task->h);
	mutex_unlock(&doe->tasks_lock);

	task->rv = rc;
	task->cb(task->private);
	/*
	 * If we got here via err_busy, and the queue isn't empty then we need
	 * to go again.
	 */
	if (doe->state == DOE_IDLE) {
		mutex_lock(&doe->tasks_lock);
		if (!list_empty(&doe->tasks))
			schedule_delayed_work(w, 0);
		mutex_unlock(&doe->tasks_lock);
	}
}

static int pci_doe_discovery(struct pci_doe *doe, u8 *index, u16 *vid,
			     u8 *protocol)
{
	u32 request_pl = FIELD_PREP(PCI_DOE_DATA_OBJECT_DISC_REQ_3_INDEX, *index);
	u32 response_pl;
	struct pci_doe_exchange ex = {
		.vid = PCI_VENDOR_ID_PCI_SIG,
		.protocol = PCI_DOE_PROTOCOL_DISCOVERY,
		.request_pl = &request_pl,
		.request_pl_sz = sizeof(request_pl),
		.response_pl = &response_pl,
		.response_pl_sz = sizeof(response_pl),
	};
	int ret;

	ret = pci_doe_exchange_sync(doe, &ex);
	if (ret < 0)
		return ret;

	if (ret != sizeof(response_pl))
		return -EIO;

	*vid = FIELD_GET(PCI_DOE_DATA_OBJECT_DISC_RSP_3_VID, response_pl);
	*protocol = FIELD_GET(PCI_DOE_DATA_OBJECT_DISC_RSP_3_PROTOCOL, response_pl);
	*index = FIELD_GET(PCI_DOE_DATA_OBJECT_DISC_RSP_3_NEXT_INDEX, response_pl);

	return 0;
}

static int pci_doe_cache_protocols(struct pci_doe *doe)
{
	u8 index = 0;
	int rc;

	/* Discovery protocol must always be supported and must report itself */
	doe->num_prots = 1;
	doe->prots = kzalloc(sizeof(*doe->prots) * doe->num_prots, GFP_KERNEL);
	if (doe->prots == NULL)
		return -ENOMEM;

	do {
		struct pci_doe_prot *prot, *prot_new;

		prot = &doe->prots[doe->num_prots - 1];
		rc = pci_doe_discovery(doe, &index, &prot->vid, &prot->type);
		if (rc)
			goto err_free_prots;

		if (index) {
			prot_new = krealloc(doe->prots,
					    sizeof(*doe->prots) * doe->num_prots,
					    GFP_KERNEL);
			if (prot_new == NULL) {
				rc = -ENOMEM;
				goto err_free_prots;
			}
			doe->prots = prot_new;
			doe->num_prots++;
		}
	} while (index);

	return 0;

err_free_prots:
	kfree(doe->prots);
	return rc;
}

static void pci_doe_init(struct pci_doe *doe, struct pci_dev *pdev, int doe_offset)
{
	mutex_init(&doe->tasks_lock);
	init_completion(&doe->abort_c);
	doe->cap = doe_offset;
	doe->pdev = pdev;
	INIT_LIST_HEAD(&doe->tasks);
	INIT_DELAYED_WORK(&doe->statemachine, doe_statemachine_work);
}

static int pci_doe_abort(struct pci_doe *doe)
{
	reinit_completion(&doe->abort_c);
	mutex_lock(&doe->tasks_lock);
	doe->abort = true;
	mutex_unlock(&doe->tasks_lock);
	schedule_delayed_work(&doe->statemachine, 0);
	wait_for_completion(&doe->abort_c);

	if (doe->dead)
		return -EIO;

	return 0;
}

static int pci_doe_register(struct pci_doe *doe)
{
	struct pci_dev *pdev = doe->pdev;
	bool poll = !pci_dev_msi_enabled(pdev);
	int rc, irq;
	u32 val;

	pci_read_config_dword(pdev, doe->cap + PCI_DOE_CAP, &val);

	if (!poll && FIELD_GET(PCI_DOE_CAP_INT, val)) {
		irq = pci_irq_vector(pdev, FIELD_GET(PCI_DOE_CAP_IRQ, val));
		if (irq < 0)
			return irq;

		rc = request_irq(irq, pci_doe_irq, 0, "DOE", doe);
		if (rc)
			return rc;

		doe->irq = irq;
		pci_write_config_dword(pdev, doe->cap + PCI_DOE_CTRL,
				       FIELD_PREP(PCI_DOE_CTRL_INT_EN, 1));
	}

	/* Reset the mailbox by issuing an abort */
	rc = pci_doe_abort(doe);
	if (rc)
		goto err_free_irqs;

	return 0;

err_free_irqs:
	if (doe->irq > 0)
		free_irq(doe->irq, doe);

	return rc;
}

static void pci_doe_unregister(struct pci_doe *doe)
{
	if (doe->irq > 0)
		free_irq(doe->irq, doe);
}

void pci_doe_unregister_all(struct pci_dev *pdev)
{
	struct pci_doe *doe, *next;

	list_for_each_entry_safe(doe, next, &pdev->doe_list, h) {
		/* First halt the state machine */
		cancel_delayed_work_sync(&doe->statemachine);
		kfree(doe->prots);
		pci_doe_unregister(doe);
		kfree(doe);
	}
}
EXPORT_SYMBOL_GPL(pci_doe_unregister_all);

/**
 * pci_doe_register_all() - Find and register all DOE mailboxes.
 * @pdev: PCI device whose DOE mailboxes we are finding.
 *
 * Will locate any DOE mailboxes present on the device and cache the protocols
 * so that pci_doe_find() can be used to retrieve a suitable DOE instance.
 *
 * DOE mailboxes are available until pci_doe_unregister_all() is called.
 *
 * RETURNS: 0 on success, < 0 on error.
 */
int pci_doe_register_all(struct pci_dev *pdev)
{
	struct pci_doe *doe;
	int pos = 0;
	int rc;

	INIT_LIST_HEAD(&pdev->doe_list);

	/* Walk the DOE extended capabilities and add to per pci_dev list */
	while (true) {
		pos = pci_find_next_ext_capability(pdev, pos,
						   PCI_EXT_CAP_ID_DOE);
		if (!pos)
			return 0;

		doe = kzalloc(sizeof(*doe), GFP_KERNEL);
		if (!doe) {
			rc = -ENOMEM;
			goto err_free_does;
		}

		pci_doe_init(doe, pdev, pos);
		rc = pci_doe_register(doe);
		if (rc) {
			kfree(doe);
			goto err_free_does;
		}

		rc = pci_doe_cache_protocols(doe);
		if (rc) {
			pci_doe_unregister(doe);
			kfree(doe);
			goto err_free_does;
		}

		list_add(&doe->h, &pdev->doe_list);
	}

err_free_does:
	pci_doe_unregister_all(pdev);

	return rc;
}
EXPORT_SYMBOL_GPL(pci_doe_register_all);

/**
 * pci_doe_find() - Find the first DOE instance that supports a given protocol.
 * @pdev: Device on which to find the DOE instance.
 * @vid: Vendor ID.
 * @type: Specific protocol for this vendor.
 *
 * RETURNS: Pointer to DOE instance on success, NULL on no suitable instance
 * available.
 */
struct pci_doe *pci_doe_find(struct pci_dev *pdev, u16 vid, u8 type)
{
	struct pci_doe *doe;
	int i;

	list_for_each_entry(doe, &pdev->doe_list, h) {
		for (i = 0; i < doe->num_prots; i++)
			if ((doe->prots[i].vid == vid) &&
			    (doe->prots[i].type == type))
				return doe;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(pci_doe_find);
