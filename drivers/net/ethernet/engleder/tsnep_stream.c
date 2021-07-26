// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Gerhard Engleder <gerhard@engleder-embedded.com> */

#include "tsnep.h"

#include <linux/idr.h>
#include <linux/rbtree.h>
#include <linux/poll.h>

#define MAX_DMA_BUFFER_COUNT (16 * 1024)
#define PGOFF_IO 0
#define PGOFF_DMA 4

struct tsnep_dma_buffer {
	pgoff_t pgoff;
	struct rb_node rb_node;
	void *data;
	dma_addr_t addr;
};

#define TSNEP_CMD_STREAM 0
struct tsnep_cmd_stream {
	s32 cmd;
};

#define TSNEP_CMD_DMA 1024
struct tsnep_cmd_dma {
	s32 cmd;
	union {
		u64 offset;
		u64 addr;
	};
};

#define MAX_CMD_LENGTH (sizeof(struct tsnep_cmd_dma))

struct tsnep_file {
	struct tsnep_adapter *adapter;
	struct tsnep_stream *stream;

	bool cmd;
	size_t cmd_length;
	u8 cmd_data[MAX_CMD_LENGTH];
};

static DEFINE_IDA(index_ida);

static struct tsnep_dma_buffer *
tsnep_create_dma_buffer(struct tsnep_stream *stream)
{
	struct tsnep_dma_buffer *buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);

	if (!buffer)
		return 0;

	buffer->data = dma_alloc_coherent(&stream->adapter->pdev->dev,
					  PAGE_SIZE, &buffer->addr, GFP_KERNEL);
	if (!buffer->data) {
		kfree(buffer);

		return 0;
	}

	return buffer;
}

static void tsnep_delete_dma_buffer(struct tsnep_stream *stream,
				    struct tsnep_dma_buffer *buffer)
{
	dma_free_coherent(&stream->adapter->pdev->dev, PAGE_SIZE, buffer->data,
			  buffer->addr);
	kfree(buffer);
}

static struct tsnep_dma_buffer *
tsnep_get_dma_buffer(struct tsnep_stream *stream, pgoff_t pgoff)
{
	struct rb_node **link;
	struct rb_node *parent = 0;
	struct tsnep_dma_buffer *buffer;

	mutex_lock(&stream->dma_buffer_lock);

	/* search for existing DMA buffer */
	link = &stream->dma_buffer.rb_node;
	while (*link != 0) {
		parent = *link;
		buffer = rb_entry(parent, struct tsnep_dma_buffer, rb_node);

		if (buffer->pgoff > pgoff)
			link = &(*link)->rb_left;
		else if (buffer->pgoff < pgoff)
			link = &(*link)->rb_right;
		else
			break;
	}

	/* create new DMA buffer */
	if (*link == 0) {
		buffer = tsnep_create_dma_buffer(stream);
		if (buffer != 0) {
			buffer->pgoff = pgoff;
			rb_link_node(&buffer->rb_node, parent, link);
			rb_insert_color(&buffer->rb_node, &stream->dma_buffer);
		}
	}

	mutex_unlock(&stream->dma_buffer_lock);

	return buffer;
}

static int tsnep_get_dma_buffer_addr(struct tsnep_stream *stream, pgoff_t pgoff,
				     dma_addr_t *addr)
{
	struct rb_node *node;
	struct tsnep_dma_buffer *buffer = 0;
	int retval = -EINVAL;

	mutex_lock(&stream->dma_buffer_lock);

	/* search for existing DMA buffer */
	node = stream->dma_buffer.rb_node;
	while (node != 0) {
		buffer = rb_entry(node, struct tsnep_dma_buffer, rb_node);

		if (buffer->pgoff > pgoff) {
			node = node->rb_left;
		} else if (buffer->pgoff < pgoff) {
			node = node->rb_right;
		} else {
			*addr = buffer->addr;
			retval = 0;
			break;
		}
	}

	mutex_unlock(&stream->dma_buffer_lock);

	return retval;
}

static void tsnep_delete_all_dma_buffers(struct tsnep_stream *stream)
{
	struct rb_node *node;
	struct tsnep_dma_buffer *buffer;

	mutex_lock(&stream->dma_buffer_lock);

	/* delete one DMA buffer after the other */
	node = rb_first(&stream->dma_buffer);
	while (node != 0) {
		rb_erase(node, &stream->dma_buffer);
		buffer = rb_entry(node, struct tsnep_dma_buffer, rb_node);
		tsnep_delete_dma_buffer(stream, buffer);

		node = rb_first(&stream->dma_buffer);
	}

	mutex_unlock(&stream->dma_buffer_lock);
}

static int tsnep_stream_open(struct inode *inode, struct file *filp)
{
	struct tsnep_adapter *adapter =
		container_of(filp->private_data, struct tsnep_adapter, misc);
	struct tsnep_file *tsnep_file;

	tsnep_file = kzalloc(sizeof(*tsnep_file), GFP_KERNEL);
	if (!tsnep_file)
		return -ENOMEM;
	tsnep_file->adapter = adapter;
	filp->private_data = tsnep_file;

	return 0;
}

static int tsnep_stream_release(struct inode *inode, struct file *filp)
{
	struct tsnep_file *tsnep_file = filp->private_data;

	mutex_lock(&tsnep_file->adapter->stream_lock);

	if (tsnep_file->stream) {
		tsnep_delete_all_dma_buffers(tsnep_file->stream);
		tsnep_file->stream->in_use = false;
	}

	mutex_unlock(&tsnep_file->adapter->stream_lock);

	kfree(tsnep_file);

	return 0;
}

static ssize_t tsnep_stream_read(struct file *filp, char __user *buf,
				 size_t count, loff_t *f_pos)
{
	struct tsnep_file *tsnep_file = filp->private_data;
	struct mutex *lock = &tsnep_file->adapter->stream_lock;
	ssize_t retval;

	if (mutex_lock_interruptible(lock))
		return -ERESTARTSYS;

	if (tsnep_file->cmd) {
		if (count < tsnep_file->cmd_length) {
			mutex_unlock(lock);

			return -EINVAL;
		}

		if (copy_to_user(buf, tsnep_file->cmd_data,
				 tsnep_file->cmd_length)) {
			mutex_unlock(lock);

			return -EFAULT;
		}
		tsnep_file->cmd = false;

		retval = tsnep_file->cmd_length;
	} else {
		retval = -EBUSY;
	}

	mutex_unlock(lock);

	return retval;
}

static ssize_t tsnep_stream_assign(struct tsnep_file *tsnep_file, s32 cmd,
				   const char __user *buf, size_t count)
{
	struct mutex *lock = &tsnep_file->adapter->stream_lock;
	ssize_t retval;

	if (count != sizeof(struct tsnep_cmd_stream))
		return -EINVAL;
	if (tsnep_file->stream)
		return -EBUSY;
	if (cmd >= tsnep_file->adapter->stream_count)
		return -ENODEV;

	if (mutex_lock_interruptible(lock))
		return -ERESTARTSYS;

	if (!tsnep_file->adapter->stream[cmd].in_use) {
		tsnep_file->adapter->stream[cmd].in_use = true;
		tsnep_file->stream = &tsnep_file->adapter->stream[cmd];
		retval = count;
	} else {
		retval = -EBUSY;
	}

	mutex_unlock(lock);

	return retval;
}

static ssize_t tsnep_stream_dma(struct tsnep_file *tsnep_file, s32 cmd,
				const char __user *buf, size_t count)
{
	struct mutex *lock = &tsnep_file->adapter->stream_lock;
	struct tsnep_cmd_dma dma;
	pgoff_t pgoff;
	dma_addr_t addr;
	ssize_t retval;

	if (count != sizeof(dma))
		return -EINVAL;
	if (!tsnep_file->stream)
		return -EBUSY;
	if (copy_from_user(&dma, buf, sizeof(dma)))
		return -EFAULT;

	pgoff = dma.offset / PAGE_SIZE;
	if (pgoff < PGOFF_DMA ||
	    pgoff >= (PGOFF_DMA + MAX_DMA_BUFFER_COUNT))
		return -EINVAL;

	if (mutex_lock_interruptible(lock))
		return -ERESTARTSYS;

	if (tsnep_file->cmd) {
		mutex_unlock(lock);

		return -EBUSY;
	}

	retval = tsnep_get_dma_buffer_addr(tsnep_file->stream, pgoff, &addr);
	if (retval) {
		mutex_unlock(lock);

		return retval;
	}

	dma.addr = addr + dma.offset % PAGE_SIZE;
	memcpy(tsnep_file->cmd_data, &dma, sizeof(dma));
	tsnep_file->cmd_length = sizeof(dma);
	tsnep_file->cmd = true;

	retval = count;

	mutex_unlock(lock);

	return retval;
}

static ssize_t tsnep_stream_write(struct file *filp, const char __user *buf,
				  size_t count, loff_t *f_pos)
{
	struct tsnep_file *tsnep_file = filp->private_data;
	s32 cmd;

	if (count < sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd >= TSNEP_CMD_STREAM && cmd < TSNEP_CMD_DMA)
		return tsnep_stream_assign(tsnep_file, cmd, buf, count);
	else if (cmd == TSNEP_CMD_DMA)
		return tsnep_stream_dma(tsnep_file, cmd, buf, count);

	return -EINVAL;
}

static int tsnep_stream_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct tsnep_file *tsnep_file = filp->private_data;
	struct tsnep_dma_buffer *buffer;
	int retval;

	if (!tsnep_file->stream)
		return -ENODEV;

	if ((vma->vm_end - vma->vm_start) > PAGE_SIZE)
		return -EINVAL;

	if (vma->vm_pgoff == PGOFF_IO) {
		/* IO */
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

		retval = remap_pfn_range(vma, vma->vm_start,
					 tsnep_file->stream->addr >> PAGE_SHIFT,
					 vma->vm_end - vma->vm_start,
					 vma->vm_page_prot);
	} else if (vma->vm_pgoff >= PGOFF_DMA &&
		   vma->vm_pgoff < (PGOFF_DMA + MAX_DMA_BUFFER_COUNT)) {
		/* DMA */
		buffer = tsnep_get_dma_buffer(tsnep_file->stream,
					      vma->vm_pgoff);
		if (!buffer)
			return -ENOMEM;

		retval = remap_pfn_range(vma, vma->vm_start,
					 buffer->addr >> PAGE_SHIFT,
					 vma->vm_end - vma->vm_start,
					 vma->vm_page_prot);
	} else {
		retval = -EINVAL;
	}

	return retval;
}

static const struct file_operations tsnep_stream_fops = {
	.owner = THIS_MODULE,
	.open = tsnep_stream_open,
	.release = tsnep_stream_release,
	.read = tsnep_stream_read,
	.write = tsnep_stream_write,
	.mmap = tsnep_stream_mmap,
	.llseek = no_llseek,
};

static ssize_t loopback_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tsnep_adapter *adapter =
		container_of(dev_get_drvdata(dev), struct tsnep_adapter, misc);

	if (!adapter->loopback)
		return sprintf(buf, "off\n");
	else if (adapter->loopback_speed == 1000)
		return sprintf(buf, "1000\n");

	return sprintf(buf, "100\n");
}

static ssize_t loopback_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	struct tsnep_adapter *adapter =
		container_of(dev_get_drvdata(dev), struct tsnep_adapter, misc);
	int retval;

	if (len < 3)
		return -EINVAL;

	if ((len == 3 && strncmp(buf, "100", 3) == 0) ||
	    (len == 4 && strncmp(buf, "100\n", 4) == 0)) {
		retval = tsnep_enable_loopback(adapter, 100);
		if (!retval)
			retval = len;
	} else if ((len == 4 && strncmp(buf, "1000", 4) == 0) ||
		   (len == 5 && strncmp(buf, "1000\n", 5) == 0)) {
		retval = tsnep_enable_loopback(adapter, 1000);
		if (!retval)
			retval = len;
	} else if ((len == 3 && strncmp(buf, "off", 3) == 0) ||
		   (len == 4 && strncmp(buf, "off\n", 4) == 0)) {
		retval = tsnep_disable_loopback(adapter);
		if (!retval)
			retval = len;
	} else {
		retval = -EINVAL;
	}

	return retval;
}

static DEVICE_ATTR_RW(loopback);

static struct attribute *tsnep_stream_attrs[] = {
	&dev_attr_loopback.attr,
	NULL,
};

static const struct attribute_group tsnep_stream_group = {
	.attrs = tsnep_stream_attrs,
};

static const struct attribute_group *tsnep_stream_groups[] = {
	&tsnep_stream_group,
	NULL,
};

int tsnep_stream_init(struct tsnep_adapter *adapter)
{
	struct resource *io;
	int num_queues;
	int i;
	int retval;

	io = platform_get_resource(adapter->pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -ENODEV;

	num_queues = max(adapter->num_tx_queues, adapter->num_rx_queues);
	mutex_init(&adapter->stream_lock);
	for (i = 0; i < adapter->stream_count; i++) {
		adapter->stream[i].adapter = adapter;
		adapter->stream[i].addr = io->start +
					  TSNEP_QUEUE(num_queues + i);
		mutex_init(&adapter->stream[i].dma_buffer_lock);
		adapter->stream[i].dma_buffer = RB_ROOT;
	}

	retval = ida_simple_get(&index_ida, 0, 0, GFP_KERNEL);
	if (retval < 0)
		goto index_failed;
	adapter->index = retval;
	snprintf(adapter->name, sizeof(adapter->name), "%s%d", TSNEP,
		 adapter->index);

	adapter->misc.name = adapter->name;
	adapter->misc.minor = MISC_DYNAMIC_MINOR;
	adapter->misc.fops = &tsnep_stream_fops;
	adapter->misc.parent = &adapter->pdev->dev;
	adapter->misc.groups = tsnep_stream_groups;
	retval = misc_register(&adapter->misc);
	if (retval != 0)
		goto misc_failed;

	return 0;

misc_failed:
	ida_simple_remove(&index_ida, adapter->index);
index_failed:
	return retval;
}

void tsnep_stream_cleanup(struct tsnep_adapter *adapter)
{
	misc_deregister(&adapter->misc);
	ida_simple_remove(&index_ida, adapter->index);
}
