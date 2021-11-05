// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021, Linaro Ltd. All rights reserved.
 */
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/typec_mux.h>
#include "qcom_pmic_tcpm_pdphy.h"
#include "qcom_pmic_tcpm_typec.h"

struct pmic_tcpm {
	struct device		*dev;
	struct pmic_typec	*pmic_typec;
	struct pmic_pdphy	*pmic_pdphy;
	struct tcpm_port	*tcpm_port;
	struct tcpc_dev		tcpc;
	bool			vbus_enabled;
	struct mutex		lock;		/* VBUS state serialization */
};

#define tcpc_to_tcpm(_tcpc_) container_of(_tcpc_, struct pmic_tcpm, tcpc)

static int qcom_pmic_tcpm_core_get_vbus(struct tcpc_dev *tcpc)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);
	int ret;

	mutex_lock(&tcpm->lock);
	ret = tcpm->vbus_enabled || qcom_pmic_tcpm_typec_get_vbus(tcpm->pmic_typec);
	mutex_unlock(&tcpm->lock);

	return ret;
}

static int qcom_pmic_tcpm_core_set_vbus(struct tcpc_dev *tcpc, bool on,
					bool sink)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);
	int ret = 0;

	mutex_lock(&tcpm->lock);
	if (tcpm->vbus_enabled == on)
		goto done;

	ret = qcom_pmic_tcpm_typec_set_vbus(tcpm->pmic_typec, on);
	if (ret)
		goto done;

	tcpm->vbus_enabled = on;
	tcpm_vbus_change(tcpm->tcpm_port);

done:
	dev_dbg(tcpm->dev, "set_vbus set: %d result %d\n", on, ret);
	mutex_unlock(&tcpm->lock);

	return ret;
}

static int qcom_pmic_tcpm_core_set_vconn(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_tcpm_typec_set_vconn(tcpm->pmic_typec, on);
}

static int qcom_pmic_tcpm_core_get_cc(struct tcpc_dev *tcpc,
				      enum typec_cc_status *cc1,
				      enum typec_cc_status *cc2)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_tcpm_typec_get_cc(tcpm->pmic_typec, cc1, cc2);
}

static int qcom_pmic_tcpm_core_set_cc(struct tcpc_dev *tcpc,
				      enum typec_cc_status cc)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_tcpm_typec_set_cc(tcpm->pmic_typec, cc);
}

static int qcom_pmic_tcpm_core_set_polarity(struct tcpc_dev *tcpc,
					    enum typec_cc_polarity pol)
{
	/* Polarity is set separately by phy-qcom-qmp.c */
	return 0;
}

static int qcom_pmic_tcpm_core_start_toggling(struct tcpc_dev *tcpc,
					      enum typec_port_type port_type,
					      enum typec_cc_status cc)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_tcpm_typec_start_toggling(tcpm->pmic_typec, port_type,
						   cc);
}

static int qcom_pmic_tcpm_core_set_roles(struct tcpc_dev *tcpc, bool attached,
					 enum typec_role power_role,
					 enum typec_data_role data_role)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_tcpm_pdphy_set_roles(tcpm->pmic_pdphy, data_role,
					      power_role);
}

static int qcom_pmic_tcpm_core_set_pd_rx(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_tcpm_pdphy_set_pd_rx(tcpm->pmic_pdphy, on);
}

static int qcom_pmic_tcpm_core_pd_transmit(struct tcpc_dev *tcpc,
					   enum tcpm_transmit_type type,
					   const struct pd_message *msg,
					   unsigned int negotiated_rev)
{
	struct pmic_tcpm *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_tcpm_pdphy_pd_transmit(tcpm->pmic_pdphy, type, msg,
						negotiated_rev);
}

static struct platform_device
*qcom_pmic_tcpm_core_get_pdev(struct device *dev, const char *property_name)
{
	struct device_node *np;
	struct platform_device *pdev;
	const __be32 *prop;

	prop = of_get_property(dev->of_node, property_name, NULL);
	if (!prop) {
		dev_err(dev, "required '%s' property missing\n", property_name);
		return ERR_PTR(-EINVAL);
	}

	np = of_find_node_by_phandle(be32_to_cpup(prop));
	if (!np) {
		dev_err(dev, "could not find '%s' node\n", property_name);
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);

	if (pdev)
		return pdev;

	return ERR_PTR(-ENODEV);
}

static int qcom_pmic_tcpm_core_init(struct tcpc_dev *tcpc)
{
	return 0;
}

static int qcom_pmic_tcpm_core_probe(struct platform_device *pdev)
{
	struct pmic_tcpm *tcpm;
	struct device *dev = &pdev->dev;
	struct platform_device *typec_pdev;
	struct platform_device *pdphy_pdev;
	int ret;

	tcpm = devm_kzalloc(dev, sizeof(*tcpm), GFP_KERNEL);
	if (!tcpm)
		return -ENOMEM;

	tcpm->dev = dev;
	tcpm->tcpc.init = qcom_pmic_tcpm_core_init;
	tcpm->tcpc.get_vbus = qcom_pmic_tcpm_core_get_vbus;
	tcpm->tcpc.set_vbus = qcom_pmic_tcpm_core_set_vbus;
	tcpm->tcpc.set_cc = qcom_pmic_tcpm_core_set_cc;
	tcpm->tcpc.get_cc = qcom_pmic_tcpm_core_get_cc;
	tcpm->tcpc.set_polarity = qcom_pmic_tcpm_core_set_polarity;
	tcpm->tcpc.set_vconn = qcom_pmic_tcpm_core_set_vconn;
	tcpm->tcpc.start_toggling = qcom_pmic_tcpm_core_start_toggling;
	tcpm->tcpc.set_pd_rx = qcom_pmic_tcpm_core_set_pd_rx;
	tcpm->tcpc.set_roles = qcom_pmic_tcpm_core_set_roles;
	tcpm->tcpc.pd_transmit = qcom_pmic_tcpm_core_pd_transmit;

	mutex_init(&tcpm->lock);
	platform_set_drvdata(pdev, tcpm);

	typec_pdev = qcom_pmic_tcpm_core_get_pdev(dev, "pmic_tcpm_typec");
	if (IS_ERR(typec_pdev)) {
		dev_err(dev, "Error linking typec endpoint\n");
		return PTR_ERR(typec_pdev);
	}

	tcpm->pmic_typec = platform_get_drvdata(typec_pdev);
	if (!tcpm->pmic_typec) {
		ret = -EPROBE_DEFER;
		goto put_typec_pdev;
	}

	pdphy_pdev = qcom_pmic_tcpm_core_get_pdev(dev, "pmic_tcpm_pdphy");
	if (IS_ERR(pdphy_pdev)) {
		dev_err(dev, "Error linking pdphy endpoint\n");
		ret = PTR_ERR(pdphy_pdev);
		goto put_typec_pdev;
	}

	tcpm->pmic_pdphy = platform_get_drvdata(pdphy_pdev);
	if (!tcpm->pmic_pdphy) {
		ret = -EPROBE_DEFER;
		goto put_pdphy_dev;
	}

	tcpm->tcpc.fwnode = device_get_named_child_node(tcpm->dev, "connector");
	if (IS_ERR(tcpm->tcpc.fwnode))
		return PTR_ERR(tcpm->tcpc.fwnode);

	tcpm->tcpm_port = tcpm_register_port(tcpm->dev, &tcpm->tcpc);
	if (IS_ERR(tcpm->tcpm_port)) {
		ret = PTR_ERR(tcpm->tcpm_port);
		goto fwnode_remove;
	}

	ret = qcom_pmic_tcpm_pdphy_init(tcpm->pmic_pdphy, tcpm->tcpm_port);
	if (ret)
		goto fwnode_remove;

	ret = qcom_pmic_tcpm_typec_init(tcpm->pmic_typec, tcpm->tcpm_port);
	if (ret)
		goto fwnode_remove;

	return 0;

fwnode_remove:
	fwnode_remove_software_node(tcpm->tcpc.fwnode);

put_pdphy_dev:
	put_device(&pdphy_pdev->dev);

put_typec_pdev:
	put_device(&typec_pdev->dev);

	return ret;
}

static int qcom_pmic_tcpm_core_remove(struct platform_device *pdev)
{
	struct pmic_tcpm *tcpm = platform_get_drvdata(pdev);

	tcpm_unregister_port(tcpm->tcpm_port);
	fwnode_remove_software_node(tcpm->tcpc.fwnode);
	qcom_pmic_tcpm_pdphy_put(tcpm->pmic_pdphy);
	qcom_pmic_tcpm_typec_put(tcpm->pmic_typec);

	return 0;
}

static const struct of_device_id qcom_pmic_tcpm_core_table[] = {
	{ .compatible = "qcom,pm8150b-tcpm" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_pmic_tcpm_core_table);

static struct platform_driver qcom_pmic_tcpm_core_platform_driver = {
	.driver = {
		.name = "qcom,pmic-tcpm",
		.of_match_table = qcom_pmic_tcpm_core_table,
	},
	.probe = qcom_pmic_tcpm_core_probe,
	.remove = qcom_pmic_tcpm_core_remove,
};

static int __init qcom_pmic_tcpm_core_module_init(void)
{
	int ret;

	ret = platform_driver_register(&qcom_pmic_tcpm_typec_platform_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&qcom_pmic_tcpm_pdphy_platform_driver);
	if (ret)
		goto unregister_typec;

	ret = platform_driver_register(&qcom_pmic_tcpm_core_platform_driver);
	if (ret)
		goto unregister_pdphy;

	return 0;

unregister_pdphy:
	platform_driver_unregister(&qcom_pmic_tcpm_pdphy_platform_driver);

unregister_typec:
	platform_driver_unregister(&qcom_pmic_tcpm_typec_platform_driver);

	return ret;
}
module_init(qcom_pmic_tcpm_core_module_init);

static void __exit qcom_pmic_tcpm_core_module_exit(void)
{
	platform_driver_unregister(&qcom_pmic_tcpm_core_platform_driver);
	platform_driver_unregister(&qcom_pmic_tcpm_pdphy_platform_driver);
	platform_driver_unregister(&qcom_pmic_tcpm_typec_platform_driver);
}
module_exit(qcom_pmic_tcpm_core_module_exit);

MODULE_DESCRIPTION("QCOM PMIC USB Type-C Port Manager Driver");
MODULE_LICENSE("GPL v2");
