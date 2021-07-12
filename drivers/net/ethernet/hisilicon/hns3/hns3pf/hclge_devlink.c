// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2021 Hisilicon Limited. */

#include <net/devlink.h>

#include "hclge_devlink.h"

static int hclge_devlink_info_get(struct devlink *devlink,
				  struct devlink_info_req *req,
				  struct netlink_ext_ack *extack)
{
#define	HCLGE_DEVLINK_FW_STRING_LEN	32
	struct hclge_devlink_priv *priv = devlink_priv(devlink);
	char version_str[HCLGE_DEVLINK_FW_STRING_LEN];
	struct hclge_dev *hdev = priv->hdev;
	int ret;

	ret = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (ret)
		return ret;

	snprintf(version_str, sizeof(version_str), "%lu.%lu.%lu.%lu",
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE3_MASK,
				 HNAE3_FW_VERSION_BYTE3_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE2_MASK,
				 HNAE3_FW_VERSION_BYTE2_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE1_MASK,
				 HNAE3_FW_VERSION_BYTE1_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE0_MASK,
				 HNAE3_FW_VERSION_BYTE0_SHIFT));

	return devlink_info_version_running_put(req,
						DEVLINK_INFO_VERSION_GENERIC_FW,
						version_str);
}

static void hclge_devlink_get_param_setting(struct devlink *devlink)
{
	struct hclge_devlink_priv *priv = devlink_priv(devlink);
	struct hclge_dev *hdev = priv->hdev;
	struct pci_dev *pdev = hdev->pdev;
	union devlink_param_value val;
	int i, ret;

	ret = devlink_param_driverinit_value_get(devlink,
						 HCLGE_DEVLINK_PARAM_ID_RX_BUF_LEN,
						 &val);
	if (!ret) {
		hdev->rx_buf_len = val.vu32;
		hdev->vport->nic.kinfo.rx_buf_len = hdev->rx_buf_len;
		for (i = 0; i < hdev->num_tqps; i++)
			hdev->htqp[i].q.buf_size = hdev->rx_buf_len;
	} else {
		dev_err(&pdev->dev,
			"failed to get rx buffer size, ret = %d\n", ret);
	}

	ret = devlink_param_driverinit_value_get(devlink,
						 HCLGE_DEVLINK_PARAM_ID_TX_BUF_SIZE,
						 &val);
	if (!ret)
		hdev->vport->nic.kinfo.devlink_tx_spare_buf_size = val.vu32;
	else
		dev_err(&pdev->dev,
			"failed to get tx buffer size, ret = %d\n", ret);
}

static int hclge_devlink_reload_down(struct devlink *devlink, bool netns_change,
				     enum devlink_reload_action action,
				     enum devlink_reload_limit limit,
				     struct netlink_ext_ack *extack)
{
	struct hclge_devlink_priv *priv = devlink_priv(devlink);
	struct hclge_dev *hdev = priv->hdev;
	struct hnae3_handle *h = &hdev->vport->nic;
	struct pci_dev *pdev = hdev->pdev;
	int ret;

	if (test_bit(HCLGE_STATE_RST_HANDLING, &hdev->state)) {
		dev_err(&pdev->dev, "reset is handling\n");
		return -EBUSY;
	}

	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
		rtnl_lock();
		ret = hdev->nic_client->ops->reset_notify(h, HNAE3_DOWN_CLIENT);
		if (ret) {
			rtnl_unlock();
			return ret;
		}

		ret = hdev->nic_client->ops->reset_notify(h,
							  HNAE3_UNINIT_CLIENT);
		rtnl_unlock();
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static int hclge_devlink_reload_up(struct devlink *devlink,
				   enum devlink_reload_action action,
				   enum devlink_reload_limit limit,
				   u32 *actions_performed,
				   struct netlink_ext_ack *extack)
{
	struct hclge_devlink_priv *priv = devlink_priv(devlink);
	struct hclge_dev *hdev = priv->hdev;
	struct hnae3_handle *h = &hdev->vport->nic;
	int ret;

	*actions_performed = BIT(action);
	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
		hclge_devlink_get_param_setting(devlink);
		rtnl_lock();
		ret = hdev->nic_client->ops->reset_notify(h, HNAE3_INIT_CLIENT);
		if (ret) {
			rtnl_unlock();
			return ret;
		}

		ret = hdev->nic_client->ops->reset_notify(h, HNAE3_UP_CLIENT);
		rtnl_unlock();
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct devlink_ops hclge_devlink_ops = {
	.info_get = hclge_devlink_info_get,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT),
	.reload_down = hclge_devlink_reload_down,
	.reload_up = hclge_devlink_reload_up,
};

static int hclge_devlink_rx_buffer_size_validate(struct devlink *devlink,
						 u32 id,
						 union devlink_param_value val,
						 struct netlink_ext_ack *extack)
{
#define HCLGE_RX_BUF_LEN_2K	2048
#define HCLGE_RX_BUF_LEN_4K	4096

	if (val.vu32 != HCLGE_RX_BUF_LEN_2K &&
	    val.vu32 != HCLGE_RX_BUF_LEN_4K) {
		NL_SET_ERR_MSG_MOD(extack, "Supported size is 2048 or 4096");
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct devlink_param hclge_devlink_params[] = {
	DEVLINK_PARAM_DRIVER(HCLGE_DEVLINK_PARAM_ID_RX_BUF_LEN,
			     "rx_buffer_len", DEVLINK_PARAM_TYPE_U32,
			     BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			     NULL, NULL,
			     hclge_devlink_rx_buffer_size_validate),
	DEVLINK_PARAM_DRIVER(HCLGE_DEVLINK_PARAM_ID_TX_BUF_SIZE,
			     "tx_buffer_size", DEVLINK_PARAM_TYPE_U32,
			     BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			     NULL, NULL, NULL),
};

void hclge_devlink_set_params_init_values(struct hclge_dev *hdev)
{
	union devlink_param_value value;

	value.vu32 = hdev->rx_buf_len;
	devlink_param_driverinit_value_set(hdev->devlink,
					   HCLGE_DEVLINK_PARAM_ID_RX_BUF_LEN,
					   value);
	value.vu32 = hdev->tx_spare_buf_size;
	devlink_param_driverinit_value_set(hdev->devlink,
					   HCLGE_DEVLINK_PARAM_ID_TX_BUF_SIZE,
					   value);
}

int hclge_devlink_init(struct hclge_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct hclge_devlink_priv *priv;
	struct devlink *devlink;
	int ret;

	devlink = devlink_alloc(&hclge_devlink_ops,
				sizeof(struct hclge_devlink_priv));
	if (!devlink)
		return -ENOMEM;

	priv = devlink_priv(devlink);
	priv->hdev = hdev;

	ret = devlink_register(devlink, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register devlink, ret = %d\n",
			ret);
		goto out_reg_fail;
	}

	hdev->devlink = devlink;

	ret = devlink_params_register(devlink, hclge_devlink_params,
				      ARRAY_SIZE(hclge_devlink_params));
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register devlink params, ret = %d\n", ret);
		goto out_param_reg_fail;
	}

	devlink_reload_enable(devlink);

	return 0;
out_param_reg_fail:
	hdev->devlink = NULL;
	devlink_unregister(devlink);
out_reg_fail:
	devlink_free(devlink);
	return ret;
}

void hclge_devlink_uninit(struct hclge_dev *hdev)
{
	struct devlink *devlink = hdev->devlink;

	if (!devlink)
		return;

	devlink_reload_disable(devlink);

	devlink_params_unregister(devlink, hclge_devlink_params,
				  ARRAY_SIZE(hclge_devlink_params));
	devlink_unregister(devlink);

	devlink_free(devlink);

	hdev->devlink = NULL;
}
