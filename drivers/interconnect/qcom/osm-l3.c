// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/interconnect/qcom,osm-l3.h>

#include "sc7180.h"
#include "sc7280.h"
#include "sc8180x.h"
#include "sdm845.h"
#include "sm8150.h"
#include "sm8250.h"

#define LUT_MAX_ENTRIES			40U
#define LUT_SRC				GENMASK(31, 30)
#define LUT_L_VAL			GENMASK(7, 0)
#define CLK_HW_DIV			2

/* OSM Register offsets */
#define REG_ENABLE			0x0
#define OSM_LUT_ROW_SIZE		32
#define OSM_REG_FREQ_LUT		0x110
#define OSM_REG_PERF_STATE		0x920

/* EPSS Register offsets */
#define EPSS_LUT_ROW_SIZE		4
#define EPSS_REG_L3_VOTE		0x90
#define EPSS_REG_FREQ_LUT		0x100
#define EPSS_REG_PERF_STATE		0x320
#define EPSS_CORE_OFFSET		0x4
#define EPSS_L3_VOTE_REG(base, cpu)\
			(((base) + EPSS_REG_L3_VOTE) +\
			((cpu) * EPSS_CORE_OFFSET))

#define L3_DOMAIN_CNT		4

#define to_osm_l3_provider(_provider) \
	container_of(_provider, struct qcom_osm_l3_icc_provider, provider)

/**
 * struct qcom_osm_l3_icc_provider - Qualcomm specific interconnect provider
 * @domain_base: an array of base address for each clock domain
 * @max_state: max supported frequency level
 * @per_core_dcvs: flag used to indicate whether the frequency scaling
 * for each core is enabled
 * @reg_perf_state: requested frequency level
 * @lut_tables: an array of supported frequency levels
 * @provider: interconnect provider of this node
 */
struct qcom_osm_l3_icc_provider {
	void __iomem *domain_base[L3_DOMAIN_CNT];
	unsigned int max_state;
	bool per_core_dcvs;
	unsigned int reg_perf_state;
	unsigned long lut_tables[LUT_MAX_ENTRIES];
	struct icc_provider provider;
};

/**
 * struct qcom_osm_l3_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @id: a unique node identifier
 * @buswidth: width of the interconnect between a node and the bus
 * @domain: clock domain of the cpu node
 * @cpu: cpu instance within its clock domain
 * @num_links: the total number of @links
 * @links: an array of nodes where we can go next while traversing
 */
struct qcom_osm_l3_node {
	const char *name;
	u16 id;
	u16 buswidth;
	u8 domain;
	u8 cpu;
	u16 num_links;
	u16 links[];
};

struct qcom_osm_l3_desc {
	const struct qcom_osm_l3_node **nodes;
	size_t num_nodes;
	bool per_core_dcvs;
	unsigned int lut_row_size;
	unsigned int reg_freq_lut;
	unsigned int reg_perf_state;
};

#define __DEFINE_QNODE(_name, _id, _buswidth, _domain, _cpu, ...)	\
	static const struct qcom_osm_l3_node _name = {			\
		.name = #_name,						\
		.id = _id,						\
		.buswidth = _buswidth,					\
		.domain = _domain,					\
		.cpu = _cpu,						\
		.num_links = ARRAY_SIZE(((int[]){ __VA_ARGS__ })),	\
		.links = { __VA_ARGS__ },				\
	}

#define DEFINE_QNODE(_name, _id, _buswidth, ...)		\
		__DEFINE_QNODE(_name, _id, _buswidth, 0, 0, __VA_ARGS__)
#define DEFINE_DCVS_QNODE(_name, _id, _buswidth, _domain, _cpu, ...)		\
		__DEFINE_QNODE(_name, _id, _buswidth, _domain, _cpu, __VA_ARGS__)

DEFINE_QNODE(sdm845_osm_apps_l3, SDM845_MASTER_OSM_L3_APPS, 16, SDM845_SLAVE_OSM_L3);
DEFINE_QNODE(sdm845_osm_l3, SDM845_SLAVE_OSM_L3, 16);

static const struct qcom_osm_l3_node *sdm845_osm_l3_nodes[] = {
	[MASTER_OSM_L3_APPS] = &sdm845_osm_apps_l3,
	[SLAVE_OSM_L3] = &sdm845_osm_l3,
};

static const struct qcom_osm_l3_desc sdm845_icc_osm_l3 = {
	.nodes = sdm845_osm_l3_nodes,
	.num_nodes = ARRAY_SIZE(sdm845_osm_l3_nodes),
	.lut_row_size = OSM_LUT_ROW_SIZE,
	.reg_freq_lut = OSM_REG_FREQ_LUT,
	.reg_perf_state = OSM_REG_PERF_STATE,
};

DEFINE_QNODE(sc7180_osm_apps_l3, SC7180_MASTER_OSM_L3_APPS, 16, SC7180_SLAVE_OSM_L3);
DEFINE_QNODE(sc7180_osm_l3, SC7180_SLAVE_OSM_L3, 16);

static const struct qcom_osm_l3_node *sc7180_osm_l3_nodes[] = {
	[MASTER_OSM_L3_APPS] = &sc7180_osm_apps_l3,
	[SLAVE_OSM_L3] = &sc7180_osm_l3,
};

static const struct qcom_osm_l3_desc sc7180_icc_osm_l3 = {
	.nodes = sc7180_osm_l3_nodes,
	.num_nodes = ARRAY_SIZE(sc7180_osm_l3_nodes),
	.lut_row_size = OSM_LUT_ROW_SIZE,
	.reg_freq_lut = OSM_REG_FREQ_LUT,
	.reg_perf_state = OSM_REG_PERF_STATE,
};

DEFINE_QNODE(sc8180x_osm_apps_l3, SC8180X_MASTER_OSM_L3_APPS, 32, SC8180X_SLAVE_OSM_L3);
DEFINE_QNODE(sc8180x_osm_l3, SC8180X_SLAVE_OSM_L3, 32);

static const struct qcom_osm_l3_node *sc8180x_osm_l3_nodes[] = {
	[MASTER_OSM_L3_APPS] = &sc8180x_osm_apps_l3,
	[SLAVE_OSM_L3] = &sc8180x_osm_l3,
};

static const struct qcom_osm_l3_desc sc8180x_icc_osm_l3 = {
	.nodes = sc8180x_osm_l3_nodes,
	.num_nodes = ARRAY_SIZE(sc8180x_osm_l3_nodes),
	.lut_row_size = OSM_LUT_ROW_SIZE,
	.reg_freq_lut = OSM_REG_FREQ_LUT,
	.reg_perf_state = OSM_REG_PERF_STATE,
};

DEFINE_QNODE(sm8150_osm_apps_l3, SM8150_MASTER_OSM_L3_APPS, 32, SM8150_SLAVE_OSM_L3);
DEFINE_QNODE(sm8150_osm_l3, SM8150_SLAVE_OSM_L3, 32);

static const struct qcom_osm_l3_node *sm8150_osm_l3_nodes[] = {
	[MASTER_OSM_L3_APPS] = &sm8150_osm_apps_l3,
	[SLAVE_OSM_L3] = &sm8150_osm_l3,
};

static const struct qcom_osm_l3_desc sm8150_icc_osm_l3 = {
	.nodes = sm8150_osm_l3_nodes,
	.num_nodes = ARRAY_SIZE(sm8150_osm_l3_nodes),
	.lut_row_size = OSM_LUT_ROW_SIZE,
	.reg_freq_lut = OSM_REG_FREQ_LUT,
	.reg_perf_state = OSM_REG_PERF_STATE,
};

DEFINE_QNODE(sm8250_epss_apps_l3, SM8250_MASTER_EPSS_L3_APPS, 32, SM8250_SLAVE_EPSS_L3);
DEFINE_QNODE(sm8250_epss_l3, SM8250_SLAVE_EPSS_L3, 32);

static const struct qcom_osm_l3_node *sm8250_epss_l3_nodes[] = {
	[MASTER_EPSS_L3_APPS] = &sm8250_epss_apps_l3,
	[SLAVE_EPSS_L3_SHARED] = &sm8250_epss_l3,
};

static const struct qcom_osm_l3_desc sm8250_icc_epss_l3 = {
	.nodes = sm8250_epss_l3_nodes,
	.num_nodes = ARRAY_SIZE(sm8250_epss_l3_nodes),
	.lut_row_size = EPSS_LUT_ROW_SIZE,
	.reg_freq_lut = EPSS_REG_FREQ_LUT,
	.reg_perf_state = EPSS_REG_PERF_STATE,
};

DEFINE_DCVS_QNODE(sc7280_epss_apps_l3, SC7280_MASTER_EPSS_L3_APPS, 32, 0, 0,
		  SC7280_SLAVE_EPSS_L3_SHARED, SC7280_SLAVE_EPSS_L3_CPU0,
		  SC7280_SLAVE_EPSS_L3_CPU1, SC7280_SLAVE_EPSS_L3_CPU2,
		  SC7280_SLAVE_EPSS_L3_CPU3, SC7280_SLAVE_EPSS_L3_CPU4,
		  SC7280_SLAVE_EPSS_L3_CPU5, SC7280_SLAVE_EPSS_L3_CPU6,
		  SC7280_SLAVE_EPSS_L3_CPU7);
DEFINE_DCVS_QNODE(sc7280_epss_l3_shared, SC7280_SLAVE_EPSS_L3_SHARED, 32, 0, 0);
DEFINE_DCVS_QNODE(sc7280_epss_l3_cpu0, SC7280_SLAVE_EPSS_L3_CPU0, 32, 1, 0);
DEFINE_DCVS_QNODE(sc7280_epss_l3_cpu1, SC7280_SLAVE_EPSS_L3_CPU1, 32, 1, 1);
DEFINE_DCVS_QNODE(sc7280_epss_l3_cpu2, SC7280_SLAVE_EPSS_L3_CPU2, 32, 1, 2);
DEFINE_DCVS_QNODE(sc7280_epss_l3_cpu3, SC7280_SLAVE_EPSS_L3_CPU3, 32, 1, 3);
DEFINE_DCVS_QNODE(sc7280_epss_l3_cpu4, SC7280_SLAVE_EPSS_L3_CPU4, 32, 2, 0);
DEFINE_DCVS_QNODE(sc7280_epss_l3_cpu5, SC7280_SLAVE_EPSS_L3_CPU5, 32, 2, 1);
DEFINE_DCVS_QNODE(sc7280_epss_l3_cpu6, SC7280_SLAVE_EPSS_L3_CPU6, 32, 2, 2);
DEFINE_DCVS_QNODE(sc7280_epss_l3_cpu7, SC7280_SLAVE_EPSS_L3_CPU7, 32, 3, 0);

static const struct qcom_osm_l3_node *sc7280_epss_l3_nodes[] = {
	[MASTER_EPSS_L3_APPS] = &sc7280_epss_apps_l3,
	[SLAVE_EPSS_L3_SHARED] = &sc7280_epss_l3_shared,
	[SLAVE_EPSS_L3_CPU0] = &sc7280_epss_l3_cpu0,
	[SLAVE_EPSS_L3_CPU1] = &sc7280_epss_l3_cpu1,
	[SLAVE_EPSS_L3_CPU2] = &sc7280_epss_l3_cpu2,
	[SLAVE_EPSS_L3_CPU3] = &sc7280_epss_l3_cpu3,
	[SLAVE_EPSS_L3_CPU4] = &sc7280_epss_l3_cpu4,
	[SLAVE_EPSS_L3_CPU5] = &sc7280_epss_l3_cpu5,
	[SLAVE_EPSS_L3_CPU6] = &sc7280_epss_l3_cpu6,
	[SLAVE_EPSS_L3_CPU7] = &sc7280_epss_l3_cpu7,
};

static const struct qcom_osm_l3_desc sc7280_icc_epss_l3 = {
	.nodes = sc7280_epss_l3_nodes,
	.num_nodes = ARRAY_SIZE(sc7280_epss_l3_nodes),
	.per_core_dcvs = true,
	.lut_row_size = EPSS_LUT_ROW_SIZE,
	.reg_freq_lut = EPSS_REG_FREQ_LUT,
	.reg_perf_state = EPSS_REG_PERF_STATE,
};

static u32 qcom_osm_l3_aggregate_peak(struct icc_node *src, struct icc_node *dst)
{
	struct icc_node *n;
	u32 agg_peak = 0;
	u32 agg_avg = 0;
	struct qcom_osm_l3_icc_provider *qp;
	struct icc_provider *provider;

	provider = src->provider;
	qp = to_osm_l3_provider(provider);

	/* Skip aggregation when per core l3 scaling is enabled */
	if (qp->per_core_dcvs)
		return dst->peak_bw;

	list_for_each_entry(n, &provider->nodes, node_list)
		provider->aggregate(n, 0, n->avg_bw, n->peak_bw, &agg_avg, &agg_peak);

	agg_peak = max(agg_avg, agg_peak);

	return agg_peak;
}

static int qcom_osm_l3_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_osm_l3_icc_provider *qp;
	struct icc_provider *provider;
	const struct qcom_osm_l3_node *qn;
	unsigned int index;
	u64 rate;

	qn = dst->data;
	provider = src->provider;
	qp = to_osm_l3_provider(provider);

	rate = qcom_osm_l3_aggregate_peak(src, dst);
	rate = icc_units_to_bps(rate);
	do_div(rate, qn->buswidth);

	for (index = 0; index < qp->max_state - 1; index++) {
		if (qp->lut_tables[index] >= rate)
			break;
	}

	if (qp->per_core_dcvs)
		writel_relaxed(index, EPSS_L3_VOTE_REG(qp->domain_base[qn->domain], qn->cpu));
	else
		writel_relaxed(index, qp->domain_base[qn->domain] + qp->reg_perf_state);

	return 0;
}

static int qcom_osm_l3_remove(struct platform_device *pdev)
{
	struct qcom_osm_l3_icc_provider *qp = platform_get_drvdata(pdev);

	icc_nodes_remove(&qp->provider);
	return icc_provider_del(&qp->provider);
}

static int qcom_osm_l3_probe(struct platform_device *pdev)
{
	u32 info, src, lval, i = 0, prev_freq = 0, freq;
	static unsigned long hw_rate, xo_rate;
	struct qcom_osm_l3_icc_provider *qp;
	const struct qcom_osm_l3_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	const struct qcom_osm_l3_node **qnodes;
	struct icc_node *node;
	size_t num_nodes;
	struct clk *clk;
	int ret;

	clk = clk_get(&pdev->dev, "xo");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	xo_rate = clk_get_rate(clk);
	clk_put(clk);

	clk = clk_get(&pdev->dev, "alternate");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	hw_rate = clk_get_rate(clk) / CLK_HW_DIV;
	clk_put(clk);

	qp = devm_kzalloc(&pdev->dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	while (of_get_address(pdev->dev.of_node, i, NULL, NULL)) {
		qp->domain_base[i] = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(qp->domain_base[i]))
			return PTR_ERR(qp->domain_base[i]);
		i++;
	}

	/* HW should be in enabled state to proceed */
	if (!(readl_relaxed(qp->domain_base[0] + REG_ENABLE) & 0x1)) {
		dev_err(&pdev->dev, "error hardware not enabled\n");
		return -ENODEV;
	}

	desc = device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	qp->reg_perf_state = desc->reg_perf_state;

	for (i = 0; i < LUT_MAX_ENTRIES; i++) {
		info = readl_relaxed(qp->domain_base[0] + desc->reg_freq_lut +
				     i * desc->lut_row_size);
		src = FIELD_GET(LUT_SRC, info);
		lval = FIELD_GET(LUT_L_VAL, info);
		if (src)
			freq = xo_rate * lval;
		else
			freq = hw_rate;

		/* Two of the same frequencies signify end of table */
		if (i > 0 && prev_freq == freq)
			break;

		dev_dbg(&pdev->dev, "index=%d freq=%d\n", i, freq);

		qp->lut_tables[i] = freq;
		prev_freq = freq;
	}
	qp->max_state = i;
	qp->per_core_dcvs = desc->per_core_dcvs;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	data = devm_kcalloc(&pdev->dev, num_nodes, sizeof(*node), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	provider = &qp->provider;
	provider->dev = &pdev->dev;
	provider->set = qcom_osm_l3_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(&pdev->dev, "error adding interconnect provider\n");
		return ret;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		/* Cast away const and add it back in qcom_osm_l3_set() */
		node->data = (void *)qnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	platform_set_drvdata(pdev, qp);

	return 0;
err:
	icc_nodes_remove(provider);
	icc_provider_del(provider);

	return ret;
}

static const struct of_device_id osm_l3_of_match[] = {
	{ .compatible = "qcom,sc7180-osm-l3", .data = &sc7180_icc_osm_l3 },
	{ .compatible = "qcom,sc7280-epss-l3", .data = &sc7280_icc_epss_l3 },
	{ .compatible = "qcom,sdm845-osm-l3", .data = &sdm845_icc_osm_l3 },
	{ .compatible = "qcom,sm8150-osm-l3", .data = &sm8150_icc_osm_l3 },
	{ .compatible = "qcom,sc8180x-osm-l3", .data = &sc8180x_icc_osm_l3 },
	{ .compatible = "qcom,sm8250-epss-l3", .data = &sm8250_icc_epss_l3 },
	{ }
};
MODULE_DEVICE_TABLE(of, osm_l3_of_match);

static struct platform_driver osm_l3_driver = {
	.probe = qcom_osm_l3_probe,
	.remove = qcom_osm_l3_remove,
	.driver = {
		.name = "osm-l3",
		.of_match_table = osm_l3_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(osm_l3_driver);

MODULE_DESCRIPTION("Qualcomm OSM L3 interconnect driver");
MODULE_LICENSE("GPL v2");
