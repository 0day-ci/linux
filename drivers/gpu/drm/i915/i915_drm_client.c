// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "i915_drm_client.h"
#include "i915_gem.h"
#include "i915_utils.h"

void i915_drm_clients_init(struct i915_drm_clients *clients,
			   struct drm_i915_private *i915)
{
	clients->i915 = i915;

	clients->next_id = 0;
	xa_init_flags(&clients->xarray, XA_FLAGS_ALLOC);
}

static int
__i915_drm_client_register(struct i915_drm_client *client,
			   struct task_struct *task)
{
	char *name;

	name = kstrdup(task->comm, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	client->pid = get_task_pid(task, PIDTYPE_PID);
	client->name = name;

	return 0;
}

static void __i915_drm_client_unregister(struct i915_drm_client *client)
{
	put_pid(fetch_and_zero(&client->pid));
	kfree(fetch_and_zero(&client->name));
}

static void __rcu_i915_drm_client_free(struct work_struct *wrk)
{
	struct i915_drm_client *client =
		container_of(wrk, typeof(*client), rcu.work);

	xa_erase(&client->clients->xarray, client->id);

	__i915_drm_client_unregister(client);

	kfree(client);
}

struct i915_drm_client *
i915_drm_client_add(struct i915_drm_clients *clients, struct task_struct *task)
{
	struct i915_drm_client *client;
	int ret;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	kref_init(&client->kref);
	client->clients = clients;
	INIT_RCU_WORK(&client->rcu, __rcu_i915_drm_client_free);

	ret = xa_alloc_cyclic(&clients->xarray, &client->id, client,
			      xa_limit_32b, &clients->next_id, GFP_KERNEL);
	if (ret < 0)
		goto err_id;

	ret = __i915_drm_client_register(client, task);
	if (ret)
		goto err_register;

	return client;

err_register:
	xa_erase(&clients->xarray, client->id);
err_id:
	kfree(client);

	return ERR_PTR(ret);
}

void __i915_drm_client_free(struct kref *kref)
{
	struct i915_drm_client *client =
		container_of(kref, typeof(*client), kref);

	queue_rcu_work(system_wq, &client->rcu);
}

void i915_drm_client_close(struct i915_drm_client *client)
{
	GEM_BUG_ON(READ_ONCE(client->closed));
	WRITE_ONCE(client->closed, true);
	i915_drm_client_put(client);
}

void i915_drm_clients_fini(struct i915_drm_clients *clients)
{
	while (!xa_empty(&clients->xarray)) {
		rcu_barrier();
		flush_workqueue(system_wq);
	}

	xa_destroy(&clients->xarray);
}
