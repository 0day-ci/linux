// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2020 Intel Corporation

#include <linux/anon_inodes.h>
#include <linux/version.h>
#include <linux/configfs.h>
#include <linux/fdtable.h>
#include "dlb_configfs.h"

struct dlb_device_configfs dlb_dev_configfs[16];

/*
 * DLB domain configfs callback template minimizes replication of boilerplate
 * code to copy arguments, acquire and release the resource lock, and execute
 * the command.  The arguments and response structure name should have the
 * format dlb_<lower_name>_args.
 */
#define DLB_DOMAIN_CONFIGFS_CALLBACK_TEMPLATE(lower_name)		   \
static int dlb_domain_configfs_##lower_name(struct dlb *dlb,		   \
				   struct dlb_domain *domain,		   \
				   void *karg)				   \
{									   \
	struct dlb_cmd_response response = {0};				   \
	struct dlb_##lower_name##_args *arg = karg;			   \
	int ret;							   \
									   \
	mutex_lock(&dlb->resource_mutex);				   \
									   \
	ret = dlb_hw_##lower_name(&dlb->hw,				   \
				  domain->id,				   \
				  arg,					   \
				  &response);				   \
									   \
	mutex_unlock(&dlb->resource_mutex);				   \
									   \
	BUILD_BUG_ON(offsetof(typeof(*arg), response) != 0);		   \
									   \
	memcpy(karg, &response, sizeof(response));			   \
									   \
	return ret;							   \
}

DLB_DOMAIN_CONFIGFS_CALLBACK_TEMPLATE(create_ldb_queue)
DLB_DOMAIN_CONFIGFS_CALLBACK_TEMPLATE(create_dir_queue)
DLB_DOMAIN_CONFIGFS_CALLBACK_TEMPLATE(get_ldb_queue_depth)
DLB_DOMAIN_CONFIGFS_CALLBACK_TEMPLATE(get_dir_queue_depth)

static int dlb_create_port_fd(struct dlb *dlb,
			      const char *prefix,
			      u32 id,
			      const struct file_operations *fops,
			      int *fd,
			      struct file **f)
{
	char *name;
	int ret;

	ret = get_unused_fd_flags(O_RDWR);
	if (ret < 0)
		return ret;

	*fd = ret;

	name = kasprintf(GFP_KERNEL, "%s:%d", prefix, id);
	if (!name) {
		put_unused_fd(*fd);
		return -ENOMEM;
	}

	*f = dlb_getfile(dlb, O_RDWR | O_CLOEXEC, fops, name);

	kfree(name);

	if (IS_ERR(*f)) {
		put_unused_fd(*fd);
		return PTR_ERR(*f);
	}

	return 0;
}

static int dlb_domain_get_port_fd(struct dlb *dlb,
				  struct dlb_domain *domain,
				  u32 port_id,
				  int *fd,
				  const char *name,
				  const struct file_operations *fops,
				  bool is_ldb)
{
	struct dlb_port *port;
	struct file *file;
	int ret;

	if (is_ldb && dlb_ldb_port_owned_by_domain(&dlb->hw, domain->id,
						   port_id) != 1) {
		ret = -EINVAL;
		goto end;
	}

	if (!is_ldb && dlb_dir_port_owned_by_domain(&dlb->hw, domain->id,
						    port_id) != 1) {
		ret = -EINVAL;
		goto end;
	}

	port = (is_ldb) ? &dlb->ldb_port[port_id] : &dlb->dir_port[port_id];

	if (!port->valid) {
		ret = -EINVAL;
		goto end;
	}

	ret = dlb_create_port_fd(dlb, name, port_id, fops, fd, &file);
	if (ret < 0)
		goto end;

	file->private_data = port;
end:
	/*
	 * Save fd_install() until after the last point of failure. The domain
	 * refcnt is decremented in the close callback.
	 */
	if (ret == 0) {
		kref_get(&domain->refcnt);

		fd_install(*fd, file);
	}

	return ret;
}

static int dlb_domain_configfs_create_ldb_port(struct dlb *dlb,
					       struct dlb_domain *domain,
					       void *karg)
{
	struct dlb_cmd_response response = {0};
	struct dlb_create_ldb_port_args *arg = karg;
	dma_addr_t cq_dma_base = 0;
	void *cq_base;
	int ret;

	mutex_lock(&dlb->resource_mutex);

	cq_base = dma_alloc_coherent(&dlb->pdev->dev,
				     DLB_CQ_SIZE,
				     &cq_dma_base,
				     GFP_KERNEL);
	if (!cq_base) {
		response.status = DLB_ST_NO_MEMORY;
		ret = -ENOMEM;
		goto unlock;
	}

	ret = dlb_hw_create_ldb_port(&dlb->hw, domain->id,
				     arg, (uintptr_t)cq_dma_base,
				     &response);
	if (ret)
		goto unlock;

	/* Fill out the per-port data structure */
	dlb->ldb_port[response.id].id = response.id;
	dlb->ldb_port[response.id].is_ldb = true;
	dlb->ldb_port[response.id].domain = domain;
	dlb->ldb_port[response.id].cq_base = cq_base;
	dlb->ldb_port[response.id].cq_dma_base = cq_dma_base;
	dlb->ldb_port[response.id].valid = true;

unlock:
	if (ret && cq_dma_base)
		dma_free_coherent(&dlb->pdev->dev,
				  DLB_CQ_SIZE,
				  cq_base,
				  cq_dma_base);

	mutex_unlock(&dlb->resource_mutex);

	BUILD_BUG_ON(offsetof(typeof(*arg), response) != 0);

	memcpy(karg, &response, sizeof(response));

	return ret;
}

static int dlb_domain_configfs_create_dir_port(struct dlb *dlb,
					       struct dlb_domain *domain,
					       void *karg)
{
	struct dlb_cmd_response response = {0};
	struct dlb_create_dir_port_args *arg = karg;
	dma_addr_t cq_dma_base = 0;
	void *cq_base;
	int ret;

	mutex_lock(&dlb->resource_mutex);

	cq_base = dma_alloc_coherent(&dlb->pdev->dev,
				     DLB_CQ_SIZE,
				     &cq_dma_base,
				     GFP_KERNEL);
	if (!cq_base) {
		response.status = DLB_ST_NO_MEMORY;
		ret = -ENOMEM;
		goto unlock;
	}

	ret = dlb_hw_create_dir_port(&dlb->hw, domain->id,
				     arg, (uintptr_t)cq_dma_base,
				     &response);
	if (ret)
		goto unlock;

	/* Fill out the per-port data structure */
	dlb->dir_port[response.id].id = response.id;
	dlb->dir_port[response.id].is_ldb = false;
	dlb->dir_port[response.id].domain = domain;
	dlb->dir_port[response.id].cq_base = cq_base;
	dlb->dir_port[response.id].cq_dma_base = cq_dma_base;
	dlb->dir_port[response.id].valid = true;

unlock:
	if (ret && cq_dma_base)
		dma_free_coherent(&dlb->pdev->dev,
				  DLB_CQ_SIZE,
				  cq_base,
				  cq_dma_base);

	mutex_unlock(&dlb->resource_mutex);

	BUILD_BUG_ON(offsetof(typeof(*arg), response) != 0);

	memcpy(karg, &response, sizeof(response));

	return ret;
}

static int dlb_configfs_create_sched_domain(struct dlb *dlb,
					    void *karg)
{
	struct dlb_create_sched_domain_args *arg = karg;
	struct dlb_cmd_response response = {0};
	struct dlb_domain *domain;
	u32 flags = O_RDONLY;
	int ret, fd;

	mutex_lock(&dlb->resource_mutex);

	if (dlb->domain_reset_failed) {
		response.status = DLB_ST_DOMAIN_RESET_FAILED;
		ret = -EINVAL;
		goto unlock;
	}

	ret = dlb_hw_create_sched_domain(&dlb->hw, arg, &response);
	if (ret)
		goto unlock;

	ret = dlb_init_domain(dlb, response.id);
	if (ret) {
		dlb_reset_domain(&dlb->hw, response.id);
		goto unlock;
	}

	domain = dlb->sched_domains[response.id];

	if (dlb->f->f_mode & FMODE_WRITE)
		flags = O_RDWR;

	fd = anon_inode_getfd("[dlbdomain]", &dlb_domain_fops,
			      domain, flags);

	if (fd < 0) {
		dev_err(dlb->dev, "Failed to get anon fd.\n");
		kref_put(&domain->refcnt, dlb_free_domain);
		ret = fd;
		goto unlock;
	}

	arg->domain_fd = fd;

unlock:
	mutex_unlock(&dlb->resource_mutex);

	memcpy(karg, &response, sizeof(response));

	return ret;
}

/*
 * Reset the file descriptors for the producer port and consumer queue. Used
 * a port is closed.
 *
 */
int dlb_configfs_reset_port_fd(struct dlb *dlb,
			       struct dlb_domain *dlb_domain,
			       int port_id)
{
	struct dlb_cfs_port *dlb_cfs_port;

	dlb_cfs_port = dlb_configfs_get_port_from_id(dlb, dlb_domain, port_id);

	if (!dlb_cfs_port)
		return -EINVAL;

	dlb_cfs_port->pp_fd = 0xffffffff;
	dlb_cfs_port->cq_fd = 0xffffffff;

	return 0;
}

/*
 * Configfs directory structure for dlb driver implementation:
 *
 *                             config
 *                                |
 *                               dlb
 *                                |
 *                        +------+------+------+------
 *                        |      |      |      |
 *                       dlb0   dlb1   dlb2   dlb3  ...
 *                        |
 *                +-----------+--+--------+-------
 *                |           |           |
 *             domain0     domain1     domain2   ...
 *                |
 *        +-------+-----+------------+---------------+------------+----------
 *        |             |            |               |            |
 * num_ldb_queues     port0         port1   ...    queue0       queue1   ...
 * num_ldb_ports        |		             |
 * ...                is_ldb                   num_sequence_numbers
 * create             cq_depth                 num_qid_inflights
 * start              ...                      num_atomic_iflights
 *                    enable                   ...
 *                    ...
 */

/*
 * ------ Configfs for dlb queues ---------
 *
 * These are the templates for show and store functions in queue
 * groups/directories, which minimizes replication of boilerplate
 * code to copy arguments. All attributes, except for "create" store,
 * use the template. "name" is the attribute name in the group.
 */
#define DLB_CONFIGFS_QUEUE_SHOW(name)				\
static ssize_t dlb_cfs_queue_##name##_show(			\
	struct config_item *item,				\
	char *page)						\
{								\
	return sprintf(page, "%u\n",				\
	       to_dlb_cfs_queue(item)->name);			\
}								\

#define DLB_CONFIGFS_QUEUE_STORE(name)				\
static ssize_t dlb_cfs_queue_##name##_store(			\
	struct config_item *item,				\
	const char *page,					\
	size_t count)						\
{								\
	struct dlb_cfs_queue *dlb_cfs_queue =			\
				to_dlb_cfs_queue(item);		\
	int ret;						\
								\
	ret = kstrtoint(page, 10,				\
			 &dlb_cfs_queue->name);			\
	if (ret)						\
		return ret;					\
								\
	return count;						\
}								\

static ssize_t dlb_cfs_queue_queue_depth_show(struct config_item *item,
					      char *page)
{
	struct dlb_cfs_queue *dlb_cfs_queue = to_dlb_cfs_queue(item);
	struct dlb_domain *dlb_domain;
	struct dlb *dlb = NULL;
	int ret;

	ret = dlb_configfs_get_dlb_domain(dlb_cfs_queue->domain_grp,
					  &dlb, &dlb_domain);
	if (ret)
		return ret;

	if (dlb_cfs_queue->is_ldb) {
		struct dlb_get_ldb_queue_depth_args args = {0};

		args.queue_id = dlb_cfs_queue->queue_id;
		ret = dlb_domain_configfs_get_ldb_queue_depth(dlb,
						dlb_domain, &args);

		dlb_cfs_queue->status = args.response.status;
		dlb_cfs_queue->queue_depth = args.response.id;
	} else {
		struct dlb_get_dir_queue_depth_args args = {0};

		args.queue_id = dlb_cfs_queue->queue_id;
		ret = dlb_domain_configfs_get_dir_queue_depth(dlb,
						dlb_domain, &args);

		dlb_cfs_queue->status = args.response.status;
		dlb_cfs_queue->queue_depth = args.response.id;
	}

	if (ret) {
		dev_err(dlb->dev,
			"Getting queue depth failed: ret=%d\n", ret);
		return ret;
	}

	return sprintf(page, "%u\n", dlb_cfs_queue->queue_depth);
}

DLB_CONFIGFS_QUEUE_SHOW(status)
DLB_CONFIGFS_QUEUE_SHOW(queue_id)
DLB_CONFIGFS_QUEUE_SHOW(is_ldb)
DLB_CONFIGFS_QUEUE_SHOW(depth_threshold)
DLB_CONFIGFS_QUEUE_SHOW(num_sequence_numbers)
DLB_CONFIGFS_QUEUE_SHOW(num_qid_inflights)
DLB_CONFIGFS_QUEUE_SHOW(num_atomic_inflights)
DLB_CONFIGFS_QUEUE_SHOW(lock_id_comp_level)
DLB_CONFIGFS_QUEUE_SHOW(port_id)
DLB_CONFIGFS_QUEUE_SHOW(create)

DLB_CONFIGFS_QUEUE_STORE(is_ldb)
DLB_CONFIGFS_QUEUE_STORE(depth_threshold)
DLB_CONFIGFS_QUEUE_STORE(num_sequence_numbers)
DLB_CONFIGFS_QUEUE_STORE(num_qid_inflights)
DLB_CONFIGFS_QUEUE_STORE(num_atomic_inflights)
DLB_CONFIGFS_QUEUE_STORE(lock_id_comp_level)
DLB_CONFIGFS_QUEUE_STORE(port_id)

static ssize_t dlb_cfs_queue_create_store(struct config_item *item,
					  const char *page, size_t count)
{
	struct dlb_cfs_queue *dlb_cfs_queue = to_dlb_cfs_queue(item);
	struct dlb_domain *dlb_domain;
	struct dlb *dlb = NULL;
	int ret;

	ret = dlb_configfs_get_dlb_domain(dlb_cfs_queue->domain_grp,
					  &dlb, &dlb_domain);
	if (ret)
		return ret;

	ret = kstrtoint(page, 10, &dlb_cfs_queue->create);
	if (ret)
		return ret;

	if (dlb_cfs_queue->create == 0)  /* ToDo ? */
		return count;

	if (dlb_cfs_queue->is_ldb) {
		struct dlb_create_ldb_queue_args args = {0};

		args.num_sequence_numbers = dlb_cfs_queue->num_sequence_numbers;
		args.num_qid_inflights = dlb_cfs_queue->num_qid_inflights;
		args.num_atomic_inflights = dlb_cfs_queue->num_atomic_inflights;
		args.lock_id_comp_level = dlb_cfs_queue->lock_id_comp_level;
		args.depth_threshold = dlb_cfs_queue->depth_threshold;

		dev_dbg(dlb->dev,
			"Creating ldb queue: %s\n",
			dlb_cfs_queue->group.cg_item.ci_namebuf);

		ret = dlb_domain_configfs_create_ldb_queue(dlb, dlb_domain, &args);

		dlb_cfs_queue->status = args.response.status;
		dlb_cfs_queue->queue_id = args.response.id;
	} else {
		struct dlb_create_dir_queue_args args = {0};

		args.port_id = dlb_cfs_queue->port_id;
		args.depth_threshold = dlb_cfs_queue->depth_threshold;

		dev_dbg(dlb->dev,
			"Creating ldb queue: %s\n",
			dlb_cfs_queue->group.cg_item.ci_namebuf);

		ret = dlb_domain_configfs_create_dir_queue(dlb, dlb_domain, &args);

		dlb_cfs_queue->status = args.response.status;
		dlb_cfs_queue->queue_id = args.response.id;
	}

	if (ret) {
		dev_err(dlb->dev,
			"create queue() failed: ret=%d is_ldb=%u\n", ret,
			dlb_cfs_queue->is_ldb);
		return ret;
	}

	return count;
}

CONFIGFS_ATTR_RO(dlb_cfs_queue_, status);
CONFIGFS_ATTR_RO(dlb_cfs_queue_, queue_id);
CONFIGFS_ATTR_RO(dlb_cfs_queue_, queue_depth);
CONFIGFS_ATTR(dlb_cfs_queue_, is_ldb);
CONFIGFS_ATTR(dlb_cfs_queue_, depth_threshold);
CONFIGFS_ATTR(dlb_cfs_queue_, num_sequence_numbers);
CONFIGFS_ATTR(dlb_cfs_queue_, num_qid_inflights);
CONFIGFS_ATTR(dlb_cfs_queue_, num_atomic_inflights);
CONFIGFS_ATTR(dlb_cfs_queue_, lock_id_comp_level);
CONFIGFS_ATTR(dlb_cfs_queue_, port_id);
CONFIGFS_ATTR(dlb_cfs_queue_, create);

static struct configfs_attribute *dlb_cfs_queue_attrs[] = {
	&dlb_cfs_queue_attr_status,
	&dlb_cfs_queue_attr_queue_id,
	&dlb_cfs_queue_attr_queue_depth,
	&dlb_cfs_queue_attr_is_ldb,
	&dlb_cfs_queue_attr_depth_threshold,
	&dlb_cfs_queue_attr_num_sequence_numbers,
	&dlb_cfs_queue_attr_num_qid_inflights,
	&dlb_cfs_queue_attr_num_atomic_inflights,
	&dlb_cfs_queue_attr_lock_id_comp_level,
	&dlb_cfs_queue_attr_port_id,
	&dlb_cfs_queue_attr_create,

	NULL,
};

static void dlb_cfs_queue_release(struct config_item *item)
{
	kfree(to_dlb_cfs_queue(item));
}

static struct configfs_item_operations dlb_cfs_queue_item_ops = {
	.release	= dlb_cfs_queue_release,
};

/*
 * Note that, since no extra work is required on ->drop_item(),
 * no ->drop_item() is provided. no _group_ops either because we
 * don't need to create any groups or items in queue configfs.
 */
static const struct config_item_type dlb_cfs_queue_type = {
	.ct_item_ops	= &dlb_cfs_queue_item_ops,
	.ct_attrs	= dlb_cfs_queue_attrs,
	.ct_owner	= THIS_MODULE,
};

/*
 * ------ Configfs for dlb ports ---------
 *
 * These are the templates for show and store functions in port
 * groups/directories, which minimizes replication of boilerplate
 * code to copy arguments. Most attributes, use the simple template.
 * "name" is the attribute name in the group.
 */
#define DLB_CONFIGFS_PORT_SHOW(name)					\
static ssize_t dlb_cfs_port_##name##_show(				\
	struct config_item *item,					\
	char *page)							\
{									\
	return sprintf(page, "%u\n", to_dlb_cfs_port(item)->name);	\
}									\

#define DLB_CONFIGFS_PORT_SHOW64(name)					\
static ssize_t dlb_cfs_port_##name##_show(				\
	struct config_item *item,					\
	char *page)							\
{									\
	return sprintf(page, "%llx\n", to_dlb_cfs_port(item)->name);	\
}									\

#define DLB_CONFIGFS_PORT_SHOW_FD(name)					\
static ssize_t dlb_cfs_port_##name##_show(				\
	struct config_item *item,					\
	char *page)							\
{									\
	struct dlb_cfs_port *dlb_cfs_port = to_dlb_cfs_port(item);	\
	char filename[16], prefix[16];					\
	struct dlb_domain *domain;					\
	struct dlb *dlb = NULL;						\
	int port_id, is_ldb;						\
	int fd, ret;							\
									\
	if (to_dlb_cfs_port(item)->name != 0xffffffff)			\
		goto end;						\
									\
	ret = dlb_configfs_get_dlb_domain(dlb_cfs_port->domain_grp,	\
					    &dlb, &domain);		\
	if (ret)							\
		return ret;						\
									\
	port_id = dlb_cfs_port->port_id;				\
	is_ldb = dlb_cfs_port->is_ldb;					\
									\
	if (is_ldb)							\
		sprintf(filename, "dlb_ldb");				\
	else								\
		sprintf(filename, "dlb_dir");				\
									\
	if (!strcmp(#name, "pp_fd")) {					\
		sprintf(prefix, "%s_pp:", filename);			\
		ret = dlb_domain_get_port_fd(dlb, domain, port_id,	\
				&fd, prefix, &dlb_pp_fops, is_ldb);	\
		dlb_cfs_port->pp_fd = fd;				\
	} else {							\
		sprintf(prefix, "%s_cq:", filename);			\
		ret = dlb_domain_get_port_fd(dlb, domain, port_id,	\
				&fd, prefix, &dlb_cq_fops, is_ldb);	\
		dlb_cfs_port->cq_fd = fd;				\
	}								\
									\
	if (ret)							\
		return ret;						\
end:									\
	return sprintf(page, "%u\n", to_dlb_cfs_port(item)->name);	\
}									\

#define DLB_CONFIGFS_PORT_STORE(name)					\
static ssize_t dlb_cfs_port_##name##_store(				\
	struct config_item *item,					\
	const char *page,						\
	size_t count)							\
{									\
	struct dlb_cfs_port *dlb_cfs_port = to_dlb_cfs_port(item);	\
	int ret;							\
									\
	ret = kstrtoint(page, 10, &dlb_cfs_port->name);			\
	if (ret)							\
		return ret;						\
									\
	return count;							\
}									\

#define DLB_CONFIGFS_PORT_STORE64(name)					\
static ssize_t dlb_cfs_port_##name##_store(				\
	struct config_item *item,					\
	const char *page,						\
	size_t count)							\
{									\
	int ret;							\
	struct dlb_cfs_port *dlb_cfs_port = to_dlb_cfs_port(item);	\
									\
	ret = kstrtoll(page, 16, &dlb_cfs_port->name);			\
	if (ret)							\
		return ret;						\
									\
	return count;							\
}									\

DLB_CONFIGFS_PORT_SHOW_FD(pp_fd)
DLB_CONFIGFS_PORT_SHOW_FD(cq_fd)
DLB_CONFIGFS_PORT_SHOW(status)
DLB_CONFIGFS_PORT_SHOW(port_id)
DLB_CONFIGFS_PORT_SHOW(is_ldb)
DLB_CONFIGFS_PORT_SHOW(cq_depth)
DLB_CONFIGFS_PORT_SHOW(cq_depth_threshold)
DLB_CONFIGFS_PORT_SHOW(cq_history_list_size)
DLB_CONFIGFS_PORT_SHOW(create)
DLB_CONFIGFS_PORT_SHOW(queue_id)

DLB_CONFIGFS_PORT_STORE(is_ldb)
DLB_CONFIGFS_PORT_STORE(cq_depth)
DLB_CONFIGFS_PORT_STORE(cq_depth_threshold)
DLB_CONFIGFS_PORT_STORE(cq_history_list_size)
DLB_CONFIGFS_PORT_STORE(queue_id)

static ssize_t dlb_cfs_port_create_store(struct config_item *item,
					 const char *page, size_t count)
{
	struct dlb_cfs_port *dlb_cfs_port = to_dlb_cfs_port(item);
	struct dlb_domain *dlb_domain;
	struct dlb *dlb = NULL;
	int ret;

	ret = dlb_configfs_get_dlb_domain(dlb_cfs_port->domain_grp,
					  &dlb, &dlb_domain);
	if (ret)
		return ret;

	ret = kstrtoint(page, 10, &dlb_cfs_port->create);
	if (ret)
		return ret;

	if (dlb_cfs_port->create == 0)
		return count;

	if (dlb_cfs_port->is_ldb) {
		struct dlb_create_ldb_port_args args = {0};

		args.cq_depth = dlb_cfs_port->cq_depth;
		args.cq_depth_threshold = dlb_cfs_port->cq_depth_threshold;
		args.cq_history_list_size = dlb_cfs_port->cq_history_list_size;

		dev_dbg(dlb->dev,
			"Creating ldb port: %s\n",
			dlb_cfs_port->group.cg_item.ci_namebuf);

		ret = dlb_domain_configfs_create_ldb_port(dlb, dlb_domain, &args);

		dlb_cfs_port->status = args.response.status;
		dlb_cfs_port->port_id = args.response.id;
	} else {
		struct dlb_create_dir_port_args args = {0};

		args.queue_id = dlb_cfs_port->queue_id;
		args.cq_depth = dlb_cfs_port->cq_depth;
		args.cq_depth_threshold = dlb_cfs_port->cq_depth_threshold;

		dev_dbg(dlb->dev,
			"Creating dir port: %s\n",
			dlb_cfs_port->group.cg_item.ci_namebuf);

		ret = dlb_domain_configfs_create_dir_port(dlb, dlb_domain, &args);

		dlb_cfs_port->status = args.response.status;
		dlb_cfs_port->port_id = args.response.id;
	}

	dlb_cfs_port->pp_fd = 0xffffffff;
	dlb_cfs_port->cq_fd = 0xffffffff;

	if (ret) {
		dev_err(dlb->dev,
			"creat port %s failed: ret=%d\n",
			dlb_cfs_port->group.cg_item.ci_namebuf, ret);
		return ret;
	}

	return count;
}

CONFIGFS_ATTR_RO(dlb_cfs_port_, pp_fd);
CONFIGFS_ATTR_RO(dlb_cfs_port_, cq_fd);
CONFIGFS_ATTR_RO(dlb_cfs_port_, status);
CONFIGFS_ATTR_RO(dlb_cfs_port_, port_id);
CONFIGFS_ATTR(dlb_cfs_port_, is_ldb);
CONFIGFS_ATTR(dlb_cfs_port_, cq_depth);
CONFIGFS_ATTR(dlb_cfs_port_, cq_depth_threshold);
CONFIGFS_ATTR(dlb_cfs_port_, cq_history_list_size);
CONFIGFS_ATTR(dlb_cfs_port_, create);
CONFIGFS_ATTR(dlb_cfs_port_, queue_id);

static struct configfs_attribute *dlb_cfs_port_attrs[] = {
	&dlb_cfs_port_attr_pp_fd,
	&dlb_cfs_port_attr_cq_fd,
	&dlb_cfs_port_attr_status,
	&dlb_cfs_port_attr_port_id,
	&dlb_cfs_port_attr_is_ldb,
	&dlb_cfs_port_attr_cq_depth,
	&dlb_cfs_port_attr_cq_depth_threshold,
	&dlb_cfs_port_attr_cq_history_list_size,
	&dlb_cfs_port_attr_create,
	&dlb_cfs_port_attr_queue_id,

	NULL,
};

static void dlb_cfs_port_release(struct config_item *item)
{
	kfree(to_dlb_cfs_port(item));
}

static struct configfs_item_operations dlb_cfs_port_item_ops = {
	.release	= dlb_cfs_port_release,
};

/*
 * Note that, since no extra work is required on ->drop_item(),
 * no ->drop_item() is provided.
 */
static const struct config_item_type dlb_cfs_port_type = {
	.ct_item_ops	= &dlb_cfs_port_item_ops,
	.ct_attrs	= dlb_cfs_port_attrs,
	.ct_owner	= THIS_MODULE,
};

/*
 * ------ Configfs for dlb domains---------
 *
 * These are the templates for show and store functions in domain
 * groups/directories, which minimizes replication of boilerplate
 * code to copy arguments. Most attributes, use the simple template.
 * "name" is the attribute name in the group.
 */
#define DLB_CONFIGFS_DOMAIN_SHOW(name)				\
static ssize_t dlb_cfs_domain_##name##_show(			\
	struct config_item *item,				\
	char *page)						\
{								\
	return sprintf(page, "%u\n",				\
		       to_dlb_cfs_domain(item)->name);		\
}								\

#define DLB_CONFIGFS_DOMAIN_STORE(name)				\
static ssize_t dlb_cfs_domain_##name##_store(			\
	struct config_item *item,				\
	const char *page,					\
	size_t count)						\
{								\
	int ret;						\
	struct dlb_cfs_domain *dlb_cfs_domain =			\
				to_dlb_cfs_domain(item);	\
								\
	ret = kstrtoint(page, 10, &dlb_cfs_domain->name);	\
	if (ret)						\
		return ret;					\
								\
	return count;						\
}								\

DLB_CONFIGFS_DOMAIN_SHOW(domain_fd)
DLB_CONFIGFS_DOMAIN_SHOW(status)
DLB_CONFIGFS_DOMAIN_SHOW(domain_id)
DLB_CONFIGFS_DOMAIN_SHOW(num_ldb_queues)
DLB_CONFIGFS_DOMAIN_SHOW(num_ldb_ports)
DLB_CONFIGFS_DOMAIN_SHOW(num_dir_ports)
DLB_CONFIGFS_DOMAIN_SHOW(num_atomic_inflights)
DLB_CONFIGFS_DOMAIN_SHOW(num_hist_list_entries)
DLB_CONFIGFS_DOMAIN_SHOW(num_ldb_credits)
DLB_CONFIGFS_DOMAIN_SHOW(num_dir_credits)
DLB_CONFIGFS_DOMAIN_SHOW(create)

DLB_CONFIGFS_DOMAIN_STORE(num_ldb_queues)
DLB_CONFIGFS_DOMAIN_STORE(num_ldb_ports)
DLB_CONFIGFS_DOMAIN_STORE(num_dir_ports)
DLB_CONFIGFS_DOMAIN_STORE(num_atomic_inflights)
DLB_CONFIGFS_DOMAIN_STORE(num_hist_list_entries)
DLB_CONFIGFS_DOMAIN_STORE(num_ldb_credits)
DLB_CONFIGFS_DOMAIN_STORE(num_dir_credits)

static ssize_t dlb_cfs_domain_create_store(struct config_item *item,
					   const char *page, size_t count)
{
	struct dlb_cfs_domain *dlb_cfs_domain = to_dlb_cfs_domain(item);
	struct dlb_device_configfs *dlb_dev_configfs;
	struct dlb *dlb;
	int ret, create_in;

	dlb_dev_configfs = container_of(dlb_cfs_domain->dev_grp,
					struct dlb_device_configfs,
					dev_group);
	dlb = dlb_dev_configfs->dlb;
	if (!dlb)
		return -EINVAL;

	ret = kstrtoint(page, 10, &create_in);
	if (ret)
		return ret;

	/* Writing 1 to the 'create' triggers scheduling domain creation */
	if (create_in == 1 && dlb_cfs_domain->create == 0) {
		struct dlb_create_sched_domain_args args = {0};

		memcpy(&args.response, &dlb_cfs_domain->status,
		       sizeof(struct dlb_create_sched_domain_args));

		dev_dbg(dlb->dev,
			"Create domain: %s\n",
			dlb_cfs_domain->group.cg_item.ci_namebuf);

		ret = dlb_configfs_create_sched_domain(dlb, &args);

		dlb_cfs_domain->status = args.response.status;
		dlb_cfs_domain->domain_id = args.response.id;
		dlb_cfs_domain->domain_fd = args.domain_fd;

		if (ret) {
			dev_err(dlb->dev,
				"create sched domain failed: ret=%d\n", ret);
			return ret;
		}

		dlb_cfs_domain->create = 1;
	} else if (create_in == 0 && dlb_cfs_domain->create == 1) {
		dev_dbg(dlb->dev,
			"Close domain: %s\n",
			dlb_cfs_domain->group.cg_item.ci_namebuf);

		ret = close_fd(dlb_cfs_domain->domain_fd);
		if (ret)
			dev_err(dlb->dev,
				"close sched domain failed: ret=%d\n", ret);

		dlb_cfs_domain->create = 0;
	}

	return count;
}

CONFIGFS_ATTR_RO(dlb_cfs_domain_, domain_fd);
CONFIGFS_ATTR_RO(dlb_cfs_domain_, status);
CONFIGFS_ATTR_RO(dlb_cfs_domain_, domain_id);
CONFIGFS_ATTR(dlb_cfs_domain_, num_ldb_queues);
CONFIGFS_ATTR(dlb_cfs_domain_, num_ldb_ports);
CONFIGFS_ATTR(dlb_cfs_domain_, num_dir_ports);
CONFIGFS_ATTR(dlb_cfs_domain_, num_atomic_inflights);
CONFIGFS_ATTR(dlb_cfs_domain_, num_hist_list_entries);
CONFIGFS_ATTR(dlb_cfs_domain_, num_ldb_credits);
CONFIGFS_ATTR(dlb_cfs_domain_, num_dir_credits);
CONFIGFS_ATTR(dlb_cfs_domain_, create);

static struct configfs_attribute *dlb_cfs_domain_attrs[] = {
	&dlb_cfs_domain_attr_domain_fd,
	&dlb_cfs_domain_attr_status,
	&dlb_cfs_domain_attr_domain_id,
	&dlb_cfs_domain_attr_num_ldb_queues,
	&dlb_cfs_domain_attr_num_ldb_ports,
	&dlb_cfs_domain_attr_num_dir_ports,
	&dlb_cfs_domain_attr_num_atomic_inflights,
	&dlb_cfs_domain_attr_num_hist_list_entries,
	&dlb_cfs_domain_attr_num_ldb_credits,
	&dlb_cfs_domain_attr_num_dir_credits,
	&dlb_cfs_domain_attr_create,

	NULL,
};

static struct config_group *dlb_cfs_domain_make_queue_port(struct config_group *group,
							   const char *name)
{
	if (strstr(name, "port")) {
		struct dlb_cfs_port *dlb_cfs_port;

		dlb_cfs_port = kzalloc(sizeof(*dlb_cfs_port), GFP_KERNEL);
		if (!dlb_cfs_port)
			return ERR_PTR(-ENOMEM);

		dlb_cfs_port->domain_grp = group;

		config_group_init_type_name(&dlb_cfs_port->group, name,
					    &dlb_cfs_port_type);

		dlb_cfs_port->queue_id = 0xffffffff;
		dlb_cfs_port->port_id = 0xffffffff;

		return &dlb_cfs_port->group;
	} else if (strstr(name, "queue")) {
		struct dlb_cfs_queue *dlb_cfs_queue;

		dlb_cfs_queue = kzalloc(sizeof(*dlb_cfs_queue), GFP_KERNEL);
		if (!dlb_cfs_queue)
			return ERR_PTR(-ENOMEM);

		dlb_cfs_queue->domain_grp = group;

		config_group_init_type_name(&dlb_cfs_queue->group, name,
					    &dlb_cfs_queue_type);

		dlb_cfs_queue->queue_id = 0xffffffff;
		dlb_cfs_queue->port_id = 0xffffffff;

		return &dlb_cfs_queue->group;
	}

	return ERR_PTR(-EINVAL);
}

static void dlb_cfs_domain_release(struct config_item *item)
{
	kfree(to_dlb_cfs_domain(item));
}

static struct configfs_item_operations dlb_cfs_domain_item_ops = {
	.release	= dlb_cfs_domain_release,
};

/*
 * Note that, since no extra work is required on ->drop_item(),
 * no ->drop_item() is provided.
 */
static struct configfs_group_operations dlb_cfs_domain_group_ops = {
	.make_group     = dlb_cfs_domain_make_queue_port,
};

static const struct config_item_type dlb_cfs_domain_type = {
	.ct_item_ops	= &dlb_cfs_domain_item_ops,
	.ct_group_ops	= &dlb_cfs_domain_group_ops,
	.ct_attrs	= dlb_cfs_domain_attrs,
	.ct_owner	= THIS_MODULE,
};

/*
 *--------- dlb device level configfs -----------
 *
 * Scheduling domains are created in the device-level configfs driectory.
 */
static struct config_group *dlb_cfs_device_make_domain(struct config_group *group,
						       const char *name)
{
	struct dlb_cfs_domain *dlb_cfs_domain;

	dlb_cfs_domain = kzalloc(sizeof(*dlb_cfs_domain), GFP_KERNEL);
	if (!dlb_cfs_domain)
		return ERR_PTR(-ENOMEM);

	dlb_cfs_domain->dev_grp = group;

	config_group_init_type_name(&dlb_cfs_domain->group, name,
				    &dlb_cfs_domain_type);

	return &dlb_cfs_domain->group;
}

static struct configfs_group_operations dlb_cfs_device_group_ops = {
	.make_group     = dlb_cfs_device_make_domain,
};

static const struct config_item_type dlb_cfs_device_type = {
	/* No need for _item_ops() at the device-level, and default
	 * attribute.
	 * .ct_item_ops	= &dlb_cfs_device_item_ops,
	 * .ct_attrs	= dlb_cfs_device_attrs,
	 */

	.ct_group_ops	= &dlb_cfs_device_group_ops,
	.ct_owner	= THIS_MODULE,
};

/*------------------- dlb group subsystem for configfs ----------------
 *
 * we only need a simple configfs item type here that does not let
 * user to create new entry. The group for each dlb device will be
 * generated when the device is detected in dlb_probe().
 */

static const struct config_item_type dlb_device_group_type = {
	.ct_owner	= THIS_MODULE,
};

/* dlb group subsys in configfs */
static struct configfs_subsystem dlb_device_group_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "dlb",
			.ci_type = &dlb_device_group_type,
		},
	},
};

/* Create a configfs directory dlbN for each dlb device probed
 * in dlb_probe()
 */
int dlb_configfs_create_device(struct dlb *dlb)
{
	struct config_group *parent_group, *dev_grp;
	char device_name[16];
	int ret = 0;

	snprintf(device_name, 6, "dlb%d", dlb->id);
	parent_group = &dlb_device_group_subsys.su_group;

	dev_grp = &dlb_dev_configfs[dlb->id].dev_group;
	config_group_init_type_name(dev_grp,
				    device_name,
				    &dlb_cfs_device_type);
	ret = configfs_register_group(parent_group, dev_grp);

	if (ret)
		return ret;

	dlb_dev_configfs[dlb->id].dlb = dlb;

	return ret;
}

int configfs_dlb_init(void)
{
	struct configfs_subsystem *subsys;
	int ret;

	/* set up and register configfs subsystem for dlb */
	subsys = &dlb_device_group_subsys;
	config_group_init(&subsys->su_group);
	mutex_init(&subsys->su_mutex);
	ret = configfs_register_subsystem(subsys);
	if (ret) {
		pr_err("Error %d while registering subsystem %s\n",
		       ret, subsys->su_group.cg_item.ci_namebuf);
	}

	return ret;
}

void configfs_dlb_exit(void)
{
	configfs_unregister_subsystem(&dlb_device_group_subsys);
}
