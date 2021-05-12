// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/dev_printk.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include "device.h"
#include "nnp_elbi.h"
#include "nnp_boot_defs.h"

/*
 * SpringHill PCI card identity settings
 */
#define NNP_PCI_DEVICE_ID		0x45c6

/**
 * struct nnp_pci - structure for NNP-I PCIe device info.
 * @nnpdev: the NNP-I framework's structure for this NNP-I card device
 * @pdev: pointer to the pcie device struct
 * @mmio_va: device's BAR0 mapped virtual address
 * @mem_bar_va: device's BAR2 mapped virtual address, this is the
 *              "inbound memory region". This device memory region is
 *              described in ipc_include/nnp_inbound_mem.h
 * @lock: protects accesses to cmd_read_update_count members.
 * @response_buf: buffer to hold response messages pulled of the device's
 *                response queue.
 * @card_status_wait: waitq that get signaled when device PCI status has changed
 *                    or device has updated its read pointer of the command
 *                    queue.
 * @card_doorbell_val: card's doorbell register value, updated when doorbell
 *                     interrupt is received.
 * @cmdq_free_slots: number of slots in the device's command queue which is known
 *                   to be available.
 * @cmdq_lock: protects @cmdq_free_slots calculation.
 * @card_status: Last device interrupt status register, updated in interrupt
 *               handler.
 * @cmd_read_update_count: number of times the device has updated its read
 *                         pointer to the device command queue.
 * @removing: true if device remove is in progress.
 */
struct nnp_pci {
	struct nnp_device nnpdev;
	struct pci_dev    *pdev;

	void __iomem      *mmio_va;
	void __iomem      *mem_bar_va;

	spinlock_t      lock;
	u64             response_buf[ELBI_RESPONSE_FIFO_DEPTH];
	wait_queue_head_t card_status_wait;
	u32             card_doorbell_val;

	u32             cmdq_free_slots;
	spinlock_t      cmdq_lock;

	u32             card_status;
	u32             cmd_read_update_count;
	bool            removing;
};

#define NNP_DRIVER_NAME  "nnp_pcie"

/* interrupt mask bits we enable and handle at interrupt level */
static u32 card_status_int_mask = ELBI_PCI_STATUS_CMDQ_READ_UPDATE |
				  ELBI_PCI_STATUS_RESPQ_NEW_RESPONSE |
				  ELBI_PCI_STATUS_DOORBELL;

static inline void nnp_mmio_write(struct nnp_pci *nnp_pci, u32 off, u32 val)
{
	iowrite32(val, nnp_pci->mmio_va + off);
}

static inline u32 nnp_mmio_read(struct nnp_pci *nnp_pci, u32 off)
{
	return ioread32(nnp_pci->mmio_va + off);
}

static inline void nnp_mmio_write_8b(struct nnp_pci *nnp_pci, u32 off, u64 val)
{
	lo_hi_writeq(val, nnp_pci->mmio_va + off);
}

static inline u64 nnp_mmio_read_8b(struct nnp_pci *nnp_pci, u32 off)
{
	return lo_hi_readq(nnp_pci->mmio_va + off);
}

static void nnp_process_commands(struct nnp_pci *nnp_pci)
{
	u32 response_pci_control;
	u32 read_pointer;
	u32 write_pointer;
	u32 avail_slots;
	int i;

	response_pci_control = nnp_mmio_read(nnp_pci, ELBI_RESPONSE_PCI_CONTROL);
	read_pointer = FIELD_GET(RESPQ_READ_PTR_MASK, response_pci_control);
	write_pointer = FIELD_GET(RESPQ_WRITE_PTR_MASK, response_pci_control);
	if (read_pointer > write_pointer) {
		/* This should never happen on proper device hardware */
		dev_err(&nnp_pci->pdev->dev, "Mismatched read and write pointers\n");
		/*
		 * For now just ignore it. Implement handling for such fatal
		 * device errors on a later patch
		 */
		return;
	}

	/* Commands to read */
	avail_slots = write_pointer - read_pointer;

	if (!avail_slots)
		return;

	for (i = 0; i < avail_slots; i++) {
		read_pointer = (read_pointer + 1) % ELBI_RESPONSE_FIFO_DEPTH;

		nnp_pci->response_buf[i] =
			nnp_mmio_read_8b(nnp_pci,
					 ELBI_RESPONSE_FIFO_LOW(read_pointer));
	}

	/*
	 * HW restriction - we cannot update the read pointer with the same
	 * value it currently have. This will be the case if we need to advance
	 * it by FIFO_DEPTH locations. In this case we will update it in two
	 * steps, first advance by 1, then to the proper value.
	 */
	if (avail_slots == ELBI_COMMAND_FIFO_DEPTH) {
		u32 next_read_pointer =
			(read_pointer + 1) % ELBI_RESPONSE_FIFO_DEPTH;

		response_pci_control &= ~RESPQ_READ_PTR_MASK;
		response_pci_control |= FIELD_PREP(RESPQ_READ_PTR_MASK,
						   next_read_pointer);
		nnp_mmio_write(nnp_pci, ELBI_RESPONSE_PCI_CONTROL,
			       response_pci_control);
	}

	response_pci_control &= ~RESPQ_READ_PTR_MASK;
	response_pci_control |= FIELD_PREP(RESPQ_READ_PTR_MASK, read_pointer);
	nnp_mmio_write(nnp_pci, ELBI_RESPONSE_PCI_CONTROL,
		       response_pci_control);
}

static void mask_all_interrupts(struct nnp_pci *nnp_pci)
{
	nnp_mmio_write(nnp_pci, ELBI_PCI_MSI_MASK, GENMASK(31, 0));
}

static void unmask_interrupts(struct nnp_pci *nnp_pci)
{
	nnp_mmio_write(nnp_pci, ELBI_PCI_MSI_MASK, ~card_status_int_mask);
}

static void notify_card_doorbell_value(struct nnp_pci *nnp_pci)
{
	nnp_pci->card_doorbell_val = nnp_mmio_read(nnp_pci, CARD_DOORBELL_REG);
	nnpdev_card_doorbell_value_changed(&nnp_pci->nnpdev,
					   nnp_pci->card_doorbell_val);
}

static irqreturn_t threaded_interrupt_handler(int irq, void *data)
{
	struct nnp_pci *nnp_pci = data;
	bool should_wake = false;

	mask_all_interrupts(nnp_pci);

	nnp_pci->card_status = nnp_mmio_read(nnp_pci, ELBI_PCI_STATUS);

	nnp_mmio_write(nnp_pci, ELBI_PCI_STATUS,
		       nnp_pci->card_status & card_status_int_mask);

	if (nnp_pci->card_status & ELBI_PCI_STATUS_CMDQ_READ_UPDATE) {
		spin_lock(&nnp_pci->lock);
		should_wake = true;
		nnp_pci->cmd_read_update_count++;
		spin_unlock(&nnp_pci->lock);
	}

	if (nnp_pci->card_status &
	    ELBI_PCI_STATUS_DOORBELL)
		notify_card_doorbell_value(nnp_pci);

	if (nnp_pci->card_status & ELBI_PCI_STATUS_RESPQ_NEW_RESPONSE)
		nnp_process_commands(nnp_pci);

	unmask_interrupts(nnp_pci);

	if (should_wake)
		wake_up_all(&nnp_pci->card_status_wait);

	return IRQ_HANDLED;
}

static int nnp_setup_interrupts(struct nnp_pci *nnp_pci, struct pci_dev *pdev)
{
	int rc;
	int irq;

	mask_all_interrupts(nnp_pci);

	rc = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (rc < 1)
		return rc;

	irq = pci_irq_vector(pdev, 0);

	rc = devm_request_threaded_irq(&pdev->dev, irq, NULL,
				       threaded_interrupt_handler, IRQF_ONESHOT,
				       "nnpi-msi", nnp_pci);
	if (rc)
		goto err_irq_req_fail;

	return 0;

err_irq_req_fail:
	pci_free_irq_vectors(pdev);
	return rc;
}

static void nnp_free_interrupts(struct nnp_pci *nnp_pci, struct pci_dev *pdev)
{
	mask_all_interrupts(nnp_pci);
	devm_free_irq(&pdev->dev, pci_irq_vector(pdev, 0), nnp_pci);
	pci_free_irq_vectors(pdev);
}

/**
 * nnp_cmdq_write_mesg_nowait() - tries to write full message to command queue
 * @nnp_pci: the device
 * @msg: pointer to the command message
 * @size: size of the command message in qwords
 * @read_update_count: returns current cmd_read_update_count value,
 *                     valid only if function returns -EAGAIN.
 *
 * Return:
 * * 0: Success, command has been written
 * * -EAGAIN: command queue does not have room for the entire command
 *            message.
 *            read_update_count returns the current value of
 *            cmd_read_update_count counter which increments when the device
 *            advance its command queue read pointer. The caller may wait
 *            for this counter to be advanced past this point before calling
 *            this function again to re-try the write.
 * * -ENODEV: device remove is in progress.
 */
static int nnp_cmdq_write_mesg_nowait(struct nnp_pci *nnp_pci, u64 *msg,
				      u32 size, u32 *read_update_count)
{
	u32 cmd_iosf_control;
	u32 read_pointer, write_pointer;
	int i;

	if (nnp_pci->removing)
		return -ENODEV;

	if (!size)
		return 0;

	spin_lock(&nnp_pci->cmdq_lock);

	if (nnp_pci->cmdq_free_slots < size) {
		/* read command fifo pointers and compute free slots in fifo */
		spin_lock(&nnp_pci->lock);
		cmd_iosf_control = nnp_mmio_read(nnp_pci, ELBI_COMMAND_IOSF_CONTROL);
		read_pointer = FIELD_GET(CMDQ_READ_PTR_MASK, cmd_iosf_control);
		write_pointer =
			FIELD_GET(CMDQ_WRITE_PTR_MASK, cmd_iosf_control);

		nnp_pci->cmdq_free_slots = ELBI_COMMAND_FIFO_DEPTH -
					   (write_pointer - read_pointer);

		if (nnp_pci->cmdq_free_slots < size) {
			*read_update_count = nnp_pci->cmd_read_update_count;
			spin_unlock(&nnp_pci->lock);
			spin_unlock(&nnp_pci->cmdq_lock);
			return -EAGAIN;
		}
		spin_unlock(&nnp_pci->lock);
	}

	/* Write all but the last qword without generating msi on card */
	for (i = 0; i < size - 1; i++)
		nnp_mmio_write_8b(nnp_pci, ELBI_COMMAND_WRITE_WO_MSI_LOW, msg[i]);

	/* Write last qword with generating interrupt on card */
	nnp_mmio_write_8b(nnp_pci, ELBI_COMMAND_WRITE_W_MSI_LOW, msg[i]);

	nnp_pci->cmdq_free_slots -= size;

	spin_unlock(&nnp_pci->cmdq_lock);

	return 0;
}

/**
 * check_read_count() - check if device has read commands from command FIFO
 * @nnp_pci: the device
 * @count: last known 'cmd_read_update_count' value
 *
 * cmd_read_update_count is advanced on each interrupt received because the
 * device has advanced its read pointer into the command FIFO.
 * This function checks the current cmd_read_update_count against @count and
 * returns true if it is different. This is used to check if the device has
 * freed some entries in the command FIFO after it became full.
 *
 * Return: true if current device read update count has been advanced
 */
static bool check_read_count(struct nnp_pci *nnp_pci, u32 count)
{
	bool ret;

	spin_lock(&nnp_pci->lock);
	ret = (count != nnp_pci->cmd_read_update_count);
	spin_unlock(&nnp_pci->lock);

	return ret;
}

/**
 * nnp_cmdq_write_mesg() - writes a command message to device's command queue
 * @nnpdev: the device handle
 * @msg: The command message to write
 * @size: size of the command message in qwords
 *
 * Return:
 * * 0: Success, command has been written
 * * -ENODEV: device remove is in progress.
 */
static int nnp_cmdq_write_mesg(struct nnp_device *nnpdev, u64 *msg, u32 size)
{
	int rc;
	u32 rcnt = 0;
	struct nnp_pci *nnp_pci = container_of(nnpdev, struct nnp_pci, nnpdev);

	do {
		rc = nnp_cmdq_write_mesg_nowait(nnp_pci, msg, size, &rcnt);
		if (rc != -EAGAIN)
			break;

		rc = wait_event_interruptible(nnp_pci->card_status_wait,
					      check_read_count(nnp_pci, rcnt) ||
					      nnp_pci->removing);
		if (!rc && nnp_pci->removing) {
			rc = -ENODEV;
			break;
		}
	} while (!rc);

	if (rc)
		dev_dbg(&nnp_pci->pdev->dev,
			"Failed to write message size %u rc=%d!\n", size, rc);

	return rc;
}

static int nnp_cmdq_flush(struct nnp_device *nnpdev)
{
	struct nnp_pci *nnp_pci = container_of(nnpdev, struct nnp_pci, nnpdev);

	nnp_mmio_write(nnp_pci, ELBI_COMMAND_PCI_CONTROL,
		       ELBI_COMMAND_PCI_CONTROL_FLUSH_MASK);

	return 0;
}

static int nnp_set_host_doorbell_value(struct nnp_device *nnpdev, u32 value)
{
	struct nnp_pci *nnp_pci = container_of(nnpdev, struct nnp_pci, nnpdev);

	/*
	 * The SELF_RESET bit is set only by the h/w layer,
	 * do not allow higher layer to set it
	 */
	value &= ~NNP_HOST_DRV_REQUEST_SELF_RESET_MASK;

	nnp_mmio_write(nnp_pci, ELBI_PCI_HOST_DOORBELL_VALUE, value);

	return 0;
}

static struct nnp_device_ops nnp_device_ops = {
	.cmdq_flush = nnp_cmdq_flush,
	.cmdq_write_mesg = nnp_cmdq_write_mesg,
	.set_host_doorbell_value = nnp_set_host_doorbell_value,
};

static void set_host_boot_state(struct nnp_pci *nnp_pci, int boot_state)
{
	u32 doorbell_val = 0;

	if (boot_state != NNP_HOST_BOOT_STATE_NOT_READY) {
		doorbell_val = nnp_mmio_read(nnp_pci, HOST_DOORBELL_REG);
		doorbell_val &= ~NNP_HOST_BOOT_STATE_MASK;
		doorbell_val |= FIELD_PREP(NNP_HOST_BOOT_STATE_MASK, boot_state);
	}

	nnp_mmio_write(nnp_pci, HOST_DOORBELL_REG, doorbell_val);
}

static int nnp_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct nnp_pci *nnp_pci;
	u32 status;
	int rc;

	nnp_pci = devm_kzalloc(dev, sizeof(*nnp_pci), GFP_KERNEL);
	if (!nnp_pci)
		return -ENOMEM;

	nnp_pci->pdev = pdev;
	pci_set_drvdata(pdev, nnp_pci);

	init_waitqueue_head(&nnp_pci->card_status_wait);
	spin_lock_init(&nnp_pci->lock);
	spin_lock_init(&nnp_pci->cmdq_lock);

	rc = pcim_enable_device(pdev);
	if (rc)
		return dev_err_probe(dev, rc, "enable_device\n");

	pci_set_master(pdev);

	rc = pcim_iomap_regions(pdev, BIT(0) | BIT(2), NNP_DRIVER_NAME);
	if (rc)
		return dev_err_probe(dev, rc, "iomap_regions\n");

	nnp_pci->mmio_va = pcim_iomap_table(pdev)[0];
	nnp_pci->mem_bar_va = pcim_iomap_table(pdev)[2];

	rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (rc)
		return dev_err_probe(dev, rc, "dma_set_mask\n");

	rc = nnp_setup_interrupts(nnp_pci, pdev);
	if (rc)
		return dev_err_probe(dev, rc, "nnp_setup_interrupts\n");

	/*
	 * done setting up the new pci device,
	 * add it to the NNP-I framework.
	 */
	rc = nnpdev_init(&nnp_pci->nnpdev, dev, &nnp_device_ops);
	if (rc)
		return dev_err_probe(dev, rc, "nnpdev_init\n");

	/* notify bios that host driver is up */
	nnp_cmdq_flush(&nnp_pci->nnpdev);
	set_host_boot_state(nnp_pci, NNP_HOST_BOOT_STATE_DRV_READY);

	/* Update NNP-I framework with current value of card doorbell value */
	notify_card_doorbell_value(nnp_pci);
	status = nnp_mmio_read(nnp_pci, ELBI_PCI_STATUS);
	if (status & ELBI_PCI_STATUS_DOORBELL)
		nnp_mmio_write(nnp_pci, ELBI_PCI_STATUS, ELBI_PCI_STATUS_DOORBELL);

	/* process any existing command in the response queue */
	nnp_process_commands(nnp_pci);

	/* Enable desired interrupts */
	unmask_interrupts(nnp_pci);

	return 0;
}

static void nnp_remove(struct pci_dev *pdev)
{
	struct nnp_pci *nnp_pci = pci_get_drvdata(pdev);

	/* stop service new interrupts */
	nnp_free_interrupts(nnp_pci, nnp_pci->pdev);

	/*
	 * Flag that the device is being removed and wake any possible
	 * thread waiting on the card's command queue.
	 * During the remove flow, we want to immediately fail any thread
	 * that is using the device without waiting for pending device
	 * requests to complete. We rather give precedance to device
	 * removal over waiting for all pending requests to finish.
	 * When we set the host boot state to "NOT_READY" in the doorbell
	 * register, the card will cleanup any state, so this "hard remove"
	 * is not an issue for next time the device will get inserted.
	 */
	nnp_pci->removing = true;
	wake_up_all(&nnp_pci->card_status_wait);

	/*
	 * Inform card that host driver is down.
	 * This will also clear any state on the card so that
	 * if card is inserted again, it will be in a good, clear
	 * state.
	 */
	set_host_boot_state(nnp_pci, NNP_HOST_BOOT_STATE_NOT_READY);

	nnpdev_destroy(&nnp_pci->nnpdev);
}

static const struct pci_device_id nnp_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, NNP_PCI_DEVICE_ID) },
	{ }
};

static struct pci_driver nnp_driver = {
	.name = NNP_DRIVER_NAME,
	.id_table = nnp_pci_tbl,
	.probe = nnp_probe,
	.remove = nnp_remove,
};

module_pci_driver(nnp_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel(R) NNP-I PCIe driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_DEVICE_TABLE(pci, nnp_pci_tbl);
