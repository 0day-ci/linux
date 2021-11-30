// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Microsoft Corporation
 *
 * Author: Lakshmi Ramasubramanian (nramas@linux.microsoft.com)
 *
 * File: ima_queue_keys.c
 *       Enables deferred processing of keys
 */

#include <linux/user_namespace.h>
#include <linux/workqueue.h>
#include <linux/ima.h>
#include <keys/asymmetric-type.h>
#include "ima.h"


/*
 * This worker function frees keys that may still be
 * queued up in case custom IMA policy was not loaded.
 */
void ima_keys_handler(struct work_struct *work)
{
	struct ima_namespace *ns;

	ns = container_of(work, struct ima_namespace, ima_keys_delayed_work.work);
	ns->timer_expired = true;
	ima_process_queued_keys(ns);
}

/*
 * This function sets up a worker to free queued keys in case
 * custom IMA policy was never loaded.
 */
void ima_init_key_queue(struct ima_namespace *ns)
{
	schedule_delayed_work(&ns->ima_keys_delayed_work,
			      msecs_to_jiffies(ns->ima_key_queue_timeout));
}

static void ima_free_key_entry(struct ima_key_entry *entry)
{
	if (entry) {
		kfree(entry->payload);
		kfree(entry->keyring_name);
		kfree(entry);
	}
}

static struct ima_key_entry *ima_alloc_key_entry(struct key *keyring,
						 const void *payload,
						 size_t payload_len)
{
	int rc = 0;
	const char *audit_cause = "ENOMEM";
	struct ima_key_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry) {
		entry->payload = kmemdup(payload, payload_len, GFP_KERNEL);
		entry->keyring_name = kstrdup(keyring->description,
					      GFP_KERNEL);
		entry->payload_len = payload_len;
	}

	if ((entry == NULL) || (entry->payload == NULL) ||
	    (entry->keyring_name == NULL)) {
		rc = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&entry->list);

out:
	if (rc) {
		integrity_audit_message(AUDIT_INTEGRITY_PCR, NULL,
					keyring->description,
					func_measure_str(KEY_CHECK),
					audit_cause, rc, 0, rc);
		ima_free_key_entry(entry);
		entry = NULL;
	}

	return entry;
}

bool ima_queue_key(struct ima_namespace *ns, struct key *keyring, const void *payload,
		   size_t payload_len)
{
	bool queued = false;
	struct ima_key_entry *entry;

	entry = ima_alloc_key_entry(keyring, payload, payload_len);
	if (!entry)
		return false;

	mutex_lock(&ns->ima_keys_lock);
	if (!ns->ima_process_keys) {
		list_add_tail(&entry->list, &ns->ima_keys);
		queued = true;
	}
	mutex_unlock(&ns->ima_keys_lock);

	if (!queued)
		ima_free_key_entry(entry);

	return queued;
}

/*
 * ima_process_queued_keys() - process keys queued for measurement
 *
 * This function sets ima_process_keys to true and processes queued keys.
 * From here on keys will be processed right away (not queued).
 */
void ima_process_queued_keys(struct ima_namespace *ns)
{
	struct ima_key_entry *entry, *tmp;
	bool process = false;

	if (ns->ima_process_keys)
		return;

	/*
	 * Since ima_process_keys is set to true, any new key will be
	 * processed immediately and not be queued to ima_keys list.
	 * First one setting the ima_process_keys flag to true will
	 * process the queued keys.
	 */
	mutex_lock(&ns->ima_keys_lock);
	if (!ns->ima_process_keys) {
		ns->ima_process_keys = true;
		process = true;
	}
	mutex_unlock(&ns->ima_keys_lock);

	if (!process)
		return;

	if (!ns->timer_expired)
		cancel_delayed_work_sync(&ns->ima_keys_delayed_work);

	list_for_each_entry_safe(entry, tmp, &ns->ima_keys, list) {
		if (!ns->timer_expired)
			process_buffer_measurement(ns, &init_user_ns, NULL,
						   entry->payload,
						   entry->payload_len,
						   entry->keyring_name,
						   KEY_CHECK, 0,
						   entry->keyring_name,
						   false, NULL, 0);
		list_del(&entry->list);
		ima_free_key_entry(entry);
	}
}

inline bool ima_should_queue_key(struct ima_namespace *ns)
{
	return !ns->ima_process_keys;
}
