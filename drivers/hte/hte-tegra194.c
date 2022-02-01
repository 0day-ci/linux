// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021-2022 NVIDIA Corporation
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
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>

#define HTE_SUSPEND	0

/* HTE source clock TSC is 31.25MHz */
#define HTE_TS_CLK_RATE_HZ	31250000ULL
#define HTE_CLK_RATE_NS		32
#define HTE_TS_NS_SHIFT	__builtin_ctz(HTE_CLK_RATE_NS)

#define NV_AON_SLICE_INVALID	-1
#define NV_LINES_IN_SLICE	32

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
#define NV_AON_GPIO_PORT_CC_0		12
#define NV_AON_GPIO_PORT_CC_1		13
#define NV_AON_GPIO_PORT_CC_2		14
#define NV_AON_GPIO_PORT_CC_3		15
#define NV_AON_GPIO_PORT_CC_4		16
#define NV_AON_GPIO_PORT_CC_5		17
#define NV_AON_GPIO_PORT_CC_6		18
#define NV_AON_GPIO_PORT_CC_7		19

/* AON GPIO port DD pins */
#define NV_AON_GPIO_PORT_DD_0		20
#define NV_AON_GPIO_PORT_DD_1		21
#define NV_AON_GPIO_PORT_DD_2		22

/* AON GPIO port EE pins */
#define NV_AON_GPIO_PORT_EE_0		23
#define NV_AON_GPIO_PORT_EE_1		24
#define NV_AON_GPIO_PORT_EE_2		25
#define NV_AON_GPIO_PORT_EE_3		26
#define NV_AON_GPIO_PORT_EE_4		27
#define NV_AON_GPIO_PORT_EE_5		28
#define NV_AON_GPIO_PORT_EE_6		29


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

enum tegra_hte_type {
	HTE_TEGRA_TYPE_GPIO = 1U << 0,
	HTE_TEGRA_TYPE_LIC = 1U << 1,
};

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

struct tegra_hte_line_data {
	unsigned long flags;
	void *data;
};

struct tegra_hte_data {
	enum tegra_hte_type type;
	u32 map_sz;
	const struct tegra_hte_line_mapped *map;
};

struct tegra_hte_soc {
	int hte_irq;
	u32 itr_thrshld;
	u32 conf_rval;
	struct hte_slices *sl;
	const struct tegra_hte_data *prov_data;
	struct tegra_hte_line_data *line_data;
	struct hte_chip *chip;
	struct gpio_chip *c;
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
	[NV_AON_GPIO_PORT_EE_0] = {1, NV_AON_HTE_SLICE1_IRQ_GPIO_29},
	[NV_AON_GPIO_PORT_EE_1] = {1, NV_AON_HTE_SLICE1_IRQ_GPIO_28},
	[NV_AON_GPIO_PORT_EE_2] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_27},
	[NV_AON_GPIO_PORT_EE_3] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_26},
	[NV_AON_GPIO_PORT_EE_4] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_25},
	[NV_AON_GPIO_PORT_EE_5] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_24},
	[NV_AON_GPIO_PORT_EE_6] = {2, NV_AON_HTE_SLICE2_IRQ_GPIO_23},
};

static const struct tegra_hte_data aon_hte = {
	.map_sz = ARRAY_SIZE(tegra194_aon_gpio_map),
	.map = tegra194_aon_gpio_map,
	.type = HTE_TEGRA_TYPE_GPIO,
};

static const struct tegra_hte_data lic_hte = {
	.map_sz = 0,
	.map = NULL,
	.type = HTE_TEGRA_TYPE_LIC,
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

	if (gs->prov_data && gs->prov_data->map) {
		m = gs->prov_data->map;
		if (eid > gs->prov_data->map_sz)
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
	u32 line_id;
	struct tegra_hte_soc *gs;

	if (!gc || !desc || !xlated_id)
		return -EINVAL;

	if (args) {
		if (gc->of_hte_n_cells < 1)
			return -EINVAL;

		if (args->args_count != gc->of_hte_n_cells)
			return -EINVAL;

		desc->attr.line_id = args->args[0];
	}

	gs = gc->data;
	if (!gs)
		return -EINVAL;

	/*
	 * GPIO consumers send base + offset as line id which is a GPIO
	 * logical id in GPIO namespace, convert it into the hardware GPIO
	 * number before mapping it into HTE/GTE namespace.
	 */
	if (gs->prov_data->type == HTE_TEGRA_TYPE_GPIO)
		line_id = desc->attr.line_id - gs->c->base;
	else
		line_id = desc->attr.line_id;

	ret = tegra_hte_map_to_line_id(line_id, gc->data,
				       xlated_id);
	if (ret < 0) {
		dev_dbg(gc->dev, "line_id:%u mapping failed\n",
			desc->attr.line_id);
		return ret;
	}

	if (*xlated_id > gc->nlines)
		return -EINVAL;

	dev_dbg(gc->dev, "requested id:%u, xlated id:%u\n",
		desc->attr.line_id, *xlated_id);

	return 0;
}

static int tegra_hte_en_dis_common(struct hte_chip *chip, u32 line_id, bool en)
{
	u32 slice, sl_bit_shift, line_bit, val, reg;
	struct tegra_hte_soc *gs;

	sl_bit_shift = __builtin_ctz(HTE_SLICE_SIZE);

	if (!chip)
		return -EINVAL;

	gs = chip->data;

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

static int tegra_hte_enable(struct hte_chip *chip, u32 line_id)
{
	if (!chip)
		return -EINVAL;

	return tegra_hte_en_dis_common(chip, line_id, true);
}

static int tegra_hte_disable(struct hte_chip *chip, u32 line_id)
{
	if (!chip)
		return -EINVAL;

	return tegra_hte_en_dis_common(chip, line_id, false);
}

static int tegra_hte_request(struct hte_chip *chip, struct hte_ts_desc *desc,
			     u32 line_id)
{
	int ret;
	struct tegra_hte_soc *gs;
	struct hte_line_attr *attr;

	if (!chip || !chip->data || !desc)
		return -EINVAL;

	gs = chip->data;
	attr = &desc->attr;

	if (gs->prov_data->type == HTE_TEGRA_TYPE_GPIO) {
		ret = gpiod_enable_hw_timestamp(attr->line_data,
						attr->edge_flags);
		if (ret)
			return ret;

		gs->line_data[line_id].data = attr->line_data;
		gs->line_data[line_id].flags = attr->edge_flags;
	}

	return tegra_hte_en_dis_common(chip, line_id, true);
}

static int tegra_hte_release(struct hte_chip *chip, struct hte_ts_desc *desc,
			     u32 line_id)
{
	struct tegra_hte_soc *gs;
	struct hte_line_attr *attr;
	int ret;

	if (!chip || !chip->data || !desc)
		return -EINVAL;

	gs = chip->data;
	attr = &desc->attr;

	if (gs->prov_data->type == HTE_TEGRA_TYPE_GPIO) {
		ret = gpiod_disable_hw_timestamp(attr->line_data,
						 gs->line_data[line_id].flags);
		if (ret)
			return ret;

		gs->line_data[line_id].data = NULL;
		gs->line_data[line_id].flags = 0;
	}

	return tegra_hte_en_dis_common(chip, line_id, false);
}

static int tegra_hte_clk_src_info(struct hte_chip *chip,
				  struct hte_clk_info *ci)
{
	(void)chip;

	if (!ci)
		return -EINVAL;

	ci->hz = HTE_TS_CLK_RATE_HZ;
	ci->type = CLOCK_MONOTONIC;

	return 0;
}

int tegra_hte_get_level(struct tegra_hte_soc *gs, u32 line_id)
{
	struct gpio_desc *desc;

	if (gs->prov_data->type == HTE_TEGRA_TYPE_GPIO) {
		desc = gs->line_data[line_id].data;
		if (desc)
			return gpiod_get_raw_value(desc);
	}

	return -1;
}

static void tegra_hte_read_fifo(struct tegra_hte_soc *gs)
{
	u32 tsh, tsl, src, pv, cv, acv, slice, bit_index, line_id;
	u64 tsc;
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
			line_id = bit_index + (slice << 5);
			el.tsc = tsc << HTE_TS_NS_SHIFT;
			el.raw_level = tegra_hte_get_level(gs, line_id);
			hte_push_ts_ns(gs->chip, line_id, &el);
			acv &= ~BIT(bit_index);
		}
		tegra_hte_writel(gs, HTE_TECMD, HTE_TECMD_CMD_POP);
	}
}

static irqreturn_t tegra_hte_isr(int irq, void *dev_id)
{
	struct tegra_hte_soc *gs = dev_id;
	(void)irq;

	tegra_hte_read_fifo(gs);

	return IRQ_HANDLED;
}

bool tegra_hte_match_from_linedata(const struct hte_chip *chip,
				   const struct hte_ts_desc *hdesc)
{
	struct tegra_hte_soc *hte_dev = chip->data;

	if (!hte_dev || (hte_dev->prov_data->type != HTE_TEGRA_TYPE_GPIO))
		return false;

	return hte_dev->c == gpiod_to_chip(hdesc->attr.line_data);
}

static const struct of_device_id tegra_hte_of_match[] = {
	{ .compatible = "nvidia,tegra194-gte-lic", .data = &lic_hte},
	{ .compatible = "nvidia,tegra194-gte-aon", .data = &aon_hte},
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_hte_of_match);

static const struct hte_ops g_ops = {
	.request = tegra_hte_request,
	.release = tegra_hte_release,
	.enable = tegra_hte_enable,
	.disable = tegra_hte_disable,
	.get_clk_src_info = tegra_hte_clk_src_info,
};

static void tegra_gte_disable(void *data)
{
	struct platform_device *pdev = data;
	struct tegra_hte_soc *gs = dev_get_drvdata(&pdev->dev);

	tegra_hte_writel(gs, HTE_TECTRL, 0);
}

static int tegra_get_gpiochip_from_name(struct gpio_chip *chip, void *data)
{
	return !strcmp(chip->label, data);
}

static int tegra_hte_probe(struct platform_device *pdev)
{
	int ret;
	u32 i, slices, val = 0;
	u32 nlines;
	struct device *dev;
	struct tegra_hte_soc *hte_dev;
	struct hte_chip *gc;

	dev = &pdev->dev;

	ret = of_property_read_u32(dev->of_node, "nvidia,slices", &slices);
	if (ret != 0) {
		dev_err(dev, "Could not read slices\n");
		return -EINVAL;
	}
	nlines = slices << 5;

	hte_dev = devm_kzalloc(dev, sizeof(*hte_dev), GFP_KERNEL);
	if (!hte_dev)
		return -ENOMEM;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, hte_dev);
	hte_dev->prov_data = of_device_get_match_data(&pdev->dev);

	hte_dev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hte_dev->regs))
		return PTR_ERR(hte_dev->regs);

	ret = of_property_read_u32(dev->of_node, "nvidia,int-threshold",
				   &hte_dev->itr_thrshld);
	if (ret != 0)
		hte_dev->itr_thrshld = 1;

	hte_dev->sl = devm_kcalloc(dev, slices, sizeof(*hte_dev->sl),
				   GFP_KERNEL);
	if (!hte_dev->sl)
		return -ENOMEM;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err_probe(dev, ret, "failed to get irq\n");
		return ret;
	}
	hte_dev->hte_irq = ret;
	ret = devm_request_irq(dev, hte_dev->hte_irq, tegra_hte_isr, 0,
			       dev_name(dev), hte_dev);
	if (ret < 0) {
		dev_err(dev, "request irq failed.\n");
		return ret;
	}

	gc->nlines = nlines;
	gc->ops = &g_ops;
	gc->dev = dev;
	gc->data = hte_dev;
	gc->xlate = tegra_hte_line_xlate;
	gc->of_hte_n_cells = 1;

	if (hte_dev->prov_data &&
	    hte_dev->prov_data->type == HTE_TEGRA_TYPE_GPIO) {
		hte_dev->line_data = devm_kcalloc(dev, nlines,
						  sizeof(*hte_dev->line_data),
						  GFP_KERNEL);
		if (!hte_dev->line_data)
			return -ENOMEM;

		gc->match_from_linedata = tegra_hte_match_from_linedata;

		hte_dev->c = gpiochip_find("tegra194-gpio-aon",
					   tegra_get_gpiochip_from_name);
		if (!hte_dev->c)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "wait for gpio controller\n");
	}

	hte_dev->chip = gc;

	ret = devm_hte_register_chip(hte_dev->chip);
	if (ret) {
		dev_err(gc->dev, "hte chip register failed");
		return ret;
	}

	for (i = 0; i < slices; i++) {
		hte_dev->sl[i].flags = 0;
		spin_lock_init(&hte_dev->sl[i].s_lock);
	}

	val = HTE_TECTRL_ENABLE_ENABLE |
	      (HTE_TECTRL_INTR_ENABLE << HTE_TECTRL_INTR_SHIFT) |
	      (hte_dev->itr_thrshld << HTE_TECTRL_OCCU_SHIFT);
	tegra_hte_writel(hte_dev, HTE_TECTRL, val);

	ret = devm_add_action_or_reset(&pdev->dev, tegra_gte_disable, pdev);
	if (ret)
		return ret;

	dev_dbg(gc->dev, "lines: %d, slices:%d", gc->nlines, slices);

	return 0;
}

static int __maybe_unused tegra_hte_resume_early(struct device *dev)
{
	u32 i;
	struct tegra_hte_soc *gs = dev_get_drvdata(dev);
	u32 slices = gs->chip->nlines / NV_LINES_IN_SLICE;
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

static int __maybe_unused tegra_hte_suspend_late(struct device *dev)
{
	u32 i;
	struct tegra_hte_soc *gs = dev_get_drvdata(dev);
	u32 slices = gs->chip->nlines / NV_LINES_IN_SLICE;
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

static const struct dev_pm_ops tegra_hte_pm = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(tegra_hte_suspend_late,
				     tegra_hte_resume_early)
};

static struct platform_driver tegra_hte_driver = {
	.probe = tegra_hte_probe,
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
