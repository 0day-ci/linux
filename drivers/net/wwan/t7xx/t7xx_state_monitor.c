// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021, Intel Corporation.
 *
 * Authors: Haijun Lio <haijun.liu@mediatek.com>
 * Contributors: Amir Hanania <amir.hanania@intel.com>
 *               Eliot Lee <eliot.lee@intel.com>
 *               Moises Veleta <moises.veleta@intel.com>
 *               Ricardo Martinez<ricardo.martinez@linux.intel.com>
 *               Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>

#include "t7xx_hif_cldma.h"
#include "t7xx_mhccif.h"
#include "t7xx_modem_ops.h"
#include "t7xx_monitor.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_port.h"
#include "t7xx_port_proxy.h"

#define FSM_DRM_DISABLE_DELAY_MS 200
#define FSM_EX_REASON GENMASK(23, 16)

static struct ccci_fsm_ctl *ccci_fsm_entry;

void fsm_notifier_register(struct fsm_notifier_block *notifier)
{
	struct ccci_fsm_ctl *ctl;
	unsigned long flags;

	ctl = ccci_fsm_entry;
	spin_lock_irqsave(&ctl->notifier_lock, flags);
	list_add_tail(&notifier->entry, &ctl->notifier_list);
	spin_unlock_irqrestore(&ctl->notifier_lock, flags);
}

void fsm_notifier_unregister(struct fsm_notifier_block *notifier)
{
	struct fsm_notifier_block *notifier_cur, *notifier_next;
	struct ccci_fsm_ctl *ctl;
	unsigned long flags;

	ctl = ccci_fsm_entry;
	spin_lock_irqsave(&ctl->notifier_lock, flags);
	list_for_each_entry_safe(notifier_cur, notifier_next,
				 &ctl->notifier_list, entry) {
		if (notifier_cur == notifier)
			list_del(&notifier->entry);
	}

	spin_unlock_irqrestore(&ctl->notifier_lock, flags);
}

static void fsm_state_notify(enum md_state state)
{
	struct fsm_notifier_block *notifier;
	struct ccci_fsm_ctl *ctl;
	unsigned long flags;

	ctl = ccci_fsm_entry;
	spin_lock_irqsave(&ctl->notifier_lock, flags);
	list_for_each_entry(notifier, &ctl->notifier_list, entry) {
		spin_unlock_irqrestore(&ctl->notifier_lock, flags);
		if (notifier->notifier_fn)
			notifier->notifier_fn(state, notifier->data);

		spin_lock_irqsave(&ctl->notifier_lock, flags);
	}

	spin_unlock_irqrestore(&ctl->notifier_lock, flags);
}

void fsm_broadcast_state(struct ccci_fsm_ctl *ctl, enum md_state state)
{
	if (ctl->md_state != MD_STATE_WAITING_FOR_HS2 && state == MD_STATE_READY)
		return;

	ctl->md_state = state;

	/* update to port first, otherwise sending message on HS2 may fail */
	port_proxy_md_status_notify(state);

	fsm_state_notify(state);
}

static void fsm_finish_command(struct ccci_fsm_ctl *ctl, struct ccci_fsm_command *cmd, int result)
{
	unsigned long flags;

	if (cmd->flag & FSM_CMD_FLAG_WAITING_TO_COMPLETE) {
		/* The processing thread may see the list, after a command is added,
		 * without being woken up. Hence a spinlock is needed.
		 */
		spin_lock_irqsave(&ctl->cmd_complete_lock, flags);
		cmd->result = result;
		wake_up_all(&cmd->complete_wq);
		spin_unlock_irqrestore(&ctl->cmd_complete_lock, flags);
	} else {
		/* no one is waiting for this command, free to kfree */
		kfree(cmd);
	}
}

/* call only with protection of event_lock */
static void fsm_finish_event(struct ccci_fsm_ctl *ctl, struct ccci_fsm_event *event)
{
	list_del(&event->entry);
	kfree(event);
}

static void fsm_flush_queue(struct ccci_fsm_ctl *ctl)
{
	struct ccci_fsm_event *event, *evt_next;
	struct ccci_fsm_command *cmd, *cmd_next;
	unsigned long flags;
	struct device *dev;

	dev = &ctl->md->mtk_dev->pdev->dev;
	spin_lock_irqsave(&ctl->command_lock, flags);
	list_for_each_entry_safe(cmd, cmd_next, &ctl->command_queue, entry) {
		dev_warn(dev, "unhandled command %d\n", cmd->cmd_id);
		list_del(&cmd->entry);
		fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_FAIL);
	}

	spin_unlock_irqrestore(&ctl->command_lock, flags);
	spin_lock_irqsave(&ctl->event_lock, flags);
	list_for_each_entry_safe(event, evt_next, &ctl->event_queue, entry) {
		dev_warn(dev, "unhandled event %d\n", event->event_id);
		fsm_finish_event(ctl, event);
	}

	spin_unlock_irqrestore(&ctl->event_lock, flags);
}

/* cmd is not NULL only when reason is ordinary exception */
static void fsm_routine_exception(struct ccci_fsm_ctl *ctl, struct ccci_fsm_command *cmd,
				  enum ccci_ex_reason reason)
{
	struct t7xx_port *log_port, *meta_port;
	bool rec_ok = false, pass = false;
	struct ccci_fsm_event *event;
	unsigned long flags;
	struct device *dev;
	int cnt;

	dev = &ctl->md->mtk_dev->pdev->dev;
	dev_err(dev, "exception %d, from %ps\n", reason, __builtin_return_address(0));
	/* state sanity check */
	if (ctl->curr_state != CCCI_FSM_READY && ctl->curr_state != CCCI_FSM_STARTING) {
		if (cmd)
			fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_FAIL);
		return;
	}

	ctl->last_state = ctl->curr_state;
	ctl->curr_state = CCCI_FSM_EXCEPTION;

	/* check exception reason */
	switch (reason) {
	case EXCEPTION_HS_TIMEOUT:
		dev_err(dev, "BOOT_HS_FAIL\n");
		break;

	case EXCEPTION_EVENT:
		fsm_broadcast_state(ctl, MD_STATE_EXCEPTION);
		mtk_pci_pm_exp_detected(ctl->md->mtk_dev);
		mtk_md_exception_handshake(ctl->md);
		cnt = 0;
		while (cnt < MD_EX_REC_OK_TIMEOUT_MS / EVENT_POLL_INTERVAL_MS) {
			if (kthread_should_stop())
				return;

			spin_lock_irqsave(&ctl->event_lock, flags);
			if (!list_empty(&ctl->event_queue)) {
				event = list_first_entry(&ctl->event_queue,
							 struct ccci_fsm_event, entry);
				if (event->event_id == CCCI_EVENT_MD_EX) {
					fsm_finish_event(ctl, event);
				} else if (event->event_id == CCCI_EVENT_MD_EX_REC_OK) {
					rec_ok = true;
					fsm_finish_event(ctl, event);
				}
			}

			spin_unlock_irqrestore(&ctl->event_lock, flags);
			if (rec_ok)
				break;

			cnt++;
			msleep(EVENT_POLL_INTERVAL_MS);
		}

		cnt = 0;
		while (cnt < MD_EX_PASS_TIMEOUT_MS / EVENT_POLL_INTERVAL_MS) {
			if (kthread_should_stop())
				return;

			spin_lock_irqsave(&ctl->event_lock, flags);
			if (!list_empty(&ctl->event_queue)) {
				event = list_first_entry(&ctl->event_queue,
							 struct ccci_fsm_event, entry);
				if (event->event_id == CCCI_EVENT_MD_EX_PASS) {
					pass = true;
					fsm_finish_event(ctl, event);
				}
			}

			spin_unlock_irqrestore(&ctl->event_lock, flags);
			if (pass) {
				log_port = port_get_by_name("ttyCMdLog");
				if (log_port)
					log_port->ops->enable_chl(log_port);
				else
					dev_err(dev, "ttyCMdLog port not found\n");

				meta_port = port_get_by_name("ttyCMdMeta");
				if (meta_port)
					meta_port->ops->enable_chl(meta_port);
				else
					dev_err(dev, "ttyCMdMeta port not found\n");

				break;
			}

			cnt++;
			msleep(EVENT_POLL_INTERVAL_MS);
		}

		break;

	default:
		break;
	}

	if (cmd)
		fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_OK);
}

static void fsm_stopped_handler(struct ccci_fsm_ctl *ctl)
{
	ctl->last_state = ctl->curr_state;
	ctl->curr_state = CCCI_FSM_STOPPED;

	fsm_broadcast_state(ctl, MD_STATE_STOPPED);
	mtk_md_reset(ctl->md->mtk_dev);
}

static void fsm_routine_stopped(struct ccci_fsm_ctl *ctl, struct ccci_fsm_command *cmd)
{
	/* state sanity check */
	if (ctl->curr_state == CCCI_FSM_STOPPED) {
		fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_FAIL);
		return;
	}

	fsm_stopped_handler(ctl);
	fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_OK);
}

static void fsm_routine_stopping(struct ccci_fsm_ctl *ctl, struct ccci_fsm_command *cmd)
{
	struct mtk_pci_dev *mtk_dev;
	int err;

	/* state sanity check */
	if (ctl->curr_state == CCCI_FSM_STOPPED || ctl->curr_state == CCCI_FSM_STOPPING) {
		fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_FAIL);
		return;
	}

	ctl->last_state = ctl->curr_state;
	ctl->curr_state = CCCI_FSM_STOPPING;

	fsm_broadcast_state(ctl, MD_STATE_WAITING_TO_STOP);
	/* stop HW */
	cldma_stop(ID_CLDMA1);

	mtk_dev = ctl->md->mtk_dev;
	if (!atomic_read(&ctl->md->rgu_irq_asserted)) {
		/* disable DRM before FLDR */
		mhccif_h2d_swint_trigger(mtk_dev, H2D_CH_DRM_DISABLE_AP);
		msleep(FSM_DRM_DISABLE_DELAY_MS);
		/* try FLDR first */
		err = mtk_acpi_fldr_func(mtk_dev);
		if (err)
			mhccif_h2d_swint_trigger(mtk_dev, H2D_CH_DEVICE_RESET);
	}

	/* auto jump to stopped state handler */
	fsm_stopped_handler(ctl);

	fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_OK);
}

static void fsm_routine_ready(struct ccci_fsm_ctl *ctl)
{
	struct mtk_modem *md;

	ctl->last_state = ctl->curr_state;
	ctl->curr_state = CCCI_FSM_READY;
	fsm_broadcast_state(ctl, MD_STATE_READY);
	md = ctl->md;
	mtk_md_event_notify(md, FSM_READY);
}

static void fsm_routine_starting(struct ccci_fsm_ctl *ctl)
{
	struct mtk_modem *md;
	struct device *dev;

	ctl->last_state = ctl->curr_state;
	ctl->curr_state = CCCI_FSM_STARTING;

	fsm_broadcast_state(ctl, MD_STATE_WAITING_FOR_HS1);
	md = ctl->md;
	dev = &md->mtk_dev->pdev->dev;
	mtk_md_event_notify(md, FSM_START);

	wait_event_interruptible_timeout(ctl->async_hk_wq,
					 atomic_read(&md->core_md.ready) ||
					 atomic_read(&ctl->exp_flg), HZ * 60);

	if (atomic_read(&ctl->exp_flg))
		dev_err(dev, "MD exception is captured during handshake\n");

	if (!atomic_read(&md->core_md.ready)) {
		dev_err(dev, "MD handshake timeout\n");
		if (atomic_read(&md->core_md.handshake_ongoing))
			fsm_append_event(ctl, CCCI_EVENT_MD_HS2_EXIT, NULL, 0);

		fsm_routine_exception(ctl, NULL, EXCEPTION_HS_TIMEOUT);
	} else {
		mtk_pci_pm_init_late(md->mtk_dev);
		fsm_routine_ready(ctl);
	}
}

static void fsm_routine_start(struct ccci_fsm_ctl *ctl, struct ccci_fsm_command *cmd)
{
	struct mtk_modem *md;
	struct device *dev;
	u32 dev_status;

	md = ctl->md;
	if (!md)
		return;

	dev = &md->mtk_dev->pdev->dev;
	/* state sanity check */
	if (ctl->curr_state != CCCI_FSM_INIT &&
	    ctl->curr_state != CCCI_FSM_PRE_START &&
	    ctl->curr_state != CCCI_FSM_STOPPED) {
		fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_FAIL);
		return;
	}

	ctl->last_state = ctl->curr_state;
	ctl->curr_state = CCCI_FSM_PRE_START;
	mtk_md_event_notify(md, FSM_PRE_START);

	read_poll_timeout(ioread32, dev_status, (dev_status & MISC_STAGE_MASK) == LINUX_STAGE,
			  20000, 2000000, false, IREG_BASE(md->mtk_dev) + PCIE_MISC_DEV_STATUS);
	if ((dev_status & MISC_STAGE_MASK) != LINUX_STAGE) {
		dev_err(dev, "invalid device status 0x%lx\n", dev_status & MISC_STAGE_MASK);
		fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_FAIL);
		return;
	}
	cldma_hif_hw_init(ID_CLDMA1);
	fsm_routine_starting(ctl);
	fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_OK);
}

static int fsm_main_thread(void *data)
{
	struct ccci_fsm_command *cmd;
	struct ccci_fsm_ctl *ctl;
	unsigned long flags;

	ctl = data;

	while (!kthread_should_stop()) {
		if (wait_event_interruptible(ctl->command_wq, !list_empty(&ctl->command_queue) ||
					     kthread_should_stop()))
			continue;
		if (kthread_should_stop())
			break;

		spin_lock_irqsave(&ctl->command_lock, flags);
		cmd = list_first_entry(&ctl->command_queue, struct ccci_fsm_command, entry);
		list_del(&cmd->entry);
		spin_unlock_irqrestore(&ctl->command_lock, flags);

		switch (cmd->cmd_id) {
		case CCCI_COMMAND_START:
			fsm_routine_start(ctl, cmd);
			break;

		case CCCI_COMMAND_EXCEPTION:
			fsm_routine_exception(ctl, cmd, FIELD_GET(FSM_EX_REASON, cmd->flag));
			break;

		case CCCI_COMMAND_PRE_STOP:
			fsm_routine_stopping(ctl, cmd);
			break;

		case CCCI_COMMAND_STOP:
			fsm_routine_stopped(ctl, cmd);
			break;

		default:
			fsm_finish_command(ctl, cmd, FSM_CMD_RESULT_FAIL);
			fsm_flush_queue(ctl);
			break;
		}
	}

	return 0;
}

int fsm_append_command(struct ccci_fsm_ctl *ctl, enum ccci_fsm_cmd_state cmd_id, unsigned int flag)
{
	struct ccci_fsm_command *cmd;
	unsigned long flags;
	int result = 0;

	cmd = kmalloc(sizeof(*cmd),
		      (in_irq() || in_softirq() || irqs_disabled()) ? GFP_ATOMIC : GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	INIT_LIST_HEAD(&cmd->entry);
	init_waitqueue_head(&cmd->complete_wq);
	cmd->cmd_id = cmd_id;
	cmd->result = FSM_CMD_RESULT_PENDING;
	if (in_irq() || irqs_disabled())
		flag &= ~FSM_CMD_FLAG_WAITING_TO_COMPLETE;

	cmd->flag = flag;

	spin_lock_irqsave(&ctl->command_lock, flags);
	list_add_tail(&cmd->entry, &ctl->command_queue);
	spin_unlock_irqrestore(&ctl->command_lock, flags);
	/* after this line, only dereference command when "waiting-to-complete" */
	wake_up(&ctl->command_wq);
	if (flag & FSM_CMD_FLAG_WAITING_TO_COMPLETE) {
		wait_event(cmd->complete_wq, cmd->result != FSM_CMD_RESULT_PENDING);
		if (cmd->result != FSM_CMD_RESULT_OK)
			result = -EINVAL;

		spin_lock_irqsave(&ctl->cmd_complete_lock, flags);
		kfree(cmd);
		spin_unlock_irqrestore(&ctl->cmd_complete_lock, flags);
	}

	return result;
}

int fsm_append_event(struct ccci_fsm_ctl *ctl, enum ccci_fsm_event_state event_id,
		     unsigned char *data, unsigned int length)
{
	struct ccci_fsm_event *event;
	unsigned long flags;
	struct device *dev;

	dev = &ctl->md->mtk_dev->pdev->dev;

	if (event_id <= CCCI_EVENT_INVALID || event_id >= CCCI_EVENT_MAX) {
		dev_err(dev, "invalid event %d\n", event_id);
		return -EINVAL;
	}

	event = kmalloc(struct_size(event, data, length),
			in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	INIT_LIST_HEAD(&event->entry);
	event->event_id = event_id;
	event->length = length;
	if (data && length)
		memcpy(event->data, data, flex_array_size(event, data, event->length));

	spin_lock_irqsave(&ctl->event_lock, flags);
	list_add_tail(&event->entry, &ctl->event_queue);
	spin_unlock_irqrestore(&ctl->event_lock, flags);
	wake_up_all(&ctl->event_wq);
	return 0;
}

void fsm_clear_event(struct ccci_fsm_ctl *ctl, enum ccci_fsm_event_state event_id)
{
	struct ccci_fsm_event *event, *evt_next;
	unsigned long flags;
	struct device *dev;

	dev = &ctl->md->mtk_dev->pdev->dev;

	spin_lock_irqsave(&ctl->event_lock, flags);
	list_for_each_entry_safe(event, evt_next, &ctl->event_queue, entry) {
		dev_err(dev, "unhandled event %d\n", event->event_id);
		if (event->event_id == event_id)
			fsm_finish_event(ctl, event);
	}

	spin_unlock_irqrestore(&ctl->event_lock, flags);
}

struct ccci_fsm_ctl *fsm_get_entity_by_device_number(dev_t dev_n)
{
	if (ccci_fsm_entry && ccci_fsm_entry->monitor_ctl.dev_n == dev_n)
		return ccci_fsm_entry;

	return NULL;
}

struct ccci_fsm_ctl *fsm_get_entry(void)
{
	return ccci_fsm_entry;
}

enum md_state ccci_fsm_get_md_state(void)
{
	struct ccci_fsm_ctl *ctl;

	ctl = ccci_fsm_entry;
	if (ctl)
		return ctl->md_state;
	else
		return MD_STATE_INVALID;
}

unsigned int ccci_fsm_get_current_state(void)
{
	struct ccci_fsm_ctl *ctl;

	ctl = ccci_fsm_entry;
	if (ctl)
		return ctl->curr_state;
	else
		return CCCI_FSM_STOPPED;
}

void ccci_fsm_recv_md_interrupt(enum md_irq_type type)
{
	struct ccci_fsm_ctl *ctl;

	ctl = ccci_fsm_entry;
	if (type == MD_IRQ_PORT_ENUM) {
		fsm_append_command(ctl, CCCI_COMMAND_START, 0);
	} else if (type == MD_IRQ_CCIF_EX) {
		/* interrupt handshake flow */
		atomic_set(&ctl->exp_flg, 1);
		wake_up(&ctl->async_hk_wq);
		fsm_append_command(ctl, CCCI_COMMAND_EXCEPTION,
				   FIELD_PREP(FSM_EX_REASON, EXCEPTION_EE));
	}
}

void ccci_fsm_reset(void)
{
	struct ccci_fsm_ctl *ctl;

	ctl = ccci_fsm_entry;
	/* Clear event and command queues */
	fsm_flush_queue(ctl);

	ctl->last_state = CCCI_FSM_INIT;
	ctl->curr_state = CCCI_FSM_STOPPED;
	atomic_set(&ctl->exp_flg, 0);
}

int ccci_fsm_init(struct mtk_modem *md)
{
	struct ccci_fsm_ctl *ctl;

	ctl = devm_kzalloc(&md->mtk_dev->pdev->dev, sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	ccci_fsm_entry = ctl;
	ctl->md = md;
	ctl->last_state = CCCI_FSM_INIT;
	ctl->curr_state = CCCI_FSM_INIT;
	INIT_LIST_HEAD(&ctl->command_queue);
	INIT_LIST_HEAD(&ctl->event_queue);
	init_waitqueue_head(&ctl->async_hk_wq);
	init_waitqueue_head(&ctl->event_wq);
	INIT_LIST_HEAD(&ctl->notifier_list);
	init_waitqueue_head(&ctl->command_wq);
	spin_lock_init(&ctl->event_lock);
	spin_lock_init(&ctl->command_lock);
	spin_lock_init(&ctl->cmd_complete_lock);
	atomic_set(&ctl->exp_flg, 0);
	spin_lock_init(&ctl->notifier_lock);

	ctl->fsm_thread = kthread_run(fsm_main_thread, ctl, "ccci_fsm");
	if (IS_ERR(ctl->fsm_thread)) {
		dev_err(&md->mtk_dev->pdev->dev, "failed to start monitor thread\n");
		return PTR_ERR(ctl->fsm_thread);
	}

	return 0;
}

void ccci_fsm_uninit(void)
{
	struct ccci_fsm_ctl *ctl;

	ctl = ccci_fsm_entry;
	if (!ctl)
		return;

	if (ctl->fsm_thread)
		kthread_stop(ctl->fsm_thread);

	fsm_flush_queue(ctl);
}
