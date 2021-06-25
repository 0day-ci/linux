// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 NVIDIA Corporation
 *
 * Author: Dipen Patel <dipenp@nvidia.com>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/hte.h>
#include <linux/uaccess.h>

#define HTE_SUSPEND	0

/* HTE source clock TSC is 31.25MHz */
#define HTE_TS_CLK_RATE_HZ	31250000ULL
#define HTE_CLK_RATE_NS		32
#define HTE_TS_NS_SHIFT	__builtin_ctz(HTE_CLK_RATE_NS)

#define NV_AON_SLICE_INVALID	-1

/* AON HTE line map For slice 1 */
#define NV_AON_HTE_SLICE1_IRQ_GPIO_28	12
#define NV_AON_HTE_SLICE1_IRQ_GPIO_29	13

/* AON HTE line map For slice 2 */
#define NV_AON_HTE_SLICE2_IRQ_GPIO_0	0
#define NV_AON_HTE_SLICE2_IRQ_GPIO_1	1
#define NV_AON_HTE_SLICE2_IRQ_GPIO_2	2
#define NV_AON_HTE_SLICE2_IRQ_GPIO_3	3
#define NV_AON_HTE_SLICE2_IRQ_GPIO_4	4
#define NV_AON_HTE_SLICE2_IRQ_GPIO_5	5
#define NV_AON_HTE_SLICE2_IRQ_GPIO_6	6
#define NV_AON_HTE_SLICE2_IRQ_GPIO_7	7
#define NV_AON_HTE_SLICE2_IRQ_GPIO_8	8
#define NV_AON_HTE_SLICE2_IRQ_GPIO_9	9
#define NV_AON_HTE_SLICE2_IRQ_GPIO_10	10
#define NV_AON_HTE_SLICE2_IRQ_GPIO_11	11
#define NV_AON_HTE_SLICE2_IRQ_GPIO_12	12
#define NV_AON_HTE_SLICE2_IRQ_GPIO_13	13
#define NV_AON_HTE_SLICE2_IRQ_GPIO_14	14
#define NV_AON_HTE_SLICE2_IRQ_GPIO_15	15
#define NV_AON_HTE_SLICE2_IRQ_GPIO_16	16
#define NV_AON_HTE_SLICE2_IRQ_GPIO_17	17
#define NV_AON_HTE_SLICE2_IRQ_GPIO_18	18
#define NV_AON_HTE_SLICE2_IRQ_GPIO_19	19
#define NV_AON_HTE_SLICE2_IRQ_GPIO_20	20
#define NV_AON_HTE_SLICE2_IRQ_GPIO_21	21
#define NV_AON_HTE_SLICE2_IRQ_GPIO_22	22
#define NV_AON_HTE_SLICE2_IRQ_GPIO_23	23
#define NV_AON_HTE_SLICE2_IRQ_GPIO_24	24
#define NV_AON_HTE_SLICE2_IRQ_GPIO_25	25
#define NV_AON_HTE_SLICE2_IRQ_GPIO_26	26
#define NV_AON_HTE_SLICE2_IRQ_GPIO_27	27

/* AON GPIO port AA pins */
#define NV_AON_GPIO_PORT_AA_0		0
#define NV_AON_GPIO_PORT_AA_1		1
#define NV_AON_GPIO_PORT_AA_2		2
#define NV_AON_GPIO_PORT_AA_3		3
#define NV_AON_GPIO_PORT_AA_4		4
#define NV_AON_GPIO_PORT_AA_5		5
#define NV_AON_GPIO_PORT_AA_6		6
#define NV_AON_GPIO_PORT_AA_7		7

/* AON GPIO port BB pins */
#define NV_AON_GPIO_PORT_BB_0		8
#define NV_AON_GPIO_PORT_BB_1		9
#define NV_AON_GPIO_PORT_BB_2		10
#define NV_AON_GPIO_PORT_BB_3		11

/* AON GPIO port CC pins */
#define NV_AON_GPIO_PORT_CC_0		16
#define NV_AON_GPIO_PORT_CC_1		17
#define NV_AON_GPIO_PORT_CC_2		18
#define NV_AON_GPIO_PORT_CC_3		19
#define NV_AON_GPIO_PORT_CC_4		20
#define NV_AON_GPIO_PORT_CC_5		21
#define NV_AON_GPIO_PORT_CC_6		22
#define NV_AON_GPIO_PORT_CC_7		23

/* AON GPIO port DD pins */
#define NV_AON_GPIO_PORT_DD_0		24
#define NV_AON_GPIO_PORT_DD_1		25
#define NV_AON_GPIO_PORT_DD_2		26

/* AON GPIO port EE pins */
#define NV_AON_GPIO_PORT_EE_0		32
#define NV_AON_GPIO_PORT_EE_1		33
#define NV_AON_GPIO_PORT_EE_2		34
#define NV_AON_GPIO_PORT_EE_3		35
#define NV_AON_GPIO_PORT_EE_4		36
#define NV_AON_GPIO_PORT_EE_5		37
#define NV_AON_GPIO_PORT_EE_6		38


#define HTE_TECTRL		0x0
#define HTE_TETSCH		0x4
#define HTE_TETSCL		0x8
#define HTE_TESRC		0xC
#define HTE_TECCV		0x10
#define HTE_TEPCV		0x14
#define HTE_TECMD		0x1C
#define HTE_TESTATUS		0x20
#define HTE_SLICE0_TETEN	0x40
#define HTE_SLICE1_TETEN	0x60

#define HTE_SLICE_SIZE		(HTE_SLICE1_TETEN - HTE_SLICE0_TETEN)

#define HTE_TECTRL_ENABLE_ENABLE	0x1

#define HTE_TECTRL_OCCU_SHIFT		0x8
#define HTE_TECTRL_INTR_SHIFT		0x1
#define HTE_TECTRL_INTR_ENABLE		0x1

#define HTE_TESRC_SLICE_SHIFT		16
#define HTE_TESRC_SLICE_DEFAULT_MASK	0xFF

#define HTE_TECMD_CMD_POP		0x1

#define HTE_TESTATUS_OCCUPANCY_SHIFT	8
#define HTE_TESTATUS_OCCUPANCY_MASK	0xFF

struct hte_slices {
	u32 r_val;
	unsigned long flags;
	/* to prevent lines mapped to same slice updating its register */
	spinlock_t s_lock;
};

struct tegra_hte_line_mapped {
	int slice;
	u32 bit_index;
};

struct tegra_hte_line_table {
	int map_sz;
	const struct tegra_hte_line_mapped *map;
};

struct tegra_hte_soc {
	int hte_irq;
	u32 itr_thrshld;
	u32 conf_rval;
	struct hte_slices *sl;
	const struct tegra_hte_line_table *line_map;
	struct hte_chip *chip;
	void __iomem *regs;
};

static const struct tegra_hte_line_mapped tegra194_aon_gpio_map[] = {
	/* gpio, slice, bit_index */
	[NV_AON_GPIO_PORT_AA_0]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_11},
	[NV_AON_GPIO_PORT_AA_1]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_10},
	[NV_AON_GPIO_PORT_AA_2]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_9},
	[NV_AON_GPIO_PORT_AA_3]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_8},
	[NV_AON_GPIO_PORT_AA_4]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_7},
	[NV_AON_GPIO_PORT_AA_5]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_6},
	[NV_AON_GPIO_PORT_AA_6]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_5},
	[NV_AON_GPIO_PORT_AA_7]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_4},
	[NV_AON_GPIO_PORT_BB_0]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_3},
	[NV_AON_GPIO_PORT_BB_1]  = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_2},
	[NV_AON_GPIO_PORT_BB_2] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_1},
	[NV_AON_GPIO_PORT_BB_3] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_0},
	[12] = {NV_AON_SLICE_INVALID, 0},
	[13] = {NV_AON_SLICE_INVALID, 0},
	[14] = {NV_AON_SLICE_INVALID, 0},
	[15] = {NV_AON_SLICE_INVALID, 0},
	[NV_AON_GPIO_PORT_CC_0] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_22},
	[NV_AON_GPIO_PORT_CC_1] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_21},
	[NV_AON_GPIO_PORT_CC_2] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_20},
	[NV_AON_GPIO_PORT_CC_3] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_19},
	[NV_AON_GPIO_PORT_CC_4] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_18},
	[NV_AON_GPIO_PORT_CC_5] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_17},
	[NV_AON_GPIO_PORT_CC_6] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_16},
	[NV_AON_GPIO_PORT_CC_7] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_15},
	[NV_AON_GPIO_PORT_DD_0] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_14},
	[NV_AON_GPIO_PORT_DD_1] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_13},
	[NV_AON_GPIO_PORT_DD_2] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_12},
	[27] = {NV_AON_SLICE_INVALID, 0},
	[28] = {NV_AON_SLICE_INVALID, 0},
	[29] = {NV_AON_SLICE_INVALID, 0},
	[30] = {NV_AON_SLICE_INVALID, 0},
	[31] = {NV_AON_SLICE_INVALID, 0},
	[NV_AON_GPIO_PORT_EE_0] = {1, NV_AON_HTE_SLICE1_IRQ_GPIO_29},
	[NV_AON_GPIO_PORT_EE_1] = {1, NV_AON_HTE_SLICE1_IRQ_GPIO_28},
	[NV_AON_GPIO_PORT_EE_2] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_27},
	[NV_AON_GPIO_PORT_EE_3] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_26},
	[NV_AON_GPIO_PORT_EE_4] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_25},
	[NV_AON_GPIO_PORT_EE_5] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_24},
	[NV_AON_GPIO_PORT_EE_6] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_23},
};

static const struct tegra_hte_line_table aon_hte_map = {
	.map_sz = ARRAY_SIZE(tegra194_aon_gpio_map),
	.map = tegra194_aon_gpio_map,
};

static inline u32 tegra_hte_readl(struct tegra_hte_soc *hte, u32 reg)
{
	return readl(hte->regs + reg);
}

static inline void tegra_hte_writel(struct tegra_hte_soc *hte, u32 reg,
				    u32 val)
{
	writel(val, hte->regs + reg);
}

static inline int tegra_hte_map_to_line_id(u32 eid, struct tegra_hte_soc *gs,
					  u32 *mapped)
{
	const struct tegra_hte_line_mapped *m;

	if (gs->line_map) {
		m = gs->line_map->map;
		if (eid > gs->line_map->map_sz)
			return -EINVAL;
		if (m[eid].slice == NV_AON_SLICE_INVALID)
			return -EINVAL;

		*mapped = (m[eid].slice << 5) + m[eid].bit_index;
	} else {
		*mapped = eid;
	}

	return 0;
}

static int tegra_hte_line_xlate(struct hte_chip *gc,
				 const struct of_phandle_args *args,
				 struct hte_ts_desc *desc, u32 *xlated_id)
{
	int ret = 0;

	if (!gc || !desc || !xlated_id)
		return -EINVAL;

	if (args) {
		if (gc->of_hte_n_cells < 1)
			return -EINVAL;

		if (args->args_count != gc->of_hte_n_cells)
			return -EINVAL;

		desc->con_id = args->args[0];
	}

	ret = tegra_hte_map_to_line_id(desc->con_id, gc->data,
				       xlated_id);
	if (ret < 0) {
		dev_dbg(gc->dev, "con_id:%u mapping failed\n",
			desc->con_id);
		return ret;
	}

	if (*xlated_id > gc->nlines)
		return -EINVAL;

	dev_dbg(gc->dev, "requested id:%u, xlated id:%u\n",
		desc->con_id, *xlated_id);

	return 0;
}

static int tegra_hte_en_dis_common(struct hte_chip *chip, u32 line_id, bool en)
{
	u32 slice, sl_bit_shift, line_bit, val, reg;
	struct tegra_hte_soc *gs;

	sl_bit_shift = __builtin_ctz(HTE_SLICE_SIZE);

	if (!chip)
		return -EINVAL;

	gs = (struct tegra_hte_soc *)chip->data;

	if (line_id > chip->nlines) {
		dev_err(chip->dev,
			"line id: %u is not supported by this controller\n",
			line_id);
		return -EINVAL;
	}

	slice = line_id >> sl_bit_shift;
	line_bit = line_id & (HTE_SLICE_SIZE - 1);
	reg = (slice << sl_bit_shift) + HTE_SLICE0_TETEN;

	spin_lock(&gs->sl[slice].s_lock);

	if (test_bit(HTE_SUSPEND, &gs->sl[slice].flags)) {
		spin_unlock(&gs->sl[slice].s_lock);
		dev_dbg(chip->dev, "device suspended");
		return -EBUSY;
	}

	val = tegra_hte_readl(gs, reg);
	if (en)
		val = val | (1 << line_bit);
	else
		val = val & (~(1 << line_bit));
	tegra_hte_writel(gs, reg, val);

	spin_unlock(&gs->sl[slice].s_lock);

	dev_dbg(chip->dev, "line: %u, slice %u, line_bit %u, reg:0x%x\n",
		line_id, slice, line_bit, reg);

	return 0;
}

static int tegra_hte_request(struct hte_chip *chip, u32 line_id)
{
	return tegra_hte_en_dis_common(chip, line_id, true);
}

static int tegra_hte_release(struct hte_chip *chip, u32 line_id)
{
	return tegra_hte_en_dis_common(chip, line_id, false);
}

static int tegra_hte_clk_src_info(struct hte_chip *chip,
				  struct hte_clk_info *ci)
{
	(void)chip;

	ci->hz = HTE_TS_CLK_RATE_HZ;
	ci->type = CLOCK_MONOTONIC;

	return 0;
}

static void tegra_hte_read_fifo(struct tegra_hte_soc *gs)
{
	u32 tsh, tsl, src, pv, cv, acv, slice, bit_index, line_id;
	u64 tsc;
	int dir;
	struct hte_ts_data el;

	while ((tegra_hte_readl(gs, HTE_TESTATUS) >>
		HTE_TESTATUS_OCCUPANCY_SHIFT) &
		HTE_TESTATUS_OCCUPANCY_MASK) {
		tsh = tegra_hte_readl(gs, HTE_TETSCH);
		tsl = tegra_hte_readl(gs, HTE_TETSCL);
		tsc = (((u64)tsh << 32) | tsl);

		src = tegra_hte_readl(gs, HTE_TESRC);
		slice = (src >> HTE_TESRC_SLICE_SHIFT) &
			    HTE_TESRC_SLICE_DEFAULT_MASK;

		pv = tegra_hte_readl(gs, HTE_TEPCV);
		cv = tegra_hte_readl(gs, HTE_TECCV);
		acv = pv ^ cv;
		while (acv) {
			bit_index = __builtin_ctz(acv);
			if ((pv >> bit_index) & BIT(0))
				dir = HTE_EVENT_RISING_EDGE;
			else
				dir = HTE_EVENT_FALLING_EDGE;

			line_id = bit_index + (slice << 5);
			el.dir = dir;
			el.tsc = tsc << HTE_TS_NS_SHIFT;
			hte_push_ts_ns_atomic(gs->chip, line_id, &el,
					      sizeof(el));
			acv &= ~BIT(bit_index);
		}
		tegra_hte_writel(gs, HTE_TECMD, HTE_TECMD_CMD_POP);
	}
}

static irqreturn_t tegra_hte_isr(int irq, void *dev_id)
{
	struct tegra_hte_soc *gs = dev_id;

	tegra_hte_read_fifo(gs);

	return IRQ_HANDLED;
}

static const struct of_device_id tegra_hte_of_match[] = {
	{ .compatible = "nvidia,tegra194-gte-lic"},
	{ .compatible = "nvidia,tegra194-gte-aon", .data = &aon_hte_map},
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_hte_of_match);

static const struct hte_ops g_ops = {
	.request = tegra_hte_request,
	.release = tegra_hte_release,
	.enable = tegra_hte_request,
	.disable = tegra_hte_release,
	.get_clk_src_info = tegra_hte_clk_src_info,
};

static int tegra_hte_probe(struct platform_device *pdev)
{
	int ret;
	u32 i, slices, val = 0;
	struct device *dev;
	struct tegra_hte_soc *hte_dev;
	struct hte_chip *gc;

	dev = &pdev->dev;

	hte_dev = devm_kzalloc(dev, sizeof(*hte_dev), GFP_KERNEL);
	if (!hte_dev)
		return -ENOMEM;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, hte_dev);
	hte_dev->line_map = of_device_get_match_data(&pdev->dev);

	hte_dev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hte_dev->regs))
		return PTR_ERR(hte_dev->regs);

	ret = of_property_read_u32(dev->of_node, "int-threshold",
				   &hte_dev->itr_thrshld);
	if (ret != 0)
		hte_dev->itr_thrshld = 1;

	ret = of_property_read_u32(dev->of_node, "slices", &slices);
	if (ret != 0) {
		dev_err(dev, "Could not read slices\n");
		return -EINVAL;
	}

	hte_dev->sl = devm_kzalloc(dev, sizeof(struct hte_slices) * slices,
				   GFP_KERNEL);
	if (!hte_dev->sl)
		return -ENOMEM;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "get irq failed.\n");
		return ret;
	}
	hte_dev->hte_irq = ret;
	ret = devm_request_irq(dev, hte_dev->hte_irq, tegra_hte_isr, 0,
			       dev_name(dev), hte_dev);
	if (ret < 0) {
		dev_err(dev, "request irq failed.\n");
		return ret;
	}

	gc->nlines = slices << 5;
	gc->ops = &g_ops;
	gc->dev = dev;
	hte_dev->chip = gc;
	gc->data = (void *)hte_dev;
	gc->xlate = tegra_hte_line_xlate;
	gc->of_hte_n_cells = 1;

	ret = hte_register_chip(hte_dev->chip);

	if (ret)
		dev_err(gc->dev, "hte chip register failed");

	for (i = 0; i < slices; i++) {
		hte_dev->sl[i].flags = 0;
		spin_lock_init(&hte_dev->sl[i].s_lock);
	}

	val = HTE_TECTRL_ENABLE_ENABLE |
	      (HTE_TECTRL_INTR_ENABLE << HTE_TECTRL_INTR_SHIFT) |
	      (hte_dev->itr_thrshld << HTE_TECTRL_OCCU_SHIFT);
	tegra_hte_writel(hte_dev, HTE_TECTRL, val);

	dev_dbg(gc->dev, "lines: %d, slices:%d", gc->nlines, slices);
	return 0;
}

static int tegra_hte_remove(struct platform_device *pdev)
{
	struct tegra_hte_soc *gs = dev_get_drvdata(&pdev->dev);

	tegra_hte_writel(gs, HTE_TECTRL, 0);

	return hte_unregister_chip(gs->chip);
}

#ifdef CONFIG_PM_SLEEP
static int tegra_hte_resume_early(struct device *dev)
{
	u32 i;
	struct tegra_hte_soc *gs = dev_get_drvdata(dev);
	u32 slices = gs->chip->nlines >> 5;
	u32 sl_bit_shift = __builtin_ctz(HTE_SLICE_SIZE);

	tegra_hte_writel(gs, HTE_TECTRL, gs->conf_rval);

	for (i = 0; i < slices; i++) {
		spin_lock(&gs->sl[i].s_lock);
		tegra_hte_writel(gs,
				 ((i << sl_bit_shift) + HTE_SLICE0_TETEN),
				 gs->sl[i].r_val);
		clear_bit(HTE_SUSPEND, &gs->sl[i].flags);
		spin_unlock(&gs->sl[i].s_lock);
	}

	return 0;
}

static int tegra_hte_suspend_late(struct device *dev)
{
	u32 i;
	struct tegra_hte_soc *gs = dev_get_drvdata(dev);
	u32 slices = gs->chip->nlines >> 5;
	u32 sl_bit_shift = __builtin_ctz(HTE_SLICE_SIZE);

	gs->conf_rval = tegra_hte_readl(gs, HTE_TECTRL);
	for (i = 0; i < slices; i++) {
		spin_lock(&gs->sl[i].s_lock);
		gs->sl[i].r_val = tegra_hte_readl(gs,
				((i << sl_bit_shift) + HTE_SLICE0_TETEN));
		set_bit(HTE_SUSPEND, &gs->sl[i].flags);
		spin_unlock(&gs->sl[i].s_lock);
	}

	return 0;
}
#endif

static const struct dev_pm_ops tegra_hte_pm = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(tegra_hte_suspend_late,
				     tegra_hte_resume_early)
};

static struct platform_driver tegra_hte_driver = {
	.probe = tegra_hte_probe,
	.remove = tegra_hte_remove,
	.driver = {
		.name = "tegra_hte",
		.pm = &tegra_hte_pm,
		.of_match_table = tegra_hte_of_match,
	},
};

module_platform_driver(tegra_hte_driver);

MODULE_AUTHOR("Dipen Patel <dipenp@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra HTE (Hardware Timestamping Engine) driver");
MODULE_LICENSE("GPL v2");
