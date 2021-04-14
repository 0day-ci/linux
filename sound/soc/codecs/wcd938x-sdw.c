// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/component.h>
#include <sound/soc.h>
#include <linux/pm_runtime.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include "wcd938x.h"

#define SWRS_SCP_HOST_CLK_DIV2_CTL_BANK(m) (0xE0 + 0x10 * (m))

static struct wcd938x_sdw_ch_info wcd938x_sdw_rx_ch_info[] = {
	WCD_SDW_CH(WCD938X_HPH_L, WCD938X_HPH_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_HPH_R, WCD938X_HPH_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_CLSH, WCD938X_CLSH_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_COMP_L, WCD938X_COMP_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_COMP_R, WCD938X_COMP_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_LO, WCD938X_LO_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_DSD_L, WCD938X_DSD_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_DSD_R, WCD938X_DSD_PORT, BIT(1)),
};

static struct wcd938x_sdw_ch_info wcd938x_sdw_tx_ch_info[] = {
	WCD_SDW_CH(WCD938X_ADC1, WCD938X_ADC_1_2_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_ADC2, WCD938X_ADC_1_2_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_ADC3, WCD938X_ADC_3_4_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_ADC4, WCD938X_ADC_3_4_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_DMIC0, WCD938X_DMIC_0_3_MBHC_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_DMIC1, WCD938X_DMIC_0_3_MBHC_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_MBHC, WCD938X_DMIC_0_3_MBHC_PORT, BIT(2)),
	WCD_SDW_CH(WCD938X_DMIC2, WCD938X_DMIC_0_3_MBHC_PORT, BIT(2)),
	WCD_SDW_CH(WCD938X_DMIC3, WCD938X_DMIC_0_3_MBHC_PORT, BIT(3)),
	WCD_SDW_CH(WCD938X_DMIC4, WCD938X_DMIC_4_7_PORT, BIT(0)),
	WCD_SDW_CH(WCD938X_DMIC5, WCD938X_DMIC_4_7_PORT, BIT(1)),
	WCD_SDW_CH(WCD938X_DMIC6, WCD938X_DMIC_4_7_PORT, BIT(2)),
	WCD_SDW_CH(WCD938X_DMIC7, WCD938X_DMIC_4_7_PORT, BIT(3)),
};

static struct sdw_dpn_prop wcd938x_dpn_prop[WCD938X_MAX_SWR_PORTS] = {
	{
		.num = 1,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 8,
		.simple_ch_prep_sm = true,
	}, {
		.num = 2,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 4,
		.simple_ch_prep_sm = true,
	}, {
		.num = 3,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 4,
		.simple_ch_prep_sm = true,
	}, {
		.num = 4,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 4,
		.simple_ch_prep_sm = true,
	}, {
		.num = 5,
		.type = SDW_DPN_SIMPLE,
		.min_ch = 1,
		.max_ch = 4,
		.simple_ch_prep_sm = true,
	}
};

static int wcd9380_update_status(struct sdw_slave *slave,
				 enum sdw_slave_status status)
{
	return 0;
}

static int wcd9380_bus_config(struct sdw_slave *slave,
			      struct sdw_bus_params *params)
{
	sdw_write(slave, SWRS_SCP_HOST_CLK_DIV2_CTL_BANK(params->next_bank),  0x01);

	return 0;
}

static int wcd9380_interrupt_callback(struct sdw_slave *slave,
				      struct sdw_slave_intr_status *status)
{
	struct wcd938x_sdw_priv *wcd = dev_get_drvdata(&slave->dev);

	return wcd938x_handle_sdw_irq(wcd);
}

static struct sdw_slave_ops wcd9380_slave_ops = {
	.update_status = wcd9380_update_status,
	.interrupt_callback = wcd9380_interrupt_callback,
	.bus_config = wcd9380_bus_config,
};

static int wcd938x_sdw_component_bind(struct device *dev,
				      struct device *master, void *data)
{
	return 0;
}

static void wcd938x_sdw_component_unbind(struct device *dev,
					 struct device *master, void *data)
{
}

static const struct component_ops wcd938x_sdw_component_ops = {
	.bind   = wcd938x_sdw_component_bind,
	.unbind = wcd938x_sdw_component_unbind,
};

static int wcd9380_probe(struct sdw_slave *pdev,
			 const struct sdw_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct wcd938x_sdw_priv *wcd;
	const char *dir = "rx";
	int ret;

	wcd = devm_kzalloc(dev, sizeof(*wcd), GFP_KERNEL);
	if (!wcd)
		return -ENOMEM;

	of_property_read_string(dev->of_node, "qcom,direction", &dir);
	if (!strcmp(dir, "tx"))
		wcd->is_tx = true;
	else
		wcd->is_tx = false;


	ret = of_property_read_variable_u32_array(dev->of_node, "qcom,port-mapping",
						  wcd->port_map,
						  WCD938X_MAX_TX_SWR_PORTS,
						  WCD938X_MAX_SWR_PORTS);
	if (ret)
		dev_info(dev, "Static Port mapping not specified\n");

	wcd->sdev = pdev;
	dev_set_drvdata(dev, wcd);

	pdev->prop.scp_int1_mask = SDW_SCP_INT1_IMPL_DEF |
					SDW_SCP_INT1_BUS_CLASH |
					SDW_SCP_INT1_PARITY;
	pdev->prop.lane_control_support = true;
	if (wcd->is_tx) {
		pdev->prop.source_ports = GENMASK(WCD938X_MAX_SWR_PORTS, 0);
		pdev->prop.src_dpn_prop = wcd938x_dpn_prop;
		wcd->ch_info = &wcd938x_sdw_tx_ch_info[0];
		pdev->prop.wake_capable = true;
	} else {
		pdev->prop.sink_ports = GENMASK(WCD938X_MAX_SWR_PORTS, 0);
		pdev->prop.sink_dpn_prop = wcd938x_dpn_prop;
		wcd->ch_info = &wcd938x_sdw_rx_ch_info[0];
	}

	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return component_add(dev, &wcd938x_sdw_component_ops);
}

static const struct sdw_device_id wcd9380_slave_id[] = {
	SDW_SLAVE_ENTRY(0x0217, 0x10d, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, wcd9380_slave_id);

static int __maybe_unused wcd938x_sdw_runtime_suspend(struct device *dev)
{
	struct regmap *regmap = dev_get_regmap(dev, NULL);

	if (regmap) {
		regcache_cache_only(regmap, true);
		regcache_mark_dirty(regmap);
	}
	return 0;
}

static int __maybe_unused wcd938x_sdw_runtime_resume(struct device *dev)
{
	struct regmap *regmap = dev_get_regmap(dev, NULL);

	if (regmap) {
		regcache_cache_only(regmap, false);
		regcache_sync(regmap);
	}

	pm_runtime_mark_last_busy(dev);

	return 0;
}

static const struct dev_pm_ops wcd938x_sdw_pm_ops = {
	SET_RUNTIME_PM_OPS(wcd938x_sdw_runtime_suspend, wcd938x_sdw_runtime_resume, NULL)
};


static struct sdw_driver wcd9380_codec_driver = {
	.probe	= wcd9380_probe,
	.ops = &wcd9380_slave_ops,
	.id_table = wcd9380_slave_id,
	.driver = {
		.name	= "wcd9380-codec",
		.pm = &wcd938x_sdw_pm_ops,
	}
};
module_sdw_driver(wcd9380_codec_driver);

MODULE_DESCRIPTION("WCD938X SDW codec driver");
MODULE_LICENSE("GPL");
