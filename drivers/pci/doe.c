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
#include <uapi/linux/pci_doe.h>

/* Maximum number of DOE instances in the system */
#define PCI_DOE_MAX_CNT 65536

#define PCI_DOE_PROTOCOL_DISCOVERY 0

#define PCI_DOE_BUSY_MAX_RETRIES 16
#define PCI_DOE_POLL_INTERVAL (HZ / 128)

/* Timeout of 1 second from 6.xx.1 (Operation), ECN - Data Object Exchange */
#define PCI_DOE_TIMEOUT HZ

static int pci_doe_major;
static DEFINE_IDA(pci_doe_ida);
static DECLARE_RWSEM(pci_doe_rwsem);

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

	/*
	 * Busy being cleared can result in an interrupt, but as
	 * the original Busy may not have been detected, there is no
	 * way to separate such an interrupt from a spurious interrupt.
	 */
	return IRQ_HANDLED;
}

/*
 * Only call when safe to directly access the DOE, either because no tasks yet
 * queued, or called from doe_statemachine_work() which has exclusive access to
 * the DOE config space.
 */
static void pci_doe_abort_start(struct pci_doe *doe)
{
	struct pci_dev *pdev = doe->pdev;
	u32 val;

	val = PCI_DOE_CTRL_ABORT;
	if (doe->irq)
		val |= PCI_DOE_CTRL_INT_EN;
	pci_write_config_dword(pdev, doe->cap + PCI_DOE_CTRL, val);

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

	val = PCI_DOE_CTRL_GO;
	if (doe->irq)
		val |= PCI_DOE_CTRL_INT_EN;

	pci_write_config_dword(pdev, doe->cap + PCI_DOE_CTRL, val);
	/* Request is sent - now wait for poll or IRQ */
	return 0;
}

static int pci_doe_recv_resp(struct pci_doe *doe, struct pci_doe_exchange *ex)
{
	struct pci_dev *pdev = doe->pdev;
	size_t length;
	u32 val;
	int i;

	/* Read the first dword to get the protocol */
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
	/* Read the second dword to get the length */
	pci_read_config_dword(pdev, doe->cap + PCI_DOE_READ, &val);
	pci_write_config_dword(pdev, doe->cap + PCI_DOE_READ, 0);

	length = FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_2_LENGTH, val);
	if (length > SZ_1M || length < 2)
		return -EIO;

	/* First 2 dwords have already been read */
	length -= 2;
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
 * struct pci_doe_task - description of a query / response task
 * @h: Head to add it to the list of outstanding tasks
 * @ex: The details of the task to be done
 * @rv: Return value.  Length of received response or error
 * @cb: Callback for completion of task
 * @private: Private data passed to callback on completion
 */
struct pci_doe_task {
	struct list_head h;
	struct pci_doe_exchange *ex;
	int rv;
	void (*cb)(void *private);
	void *private;
};

/**
 * pci_doe_exchange_sync() - Send a request, then wait for and receive response
 * @doe: DOE mailbox state structure
 * @ex: Description of the buffers and Vendor ID + type used in this
 *      request/response pair
 *
 * Excess data will be discarded.
 *
 * RETURNS: payload in bytes on success, < 0 on error
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
				pci_WARN(pdev, true, "DOE busy for too long\n");
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
	doe->state = DOE_WAIT_ABORT_ON_ERR;
	pci_doe_abort_start(doe);
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

static void pci_doe_init(struct pci_doe *doe, struct pci_dev *pdev,
			 int doe_offset)
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

static void pci_doe_release(struct device *dev)
{
	struct pci_doe *doe = container_of(dev, struct pci_doe, dev);

	ida_free(&pci_doe_ida, MINOR(doe->dev.devt));
	kfree(doe);
}

static char *pci_doe_devnode(struct device *dev, umode_t *mode, kuid_t *uid,
			     kgid_t *gid)
{
	return kasprintf(GFP_KERNEL, "pcidoe/%s", dev_name(dev));
}

static const struct device_type pci_doe_type = {
	.name = "pci_doe",
	.release = pci_doe_release,
	.devnode = pci_doe_devnode,
};

static long __pci_doe_ioctl(struct pci_doe *doe, unsigned int cmd,
			    unsigned long arg)
{
	struct pci_doe_uexchange __user *uex;
	struct pci_doe_uexchange ex;
	struct pci_doe_exchange exchange;
	u32 *request_pl;
	u32 *response_pl;
	int ret;

	if (cmd != PCI_DOE_EXCHANGE)
		return -ENOTTY;

	uex = (void __user *)arg;
	if (copy_from_user(&ex, uex, sizeof(ex)))
		return -EFAULT;

	/* Cap size at something sensible */
	request_pl = vmemdup_user(u64_to_user_ptr(ex.in.payload), ex.in.size);
	if (!request_pl)
		return -ENOMEM;

	response_pl = kvzalloc(ex.out.size, GFP_KERNEL);
	if (!response_pl) {
		ret = -ENOMEM;
		goto free_request;
	}

	exchange.vid = ex.vid;
	exchange.protocol = ex.protocol;
	exchange.request_pl = request_pl;
	exchange.request_pl_sz = ex.in.size;
	exchange.response_pl = response_pl;
	exchange.response_pl_sz = ex.out.size;
	ret = pci_doe_exchange_sync(doe, &exchange);
	if (ret < 0)
		goto free_response;
	ret = 0;

	if (copy_to_user(u64_to_user_ptr(ex.out.payload), response_pl, ex.out.size)) {
		ret = -EFAULT;
		goto free_response;
	}

	/* No useful value to return currently */
	ex.retval = 0;
	if (copy_to_user(uex, &ex, sizeof(ex))) {
		ret = -EFAULT;
		goto free_response;
	}

free_response:
	kvfree(response_pl);
free_request:
	kvfree(request_pl);

	return ret;
}

static long pci_doe_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct pci_doe *doe = file->private_data;
	int rc = -ENXIO;

	down_read(&pci_doe_rwsem);
	if (!doe->going_down)
		rc = __pci_doe_ioctl(doe, cmd, arg);
	up_read(&pci_doe_rwsem);

	return rc;
}

static int pci_doe_open(struct inode *inode, struct file *file)
{
	struct pci_doe *doe = container_of(inode->i_cdev, typeof(*doe), cdev);

	get_device(&doe->dev);
	file->private_data = doe;

	return 0;
}

static int pci_doe_file_release(struct inode *inode, struct file *file)
{
	struct pci_doe *doe = container_of(inode->i_cdev, typeof(*doe), cdev);

	put_device(&doe->dev);

	return 0;
}
static const struct file_operations pci_doe_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = pci_doe_ioctl,
	.open = pci_doe_open,
	.release = pci_doe_file_release,
	.compat_ioctl = compat_ptr_ioctl,
	.llseek = noop_llseek,
};

static int pci_doe_register(struct pci_doe *doe)
{
	struct pci_dev *pdev = doe->pdev;
	bool poll = !pci_dev_msi_enabled(pdev);
	int rc, irq;
	u32 val;

	rc = ida_alloc_range(&pci_doe_ida, 0, PCI_DOE_MAX_CNT - 1, GFP_KERNEL);
	if (rc < 0)
		return rc;

	device_initialize(&doe->dev);
	doe->dev.parent = &pdev->dev;
	doe->dev.devt = MKDEV(pci_doe_major, rc);
	doe->dev.type = &pci_doe_type;
	device_set_pm_not_required(&doe->dev);
	rc = dev_set_name(&doe->dev, "doe[%s]_%x", dev_name(&pdev->dev), doe->cap);
	if (rc)
		goto err_put_device;

	cdev_init(&doe->cdev, &pci_doe_fops);

	pci_read_config_dword(pdev, doe->cap + PCI_DOE_CAP, &val);

	if (!poll && FIELD_GET(PCI_DOE_CAP_INT, val)) {
		rc = pci_irq_vector(pdev, FIELD_GET(PCI_DOE_CAP_IRQ, val));
		if (rc < 0)
			goto err_put_device;
		irq = rc;

		doe->irq_name = kasprintf(GFP_KERNEL, "DOE[%s]_%x",
					  dev_name(&pdev->dev), doe->cap);
		if (!doe->irq_name) {
			rc = -ENOMEM;
			goto err_put_device;
		}

		rc = request_irq(irq, pci_doe_irq, 0, doe->irq_name, doe);
		if (rc)
			goto err_free_name;

		doe->irq = irq;
		pci_write_config_dword(pdev, doe->cap + PCI_DOE_CTRL,
				       PCI_DOE_CTRL_INT_EN);
	}

	/* Reset the mailbox by issuing an abort */
	rc = pci_doe_abort(doe);
	if (rc)
		goto err_free_irqs;

	rc = cdev_device_add(&doe->cdev, &doe->dev);
	if (rc)
		goto err_free_irqs;

	return 0;

err_free_irqs:
	if (doe->irq > 0)
		free_irq(doe->irq, doe);
err_free_name:
	kfree(doe->irq_name);
err_put_device:
	put_device(&doe->dev);

	return rc;
}

static void pci_doe_unregister(struct pci_doe *doe)
{
	cdev_device_del(&doe->cdev, &doe->dev);
	down_write(&pci_doe_rwsem);
	doe->going_down = 1;
	up_write(&pci_doe_rwsem);
	if (doe->irq > 0)
		free_irq(doe->irq, doe);
	kfree(doe->irq_name);
	put_device(&doe->dev);
}

void pci_doe_unregister_all(struct pci_dev *pdev)
{
	struct pci_doe *doe, *next;

	list_for_each_entry_safe(doe, next, &pdev->doe_list, h) {
		/* First halt the state machine */
		cancel_delayed_work_sync(&doe->statemachine);
		kfree(doe->prots);
		pci_doe_unregister(doe);
	}
}
EXPORT_SYMBOL_GPL(pci_doe_unregister_all);

/**
 * pci_doe_register_all() - Find and register all DOE mailboxes
 * @pdev: PCI device whose DOE mailboxes we are finding
 *
 * Will locate any DOE mailboxes present on the device and cache the protocols
 * so that pci_doe_find() can be used to retrieve a suitable DOE instance.
 *
 * DOE mailboxes are available until pci_doe_unregister_all() is called.
 *
 * RETURNS: 0 on success, < 0 on error
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
		if (rc)
			goto err_free_does;

		rc = pci_doe_cache_protocols(doe);
		if (rc) {
			pci_doe_unregister(doe);
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
 * pci_doe_find() - Find the first DOE instance that supports a given protocol
 * @pdev: Device on which to find the DOE instance
 * @vid: Vendor ID
 * @type: Specific protocol for this vendor
 *
 * RETURNS: Pointer to DOE instance on success, NULL on no suitable instance
 * available
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

int pci_doe_sys_init(void)
{
	dev_t devt;
	int rc;

	rc = alloc_chrdev_region(&devt, 0, PCI_DOE_MAX_CNT, "pcidoe");
	if (rc)
		return rc;
	pci_doe_major = MAJOR(devt);

	return 0;
}
EXPORT_SYMBOL_GPL(pci_doe_sys_init);
