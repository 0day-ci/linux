// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Google, Inc.
 *
 * Author:
 *	Sami Tolvanen <samitolvanen@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"pgo: " fmt

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include "pgo.h"

static struct dentry *directory;

struct prf_private_data {
	void *buffer;
	size_t size;
	struct prf_object *core;
};

/* vmlinux's prf core */
static struct prf_object prf_vmlinux;

/*
 * This lock guards prf_list from concurrent access:
 * - New module is registered.
 * - Module is unloaded.
 * It does *not* protect any PGO serialization functions.
 */
DEFINE_SPINLOCK(prf_reg_lock);
LIST_HEAD(prf_list);

static inline unsigned long prf_list_lock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&prf_reg_lock, flags);

	return flags;
}

static inline void prf_list_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&prf_reg_lock, flags);
}

/*
 * Raw profile data format:
 *
 *	- llvm_prf_header
 *	- __llvm_prf_data
 *	- __llvm_prf_cnts
 *	- __llvm_prf_names
 *	- zero padding to 8 bytes
 *	- for each llvm_prf_data in __llvm_prf_data:
 *		- llvm_prf_value_data
 *			- llvm_prf_value_record + site count array
 *				- llvm_prf_value_node_data
 *				...
 *			...
 *		...
 */

static void prf_fill_header(struct prf_object *po, void **buffer)
{
	struct llvm_prf_header *header = *(struct llvm_prf_header **)buffer;

#ifdef CONFIG_64BIT
	header->magic = LLVM_INSTR_PROF_RAW_MAGIC_64;
#else
	header->magic = LLVM_INSTR_PROF_RAW_MAGIC_32;
#endif
	header->version = LLVM_VARIANT_MASK_IR_PROF | LLVM_INSTR_PROF_RAW_VERSION;
	header->data_size = po->data_num;
	header->padding_bytes_before_counters = 0;
	header->counters_size = po->cnts_num;
	header->padding_bytes_after_counters = 0;
	header->names_size = po->names_num;
	header->counters_delta = (u64)po->cnts;
	header->names_delta = (u64)po->names;
	header->value_kind_last = LLVM_INSTR_PROF_IPVK_LAST;

	*buffer += sizeof(*header);
}

/*
 * Copy the source into the buffer, incrementing the pointer into buffer in the
 * process.
 */
static void prf_copy_to_buffer(void **buffer, const void *src, unsigned long size)
{
	memcpy(*buffer, src, size);
	*buffer += size;
}

static u32 __prf_get_value_size(struct llvm_prf_data *p, u32 *value_kinds)
{
	struct llvm_prf_value_node **nodes =
		(struct llvm_prf_value_node **)p->values;
	u32 kinds = 0;
	u32 size = 0;
	unsigned int kind;
	unsigned int n;
	unsigned int s = 0;

	for (kind = 0; kind < ARRAY_SIZE(p->num_value_sites); kind++) {
		unsigned int sites = p->num_value_sites[kind];

		if (!sites)
			continue;

		/* Record + site count array */
		size += prf_get_value_record_size(sites);
		kinds++;

		if (!nodes)
			continue;

		for (n = 0; n < sites; n++) {
			u32 count = 0;
			struct llvm_prf_value_node *site = nodes[s + n];

			while (site && ++count <= U8_MAX)
				site = site->next;

			size += count *
				sizeof(struct llvm_prf_value_node_data);
		}

		s += sites;
	}

	if (size)
		size += sizeof(struct llvm_prf_value_data);

	if (value_kinds)
		*value_kinds = kinds;

	return size;
}

static u32 prf_get_value_size(struct prf_object *po)
{
	u32 size = 0;
	struct llvm_prf_data *p;
	struct llvm_prf_data *end = po->data + po->data_num;

	for (p = po->data; p < end; p++)
		size += __prf_get_value_size(p, NULL);

	return size;
}

/* Serialize the profiling's value. */
static void prf_serialize_value(struct llvm_prf_data *p, void **buffer)
{
	struct llvm_prf_value_data header;
	struct llvm_prf_value_node **nodes =
		(struct llvm_prf_value_node **)p->values;
	unsigned int kind;
	unsigned int n;
	unsigned int s = 0;

	header.total_size = __prf_get_value_size(p, &header.num_value_kinds);

	if (!header.num_value_kinds)
		/* Nothing to write. */
		return;

	prf_copy_to_buffer(buffer, &header, sizeof(header));

	for (kind = 0; kind < ARRAY_SIZE(p->num_value_sites); kind++) {
		struct llvm_prf_value_record *record;
		u8 *counts;
		unsigned int sites = p->num_value_sites[kind];

		if (!sites)
			continue;

		/* Profiling value record. */
		record = *(struct llvm_prf_value_record **)buffer;
		*buffer += prf_get_value_record_header_size();

		record->kind = kind;
		record->num_value_sites = sites;

		/* Site count array. */
		counts = *(u8 **)buffer;
		*buffer += prf_get_value_record_site_count_size(sites);

		/*
		 * If we don't have nodes, we can skip updating the site count
		 * array, because the buffer is zero filled.
		 */
		if (!nodes)
			continue;

		for (n = 0; n < sites; n++) {
			u32 count = 0;
			struct llvm_prf_value_node *site = nodes[s + n];

			while (site && ++count <= U8_MAX) {
				prf_copy_to_buffer(buffer, site,
						   sizeof(struct llvm_prf_value_node_data));
				site = site->next;
			}

			counts[n] = (u8)count;
		}

		s += sites;
	}
}

static void prf_serialize_values(struct prf_object *po, void **buffer)
{
	struct llvm_prf_data *p;
	struct llvm_prf_data *end = po->data + po->data_num;

	for (p = po->data; p < end; p++)
		prf_serialize_value(p, buffer);
}

static inline unsigned long prf_get_padding(unsigned long size)
{
	return 7 & (sizeof(u64) - size % sizeof(u64));
}

/* Note: caller *must* hold pgo_lock */
static unsigned long prf_buffer_size(struct prf_object *po)
{
	return sizeof(struct llvm_prf_header)	+
			prf_data_size(po)	+
			prf_cnts_size(po)	+
			prf_names_size(po)	+
			prf_get_padding(prf_names_size(po)) +
			prf_get_value_size(po);
}

/*
 * Serialize the profiling data into a format LLVM's tools can understand.
 * Returns actual buffer size in p->size.
 * Note: p->buffer must point into vzalloc()'d
 * area of at least prf_buffer_size() in size.
 * Note: caller *must* hold pgo_lock.
 */
static int prf_serialize(struct prf_object *po, struct prf_private_data *p, size_t buf_size)
{
	void *buffer;

	/* get buffer size, again. */
	p->size = prf_buffer_size(po);

	/* check for unlikely overflow. */
	if (p->size > buf_size)
		return -EAGAIN;

	buffer = p->buffer;

	prf_fill_header(po, &buffer);
	prf_copy_to_buffer(&buffer, po->data,  prf_data_size(po));
	prf_copy_to_buffer(&buffer, po->cnts,  prf_cnts_size(po));
	prf_copy_to_buffer(&buffer, po->names, prf_names_size(po));
	buffer += prf_get_padding(prf_names_size(po));

	prf_serialize_values(po, &buffer);

	return 0;
}

/* open() implementation for PGO. Creates a copy of the profiling data set. */
static int prf_open(struct inode *inode, struct file *file)
{
	struct prf_private_data *data;
	unsigned long flags;
	size_t buf_size;
	int err = -EINVAL;

	if (WARN_ON(!inode->i_private)) {
		/* Bug: inode was not initialized by us. */
		return err;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Get prf_object of this inode. */
	data->core = inode->i_private;

	/* Get initial buffer size. */
	flags = prf_lock();
	data->size = prf_buffer_size(data->core);
	prf_unlock(flags);

	do {
		vfree(data->buffer);

		/* Allocate, round up to page size. */
		buf_size = PAGE_ALIGN(data->size);
		data->buffer = vzalloc(buf_size);

		if (!data->buffer) {
			err = -ENOMEM;
			break;
		}

		/*
		 * Try serialize and get actual
		 * data length in data->size.
		 */
		flags = prf_lock();
		err = prf_serialize(data->core, data, buf_size);
		prf_unlock(flags);
		/* In unlikely case, try again. */
	} while (err == -EAGAIN);

	if (err < 0) {

		if (data)
			vfree(data->buffer);
		kfree(data);
	} else {
		file->private_data = data;
	}

	return err;
}

/* read() implementation for PGO. */
static ssize_t prf_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	struct prf_private_data *data = file->private_data;

	if (WARN_ON_ONCE(!data))
		return -ENOMEM;

	return simple_read_from_buffer(buf, count, ppos, data->buffer,
				       data->size);
}

/* release() implementation for PGO. Release resources allocated by open(). */
static int prf_release(struct inode *inode, struct file *file)
{
	struct prf_private_data *data = file->private_data;

	if (data) {
		vfree(data->buffer);
		kfree(data);
	}

	return 0;
}

static const struct file_operations prf_fops = {
	.owner		= THIS_MODULE,
	.open		= prf_open,
	.read		= prf_read,
	.llseek		= default_llseek,
	.release	= prf_release
};

/* write() implementation for resetting PGO's profile data. */
static ssize_t reset_write(struct file *file, const char __user *addr,
			   size_t len, loff_t *pos)
{
	struct llvm_prf_data *data;

	memset(__llvm_prf_cnts_start, 0, prf_cnts_size(&prf_vmlinux));

	for (data = __llvm_prf_data_start; data < __llvm_prf_data_end; data++) {
		struct llvm_prf_value_node **vnodes;
		u64 current_vsite_count;
		u32 i;

		if (!data->values)
			continue;

		current_vsite_count = 0;
		vnodes = (struct llvm_prf_value_node **)data->values;

		for (i = LLVM_INSTR_PROF_IPVK_FIRST; i <= LLVM_INSTR_PROF_IPVK_LAST; i++)
			current_vsite_count += data->num_value_sites[i];

		for (i = 0; i < current_vsite_count; i++) {
			struct llvm_prf_value_node *current_vnode = vnodes[i];

			while (current_vnode) {
				current_vnode->count = 0;
				current_vnode = current_vnode->next;
			}
		}
	}

	return len;
}

static const struct file_operations prf_reset_fops = {
	.owner		= THIS_MODULE,
	.write		= reset_write,
	.llseek		= noop_llseek,
};

static void pgo_module_init(struct module *mod)
{
	struct prf_object *po;
	char fname[MODULE_NAME_LEN + 9]; /* +strlen(".profraw") */
	unsigned long flags;

	/* Add new prf_object entry for the module. */
	po = kzalloc(sizeof(*po), GFP_KERNEL);
	if (!po)
		return; /* -ENOMEM */

	po->mod_name = mod->name;

	fname[0] = 0;
	snprintf(fname, sizeof(fname), "%s.profraw", po->mod_name);

	/* Setup prf_object sections. */
	po->data = mod->prf_data;
	po->data_num = prf_get_count(mod->prf_data,
		(char *)mod->prf_data + mod->prf_data_size, sizeof(po->data[0]));

	po->cnts = mod->prf_cnts;
	po->cnts_num = prf_get_count(mod->prf_cnts,
		(char *)mod->prf_cnts + mod->prf_cnts_size, sizeof(po->cnts[0]));

	po->names = mod->prf_names;
	po->names_num = prf_get_count(mod->prf_names,
		(char *)mod->prf_names + mod->prf_names_size, sizeof(po->names[0]));

	po->vnds = mod->prf_vnds;
	po->vnds_num = prf_get_count(mod->prf_vnds,
		(char *)mod->prf_vnds + mod->prf_vnds_size, sizeof(po->vnds[0]));

	/* Create debugfs entry. */
	po->file = debugfs_create_file(fname, 0600, directory, po, &prf_fops);
	if (!po->file) {
		pr_err("Failed to setup module pgo: %s", fname);
		kfree(po);

	} else {
		/* Finally enable profiling for the module. */
		flags = prf_list_lock();
		list_add_tail_rcu(&po->link, &prf_list);
		prf_list_unlock(flags);
	}

	return;
}

static int pgo_module_notifier(struct notifier_block *nb, unsigned long event,
				void *pdata)
{
	struct module *mod = pdata;
	struct prf_object *po;
	unsigned long flags;

	if (event == MODULE_STATE_LIVE) {
		/* Does the module have profiling info? */
		if (mod->prf_data
			&& mod->prf_cnts
			&& mod->prf_names
			&& mod->prf_vnds) {

			/* Setup module profiling. */
			pgo_module_init(mod);
		}
	}

	if (event == MODULE_STATE_GOING) {
		/* Find the prf_object from the list. */
		rcu_read_lock();

		list_for_each_entry_rcu(po, &prf_list, link) {
			if (strcmp(po->mod_name, mod->name) == 0)
				goto out_unlock;
		}
		/* No such module. */
		po = NULL;

out_unlock:
		rcu_read_unlock();

		if (po) {
			/* Remove from profiled modules. */
			flags = prf_list_lock();
			list_del_rcu(&po->link);
			prf_list_unlock(flags);

			debugfs_remove(po->file);
			po->file = NULL;

			/* Cleanup memory. */
			kfree_rcu(po, rcu);
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block pgo_module_nb = {
	.notifier_call = pgo_module_notifier
};

/* Create debugfs entries. */
static int __init pgo_init(void)
{
	unsigned long flags;

	/* Init profiler vmlinux core entry. */
	memset(&prf_vmlinux, 0, sizeof(prf_vmlinux));
	prf_vmlinux.data = __llvm_prf_data_start;
	prf_vmlinux.data_num = prf_get_count(__llvm_prf_data_start,
		__llvm_prf_data_end, sizeof(__llvm_prf_data_start[0]));

	prf_vmlinux.cnts = __llvm_prf_cnts_start;
	prf_vmlinux.cnts_num = prf_get_count(__llvm_prf_cnts_start,
		__llvm_prf_cnts_end, sizeof(__llvm_prf_cnts_start[0]));

	prf_vmlinux.names = __llvm_prf_names_start;
	prf_vmlinux.names_num = prf_get_count(__llvm_prf_names_start,
		__llvm_prf_names_end, sizeof(__llvm_prf_names_start[0]));

	prf_vmlinux.vnds = __llvm_prf_vnds_start;
	prf_vmlinux.vnds_num = prf_get_count(__llvm_prf_vnds_start,
		__llvm_prf_vnds_end, sizeof(__llvm_prf_vnds_start[0]));

	/* Enable profiling. */
	flags = prf_list_lock();
	prf_vmlinux.mod_name = "vmlinux";
	list_add_tail_rcu(&prf_vmlinux.link, &prf_list);
	prf_list_unlock(flags);

	directory = debugfs_create_dir("pgo", NULL);
	if (!directory)
		goto err_remove;

	prf_vmlinux.file = debugfs_create_file("vmlinux.profraw",
		0600, directory, &prf_vmlinux, &prf_fops);
	if (!prf_vmlinux.file)
		goto err_remove;

	if (!debugfs_create_file("reset", 0200, directory, NULL,
				 &prf_reset_fops))
		goto err_remove;

	/* Register module notifer. */
	register_module_notifier(&pgo_module_nb);

	/* Show notice why the system slower: */
	pr_notice("Clang PGO instrumentation is active.");

	return 0;

err_remove:
	pr_err("initialization failed\n");
	return -EIO;
}

/* Remove debugfs entries. */
static void __exit pgo_exit(void)
{
	/* Unsubscribe the notifier and do cleanup. */
	unregister_module_notifier(&pgo_module_nb);

	debugfs_remove_recursive(directory);
}

module_init(pgo_init);
module_exit(pgo_exit);
