// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2021 Intel Corporation */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "hci_codec.h"

static int hci_codec_list_add(struct list_head *list,
			      struct hci_op_read_local_codec_caps *sent,
			      struct hci_rp_read_local_codec_caps *rp,
			      void *caps,
			      __u32 len)
{
	struct codec_list *entry;

	entry = kzalloc(sizeof(*entry) + len, GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->id = sent->id;
	if (sent->id == 0xFF) {
		entry->cid = __le16_to_cpu(sent->cid);
		entry->vid = __le16_to_cpu(sent->vid);
	}
	entry->transport = sent->transport;
	entry->len = len;
	entry->num_caps = 0;
	if (rp) {
		entry->num_caps = rp->num_caps;
		memcpy(entry->caps, caps, len);
	}
	list_add(&entry->list, list);

	return 0;
}

void hci_codec_list_clear(struct list_head *codec_list)
{
	struct codec_list *c, *n;

	list_for_each_entry_safe(c, n, codec_list, list) {
		list_del(&c->list);
		kfree(c);
	}
}

static void hci_read_codec_capabilities(struct hci_dev *hdev, __u8 transport,
					struct hci_op_read_local_codec_caps
					*cmd)
{
	__u8 i;

	for (i = 0; i < TRANSPORT_TYPE_MAX; i++) {
		if (transport & BIT(i)) {
			struct hci_rp_read_local_codec_caps *rp;
			struct hci_codec_caps *caps;
			struct sk_buff *skb;
			__u8 j;
			__u32 len;

			cmd->transport = i;

			/* If Read_Codec_Capabilities command is not supported
			 * then just add codec to the list without caps
			 */
			if (!(hdev->commands[45] & 0x08)) {
				hci_dev_lock(hdev);
				hci_codec_list_add(&hdev->local_codecs, cmd,
						   NULL, NULL, 0);
				hci_dev_unlock(hdev);
				continue;
			}

			skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_CODEC_CAPS,
					     sizeof(*cmd), cmd,
					     HCI_CMD_TIMEOUT);
			if (IS_ERR(skb)) {
				bt_dev_err(hdev, "Failed to read codec capabilities (%ld)",
					   PTR_ERR(skb));
				continue;
			}

			if (skb->len < sizeof(*rp))
				goto error;

			rp = (void *)skb->data;

			if (rp->status)
				goto error;

			if (!rp->num_caps) {
				len = 0;
				/* this codec doesn't have capabilities */
				goto skip_caps_parse;
			}

			skb_pull(skb, sizeof(*rp));

			for (j = 0, len = 0; j < rp->num_caps; j++) {
				caps = (void *)skb->data;
				if (skb->len < sizeof(*caps))
					goto error;
				if (skb->len < caps->len)
					goto error;
				len += sizeof(caps->len) + caps->len;
				skb_pull(skb,  sizeof(caps->len) + caps->len);
			}

skip_caps_parse:
			hci_dev_lock(hdev);
			hci_codec_list_add(&hdev->local_codecs, cmd, rp,
					   (__u8 *)rp + sizeof(*rp), len);
			hci_dev_unlock(hdev);
error:
			kfree_skb(skb);
		}
	}
}

void hci_read_supported_codecs(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct hci_rp_read_local_supported_codecs *rp;
	struct hci_std_codecs *std_codecs;
	struct hci_vnd_codecs *vnd_codecs;
	struct hci_op_read_local_codec_caps caps;
	__u8 i;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_CODECS, 0, NULL,
			     HCI_CMD_TIMEOUT);

	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Failed to read local supported codecs (%ld)",
			   PTR_ERR(skb));
		return;
	}

	if (skb->len < sizeof(*rp))
		goto error;

	rp = (void *)skb->data;

	if (rp->status)
		goto error;

	skb_pull(skb, sizeof(rp->status));

	std_codecs = (void *)skb->data;

	/* validate codecs length before accessing */
	if (skb->len < flex_array_size(std_codecs, codec, std_codecs->num)
	    + sizeof(std_codecs->num))
		goto error;

	/* enumerate codec capabilities of standard codecs */
	memset(&caps, 0, sizeof(caps));
	for (i = 0; i < std_codecs->num; i++) {
		caps.id = std_codecs->codec[i];
		caps.direction = 0x00;
		hci_read_codec_capabilities(hdev, LOCAL_CODEC_ACL_MASK, &caps);

		caps.direction = 0x01;
		hci_read_codec_capabilities(hdev, LOCAL_CODEC_ACL_MASK, &caps);
	}

	skb_pull(skb, flex_array_size(std_codecs, codec, std_codecs->num)
		 + sizeof(std_codecs->num));

	vnd_codecs = (void *)skb->data;

	/* validate vendor codecs length before accessing */
	if (skb->len <
	    flex_array_size(vnd_codecs, codec, vnd_codecs->num)
	    + sizeof(vnd_codecs->num))
		goto error;

	/* enumerate vendor codec capabilities */
	for (i = 0; i < vnd_codecs->num; i++) {
		caps.id = 0xFF;
		caps.cid = vnd_codecs->codec[i].cid;
		caps.vid = vnd_codecs->codec[i].vid;
		caps.direction = 0x00;
		hci_read_codec_capabilities(hdev, LOCAL_CODEC_ACL_MASK, &caps);

		caps.direction = 0x01;
		hci_read_codec_capabilities(hdev, LOCAL_CODEC_ACL_MASK, &caps);
	}

error:
	kfree_skb(skb);
}

void hci_read_supported_codecs_v2(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct hci_rp_read_local_supported_codecs_v2 *rp;
	struct hci_std_codecs_v2 *std_codecs;
	struct hci_vnd_codecs_v2 *vnd_codecs;
	struct hci_op_read_local_codec_caps caps;
	__u8 i;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_CODECS_V2, 0, NULL,
			     HCI_CMD_TIMEOUT);

	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Failed to read local supported codecs (%ld)",
			   PTR_ERR(skb));
		return;
	}

	if (skb->len < sizeof(*rp))
		goto error;

	rp = (void *)skb->data;

	if (rp->status)
		goto error;

	skb_pull(skb, sizeof(rp->status));

	std_codecs = (void *)skb->data;

	/* check for payload data length before accessing */
	if (skb->len < flex_array_size(std_codecs, codec, std_codecs->num)
	    + sizeof(std_codecs->num))
		goto error;

	memset(&caps, 0, sizeof(caps));

	for (i = 0; i < std_codecs->num; i++) {
		caps.id = std_codecs->codec[i].id;
		caps.direction = 0x00;
		hci_read_codec_capabilities(hdev, std_codecs->codec[i].transport,
					    &caps);
		caps.direction = 0x01;
		hci_read_codec_capabilities(hdev, std_codecs->codec[i].transport,
					    &caps);
	}

	skb_pull(skb, flex_array_size(std_codecs, codec, std_codecs->num)
		 + sizeof(std_codecs->num));

	vnd_codecs = (void *)skb->data;

	/* check for payload data length before accessing */
	if (skb->len <
	    flex_array_size(vnd_codecs, codec, vnd_codecs->num)
	    + sizeof(vnd_codecs->num))
		goto error;

	for (i = 0; i < vnd_codecs->num; i++) {
		caps.id = 0xFF;
		caps.cid = vnd_codecs->codec[i].cid;
		caps.vid = vnd_codecs->codec[i].vid;
		caps.direction = 0x00;
		hci_read_codec_capabilities(hdev, vnd_codecs->codec[i].transport,
					    &caps);
		caps.direction = 0x01;
		hci_read_codec_capabilities(hdev, vnd_codecs->codec[i].transport,
					    &caps);
	}

error:
	kfree_skb(skb);
}

int hci_get_supported_codecs(struct hci_dev *hdev, u8 type, char __user *optval,
			     int __user *optlen, int len)
{
	int n = 0, buf_len = 0, err = 0;
	struct hci_codec_caps *caps;
	struct bt_codec codec;
	u8 num_codecs = 0, i, __user *ptr;
	struct codec_list *c;
	u8 data_path;

	if (!hci_dev_test_flag(hdev, HCI_OFFLOAD_CODECS_ENABLED)) {
		err = -EOPNOTSUPP;
		goto error;
	}

	if (!hdev->get_data_path_id) {
		err = -EOPNOTSUPP;
		goto error;
	}

	err = hdev->get_data_path_id(hdev, type, &data_path);
	if (err < 0)
		goto error;

	/* find total buffer size required to copy codec + capabilities */
	hci_dev_lock(hdev);
	list_for_each_entry(c, &hdev->local_codecs, list) {
		if (c->transport != type)
			continue;
		num_codecs++;
		for (i = 0, caps = c->caps; i < c->num_caps; i++) {
			buf_len += 1 + caps->len;
			caps = (void *)&caps->data[caps->len];
		}
		buf_len += sizeof(struct bt_codec);
	}
	hci_dev_unlock(hdev);

	buf_len += sizeof(struct bt_codecs);
	if (buf_len > len) {
		err = -ENOBUFS;
		goto error;
	}
	ptr = optval;

	if (put_user(num_codecs, ptr)) {
		err = -EFAULT;
		goto error;
	}
	ptr += sizeof(num_codecs);

	/* Iterate over all the codecs on required transport */
	hci_dev_lock(hdev);
	list_for_each_entry(c, &hdev->local_codecs, list) {
		if (c->transport != type)
			continue;

		codec.id = c->id;
		codec.cid = c->cid;
		codec.vid = c->vid;
		codec.data_path = data_path;
		codec.num_caps = c->num_caps;
		if (copy_to_user(ptr, &codec, sizeof(codec))) {
			err = -EFAULT;
			break;
		}
		ptr += sizeof(codec);

		/* find codec capabilities data length */
		n = 0;
		for (i = 0, caps = c->caps; i < c->num_caps; i++) {
			n += 1 + caps->len;
			caps = (void *)&caps->data[caps->len];
		}

		/* copy codec capabilities data */
		if (n && copy_to_user(ptr, c->caps, n)) {
			err = -EFAULT;
			break;
		}
		ptr += n;
	}
	hci_dev_unlock(hdev);

	if (!err && put_user(buf_len, optlen))
		err = -EFAULT;

error:
	return err;
}
