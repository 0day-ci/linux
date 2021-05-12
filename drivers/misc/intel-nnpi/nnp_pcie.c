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
 * @card_status: Last device interrupt status register, updated in interrupt
 *               handler.
 * @cmd_read_update_count: number of times the device has updated its read
 *                         pointer to the device command queue.
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

	u32             card_status;
	u32             cmd_read_update_count;
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

static int nnp_cmdq_flush(struct nnp_device *nnpdev)
{
	struct nnp_pci *nnp_pci = container_of(nnpdev, struct nnp_pci, nnpdev);

	nnp_mmio_write(nnp_pci, ELBI_COMMAND_PCI_CONTROL,
		       ELBI_COMMAND_PCI_CONTROL_FLUSH_MASK);

	return 0;
}

static struct nnp_device_ops nnp_device_ops = {
	.cmdq_flush = nnp_cmdq_flush,
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
