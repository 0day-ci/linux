// SPDX-License-Identifier: GPL-2.0
/*
 * Authors: Cheng Xu <chengyou@linux.alibaba.com>
 *          Kai Shen <kaishen@linux.alibaba.com>
 * Copyright (c) 2020-2021, Alibaba Group.
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/debugfs.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_debug.h"
#include "erdma_verbs.h"

char *cc_method_string[ERDMA_CC_METHODS_NUM] = {
	[ERDMA_CC_NEWRENO] = "newreno",
	[ERDMA_CC_CUBIC] = "cubic",
	[ERDMA_CC_HPCC_RTT] = "hpcc_rtt",
	[ERDMA_CC_HPCC_ECN] = "hpcc_ecn",
	[ERDMA_CC_HPCC_INT] = "hpcc_int"
};

static struct dentry *erdma_debugfs;


static int erdma_dbgfs_file_open(struct inode *inode, struct file *fp)
{
	fp->private_data = inode->i_private;
	return nonseekable_open(inode, fp);
}

static ssize_t erdma_show_stats(struct file *fp, char __user *buf, size_t space,
			      loff_t *ppos)
{
	struct erdma_dev *dev = fp->private_data;
	char *kbuf = NULL;
	int len = 0;

	if (*ppos)
		goto out;

	kbuf = kmalloc(space, GFP_KERNEL);
	if (!kbuf)
		goto out;

	len = snprintf(kbuf, space, "Resource Summary of %s:\n"
		"%s: %d\n%s: %d\n%s: %d\n%s: %d\n%s: %d\n%s: %d\n",
		dev->ibdev.name,
		"ucontext ", atomic_read(&dev->num_ctx),
		"pd       ", atomic_read(&dev->num_pd),
		"qp       ", atomic_read(&dev->num_qp),
		"cq       ", atomic_read(&dev->num_cq),
		"mr       ", atomic_read(&dev->num_mr),
		"cep      ", atomic_read(&dev->num_cep));
	if (len > space)
		len = space;
out:
	if (len)
		len = simple_read_from_buffer(buf, len, ppos, kbuf, len);

	kfree(kbuf);
	return len;

}

static ssize_t erdma_show_cmdq(struct file *fp, char __user *buf, size_t space,
			       loff_t *ppos)
{
	struct erdma_dev *dev = fp->private_data;
	char *kbuf = NULL;
	int len = 0, n;

	if (*ppos)
		goto out;

	kbuf = kmalloc(space, GFP_KERNEL);
	if (!kbuf)
		goto out;

	len =  snprintf(kbuf, space,
		"CMDQ Summary:\n"
		"submitted:%llu, completed:%llu.\n"
		"ceq notify:%llu,,notify:%llu aeq event:%llu,,notify:%llu cq armed:%llu\n",
		dev->cmdq.sq.total_cmds, dev->cmdq.sq.total_comp_cmds,
		atomic64_read(&dev->cmdq.eq.event_num),
		atomic64_read(&dev->cmdq.eq.notify_num),
		atomic64_read(&dev->aeq.eq.event_num),
		atomic64_read(&dev->aeq.eq.notify_num),
		atomic64_read(&dev->cmdq.cq.cq_armed_num));
	if (len > space) {
		len = space;
		goto out;
	}

	space -= len;
	n = snprintf(kbuf + len, space,
		"SQ-buf depth:%u, ci:0x%x, pi:0x%x\n",
		dev->cmdq.sq.depth, dev->cmdq.sq.ci, dev->cmdq.sq.pi);
	len += n;
	space -= n;
	n = snprintf(kbuf + len, space,
		"CQ-buf depth:%u, ci:0x%x\n",
		dev->cmdq.cq.depth, dev->cmdq.cq.ci);
	len += n;
	space -= n;
	n = snprintf(kbuf + len, space,
		"EQ-buf depth:%u, ci:0x%x\n",
		dev->cmdq.eq.depth, dev->cmdq.eq.ci);
	len += n;
	space -= n;
	n = snprintf(kbuf + len, space,
		"AEQ-buf depth:%u, ci:0x%x\n",
		dev->aeq.eq.depth, dev->aeq.eq.ci);
	len += n;
	space -= n;
	n = snprintf(kbuf + len, space,
		"q-flags:0x%lx\n", dev->cmdq.state);

	len += n;
	space -= n;

out:
	if (len)
		len = simple_read_from_buffer(buf, len, ppos, kbuf, len);

	kfree(kbuf);
	return len;

}

static ssize_t erdma_show_ceq(struct file *fp, char __user *buf, size_t space,
			      loff_t *ppos)
{

	struct erdma_dev *dev = fp->private_data;
	char *kbuf = NULL;
	int len = 0, n, i;
	struct erdma_eq_cb *eq_cb;

	if (*ppos)
		goto out;

	kbuf = kmalloc(space, GFP_KERNEL);
	if (!kbuf)
		goto out;

	len =  snprintf(kbuf, space, "CEQs Summary:\n");
	if (len > space) {
		len = space;
		goto out;
	}

	space -= len;

	for (i = 0; i < 31; i++) {
		eq_cb = &dev->ceqs[i];
		n = snprintf(kbuf + len, space,
			"%d ready:%u,event_num:%llu,notify_num:%llu,depth:%u,ci:0x%x\n",
			i, eq_cb->ready,
			atomic64_read(&eq_cb->eq.event_num),
			atomic64_read(&eq_cb->eq.notify_num),
			eq_cb->eq.depth, eq_cb->eq.ci);
		if (n < space) {
			len += n;
			space -= n;
		} else {
			len += space;
			break;
		}
	}

out:
	if (len)
		len = simple_read_from_buffer(buf, len, ppos, kbuf, len);

	kfree(kbuf);
	return len;

}

static ssize_t erdma_show_cc(struct file *fp, char __user *buf, size_t space,
			     loff_t *ppos)
{
	struct erdma_dev *dev = fp->private_data;
	char *kbuf = NULL;
	int len = 0;

	kbuf = kmalloc(space, GFP_KERNEL);
	if (!kbuf)
		goto out;

	if (*ppos)
		goto out;

	if (dev->cc_method < 0 || dev->cc_method >= ERDMA_CC_METHODS_NUM)
		goto out;

	len =  snprintf(kbuf, space, "%s\n", cc_method_string[dev->cc_method]);
	if (len > space)
		len = space;
out:
	if (len)
		len = simple_read_from_buffer(buf, len, ppos, kbuf, len);

	kfree(kbuf);
	return len;

}

static ssize_t erdma_set_cc(struct file *fp, const char __user *buf, size_t count, loff_t *ppos)
{
	int bytes_not_copied;
	struct erdma_dev *dev = fp->private_data;
	char cmd_buf[64];
	int i;

	if (*ppos != 0)
		return 0;

	if (count >= sizeof(cmd_buf))
		return -ENOSPC;

	bytes_not_copied = copy_from_user(cmd_buf, buf, count);
	if (bytes_not_copied < 0)
		return bytes_not_copied;
	if (bytes_not_copied > 0)
		count -= bytes_not_copied;

	cmd_buf[count] = '\0';
	*ppos = 0;

	for (i = 0; i < ERDMA_CC_METHODS_NUM; i++) {
		if (strlen(cc_method_string[i]) == (count - 1) &&
			!memcmp(cmd_buf, cc_method_string[i], count - 1)) {
			dev->cc_method = i;
			return count;
		}
	}

	return -EINVAL;
}

static const struct file_operations erdma_stats_debug_fops = {
	.owner = THIS_MODULE,
	.open = erdma_dbgfs_file_open,
	.read = erdma_show_stats
};

static const struct file_operations erdma_cmdq_debug_fops = {
	.owner = THIS_MODULE,
	.open = erdma_dbgfs_file_open,
	.read = erdma_show_cmdq
};

static const struct file_operations erdma_ceq_debug_fops = {
	.owner = THIS_MODULE,
	.open = erdma_dbgfs_file_open,
	.read = erdma_show_ceq
};

static const struct file_operations erdma_cc_fops = {
	.owner = THIS_MODULE,
	.open = erdma_dbgfs_file_open,
	.read = erdma_show_cc,
	.write = erdma_set_cc,

};

void erdma_debugfs_add_one(struct erdma_dev *dev)
{
	if (!erdma_debugfs)
		return;

	dev->debugfs = debugfs_create_dir(dev->ibdev.name, erdma_debugfs);
	if (dev->debugfs) {
		debugfs_create_file("stats", 0400, dev->debugfs,
			(void *)dev,
			&erdma_stats_debug_fops);
		debugfs_create_file("cmdq", 0400, dev->debugfs,
			(void *)dev,
			&erdma_cmdq_debug_fops);
		debugfs_create_file("ceq", 0400, dev->debugfs,
			(void *)dev,
			&erdma_ceq_debug_fops);
		debugfs_create_file("cc", 0400, dev->debugfs,
			(void *)dev,
			&erdma_cc_fops);
	}

}

void erdma_debugfs_remove_one(struct erdma_dev *dev)
{
	debugfs_remove_recursive(dev->debugfs);
	dev->debugfs = NULL;
}

void erdma_debugfs_init(void)
{
	erdma_debugfs = debugfs_create_dir("erdma", NULL);
}

void erdma_debugfs_exit(void)
{
	debugfs_remove_recursive(erdma_debugfs);
	erdma_debugfs = NULL;
}
