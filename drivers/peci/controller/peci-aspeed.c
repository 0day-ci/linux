// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2012-2017 ASPEED Technology Inc.
// Copyright (c) 2018-2021 Intel Corporation

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/peci.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/* ASPEED PECI Registers */
/* Control Register */
#define ASPEED_PECI_CTRL			0x00
#define   ASPEED_PECI_CTRL_SAMPLING_MASK	GENMASK(19, 16)
#define   ASPEED_PECI_CTRL_RD_MODE_MASK		GENMASK(13, 12)
#define     ASPEED_PECI_CTRL_RD_MODE_DBG	BIT(13)
#define     ASPEED_PECI_CTRL_RD_MODE_COUNT	BIT(12)
#define   ASPEED_PECI_CTRL_CLK_SOURCE		BIT(11)
#define   ASPEED_PECI_CTRL_CLK_DIV_MASK		GENMASK(10, 8)
#define   ASPEED_PECI_CTRL_INVERT_OUT		BIT(7)
#define   ASPEED_PECI_CTRL_INVERT_IN		BIT(6)
#define   ASPEED_PECI_CTRL_BUS_CONTENTION_EN	BIT(5)
#define   ASPEED_PECI_CTRL_PECI_EN		BIT(4)
#define   ASPEED_PECI_CTRL_PECI_CLK_EN		BIT(0)

/* Timing Negotiation Register */
#define ASPEED_PECI_TIMING_NEGOTIATION		0x04
#define   ASPEED_PECI_T_NEGO_MSG_MASK		GENMASK(15, 8)
#define   ASPEED_PECI_T_NEGO_ADDR_MASK		GENMASK(7, 0)

/* Command Register */
#define ASPEED_PECI_CMD				0x08
#define   ASPEED_PECI_CMD_PIN_MONITORING	BIT(31)
#define   ASPEED_PECI_CMD_STS_MASK		GENMASK(27, 24)
#define     ASPEED_PECI_CMD_STS_ADDR_T_NEGO	0x3
#define   ASPEED_PECI_CMD_IDLE_MASK		\
	  (ASPEED_PECI_CMD_STS_MASK | ASPEED_PECI_CMD_PIN_MONITORING)
#define   ASPEED_PECI_CMD_FIRE			BIT(0)

/* Read/Write Length Register */
#define ASPEED_PECI_RW_LENGTH			0x0c
#define   ASPEED_PECI_AW_FCS_EN			BIT(31)
#define   ASPEED_PECI_RD_LEN_MASK		GENMASK(23, 16)
#define   ASPEED_PECI_WR_LEN_MASK		GENMASK(15, 8)
#define   ASPEED_PECI_TARGET_ADDR_MASK		GENMASK(7, 0)

/* Expected FCS Data Register */
#define ASPEED_PECI_EXPECTED_FCS		0x10
#define   ASPEED_PECI_EXPECTED_RD_FCS_MASK	GENMASK(23, 16)
#define   ASPEED_PECI_EXPECTED_AW_FCS_AUTO_MASK	GENMASK(15, 8)
#define   ASPEED_PECI_EXPECTED_WR_FCS_MASK	GENMASK(7, 0)

/* Captured FCS Data Register */
#define ASPEED_PECI_CAPTURED_FCS		0x14
#define   ASPEED_PECI_CAPTURED_RD_FCS_MASK	GENMASK(23, 16)
#define   ASPEED_PECI_CAPTURED_WR_FCS_MASK	GENMASK(7, 0)

/* Interrupt Register */
#define ASPEED_PECI_INT_CTRL			0x18
#define   ASPEED_PECI_TIMING_NEGO_SEL_MASK	GENMASK(31, 30)
#define     ASPEED_PECI_1ST_BIT_OF_ADDR_NEGO	0
#define     ASPEED_PECI_2ND_BIT_OF_ADDR_NEGO	1
#define     ASPEED_PECI_MESSAGE_NEGO		2
#define   ASPEED_PECI_INT_MASK			GENMASK(4, 0)
#define     ASPEED_PECI_INT_BUS_TIMEOUT		BIT(4)
#define     ASPEED_PECI_INT_BUS_CONTENTION	BIT(3)
#define     ASPEED_PECI_INT_WR_FCS_BAD		BIT(2)
#define     ASPEED_PECI_INT_WR_FCS_ABORT	BIT(1)
#define     ASPEED_PECI_INT_CMD_DONE		BIT(0)

/* Interrupt Status Register */
#define ASPEED_PECI_INT_STS			0x1c
#define   ASPEED_PECI_INT_TIMING_RESULT_MASK	GENMASK(29, 16)
	  /* bits[4..0]: Same bit fields in the 'Interrupt Register' */

/* Rx/Tx Data Buffer Registers */
#define ASPEED_PECI_WR_DATA0			0x20
#define ASPEED_PECI_WR_DATA1			0x24
#define ASPEED_PECI_WR_DATA2			0x28
#define ASPEED_PECI_WR_DATA3			0x2c
#define ASPEED_PECI_RD_DATA0			0x30
#define ASPEED_PECI_RD_DATA1			0x34
#define ASPEED_PECI_RD_DATA2			0x38
#define ASPEED_PECI_RD_DATA3			0x3c
#define ASPEED_PECI_WR_DATA4			0x40
#define ASPEED_PECI_WR_DATA5			0x44
#define ASPEED_PECI_WR_DATA6			0x48
#define ASPEED_PECI_WR_DATA7			0x4c
#define ASPEED_PECI_RD_DATA4			0x50
#define ASPEED_PECI_RD_DATA5			0x54
#define ASPEED_PECI_RD_DATA6			0x58
#define ASPEED_PECI_RD_DATA7			0x5c
#define   ASPEED_PECI_DATA_BUF_SIZE_MAX		32

/* Timing Negotiation */
#define ASPEED_PECI_RD_SAMPLING_POINT_DEFAULT	8
#define ASPEED_PECI_RD_SAMPLING_POINT_MAX	(BIT(4) - 1)
#define ASPEED_PECI_CLK_DIV_DEFAULT		0
#define ASPEED_PECI_CLK_DIV_MAX			(BIT(3) - 1)
#define ASPEED_PECI_MSG_TIMING_DEFAULT		1
#define ASPEED_PECI_MSG_TIMING_MAX		(BIT(8) - 1)
#define ASPEED_PECI_ADDR_TIMING_DEFAULT		1
#define ASPEED_PECI_ADDR_TIMING_MAX		(BIT(8) - 1)

/* Timeout */
#define ASPEED_PECI_IDLE_CHECK_TIMEOUT_US	(50 * USEC_PER_MSEC)
#define ASPEED_PECI_IDLE_CHECK_INTERVAL_US	(10 * USEC_PER_MSEC)
#define ASPEED_PECI_CMD_TIMEOUT_MS_DEFAULT	(1000)
#define ASPEED_PECI_CMD_TIMEOUT_MS_MAX		(1000)

struct aspeed_peci {
	struct peci_controller *controller;
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;
	int irq;
	spinlock_t lock; /* to sync completion status handling */
	struct completion xfer_complete;
	u32 status;
	u32 cmd_timeout_ms;
	u32 msg_timing;
	u32 addr_timing;
	u32 rd_sampling_point;
	u32 clk_div;
};

static void aspeed_peci_init_regs(struct aspeed_peci *priv)
{
	u32 val;

	val = FIELD_PREP(ASPEED_PECI_CTRL_CLK_DIV_MASK, ASPEED_PECI_CLK_DIV_DEFAULT);
	val |= ASPEED_PECI_CTRL_PECI_CLK_EN;
	writel(val, priv->base + ASPEED_PECI_CTRL);
	/*
	 * Timing negotiation period setting.
	 * The unit of the programmed value is 4 times of PECI clock period.
	 */
	val = FIELD_PREP(ASPEED_PECI_T_NEGO_MSG_MASK, priv->msg_timing);
	val |= FIELD_PREP(ASPEED_PECI_T_NEGO_ADDR_MASK, priv->addr_timing);
	writel(val, priv->base + ASPEED_PECI_TIMING_NEGOTIATION);

	/* Clear interrupts */
	val = readl(priv->base + ASPEED_PECI_INT_STS) | ASPEED_PECI_INT_MASK;
	writel(val, priv->base + ASPEED_PECI_INT_STS);

	/* Set timing negotiation mode and enable interrupts */
	val = FIELD_PREP(ASPEED_PECI_TIMING_NEGO_SEL_MASK, ASPEED_PECI_1ST_BIT_OF_ADDR_NEGO);
	val |= ASPEED_PECI_INT_MASK;
	writel(val, priv->base + ASPEED_PECI_INT_CTRL);

	val = FIELD_PREP(ASPEED_PECI_CTRL_SAMPLING_MASK, priv->rd_sampling_point);
	val |= FIELD_PREP(ASPEED_PECI_CTRL_CLK_DIV_MASK, priv->clk_div);
	val |= ASPEED_PECI_CTRL_PECI_EN;
	val |= ASPEED_PECI_CTRL_PECI_CLK_EN;
	writel(val, priv->base + ASPEED_PECI_CTRL);
}

static inline int aspeed_peci_check_idle(struct aspeed_peci *priv)
{
	u32 cmd_sts = readl(priv->base + ASPEED_PECI_CMD);

	if (FIELD_GET(ASPEED_PECI_CMD_STS_MASK, cmd_sts) == ASPEED_PECI_CMD_STS_ADDR_T_NEGO)
		aspeed_peci_init_regs(priv);

	return readl_poll_timeout(priv->base + ASPEED_PECI_CMD,
				  cmd_sts,
				  !(cmd_sts & ASPEED_PECI_CMD_IDLE_MASK),
				  ASPEED_PECI_IDLE_CHECK_INTERVAL_US,
				  ASPEED_PECI_IDLE_CHECK_TIMEOUT_US);
}

static int aspeed_peci_xfer(struct peci_controller *controller,
			    u8 addr, struct peci_request *req)
{
	struct aspeed_peci *priv = dev_get_drvdata(controller->dev.parent);
	unsigned long timeout = msecs_to_jiffies(priv->cmd_timeout_ms);
	u32 peci_head;
	int ret;

	if (req->tx.len > ASPEED_PECI_DATA_BUF_SIZE_MAX ||
	    req->rx.len > ASPEED_PECI_DATA_BUF_SIZE_MAX)
		return -EINVAL;

	/* Check command sts and bus idle state */
	ret = aspeed_peci_check_idle(priv);
	if (ret)
		return ret; /* -ETIMEDOUT */

	spin_lock_irq(&priv->lock);
	reinit_completion(&priv->xfer_complete);

	peci_head = FIELD_PREP(ASPEED_PECI_TARGET_ADDR_MASK, addr) |
		    FIELD_PREP(ASPEED_PECI_WR_LEN_MASK, req->tx.len) |
		    FIELD_PREP(ASPEED_PECI_RD_LEN_MASK, req->rx.len);

	writel(peci_head, priv->base + ASPEED_PECI_RW_LENGTH);

	memcpy_toio(priv->base + ASPEED_PECI_WR_DATA0, req->tx.buf, min_t(u8, req->tx.len, 16));
	if (req->tx.len > 16)
		memcpy_toio(priv->base + ASPEED_PECI_WR_DATA4, req->tx.buf + 16,
			    req->tx.len - 16);

#if IS_ENABLED(CONFIG_DYNAMIC_DEBUG)
	dev_dbg(priv->dev, "HEAD : %#08x\n", peci_head);
	print_hex_dump_bytes("TX : ", DUMP_PREFIX_NONE, req->tx.buf, req->tx.len);
#endif
	priv->status = 0;
	writel(ASPEED_PECI_CMD_FIRE, priv->base + ASPEED_PECI_CMD);
	spin_unlock_irq(&priv->lock);

	ret = wait_for_completion_interruptible_timeout(&priv->xfer_complete, timeout);
	if (ret < 0)
		return ret;

	if (ret == 0) {
		dev_dbg(priv->dev, "Timeout waiting for a response!\n");
		return -ETIMEDOUT;
	}

	spin_lock_irq(&priv->lock);

	writel(0, priv->base + ASPEED_PECI_CMD);

	if (priv->status != ASPEED_PECI_INT_CMD_DONE) {
		spin_unlock_irq(&priv->lock);
		dev_dbg(priv->dev, "No valid response, status: %#02x\n", priv->status);
		return -EIO;
	}

	spin_unlock_irq(&priv->lock);

	memcpy_fromio(req->rx.buf, priv->base + ASPEED_PECI_RD_DATA0, min_t(u8, req->rx.len, 16));
	if (req->rx.len > 16)
		memcpy_fromio(req->rx.buf + 16, priv->base + ASPEED_PECI_RD_DATA4,
			      req->rx.len - 16);

#if IS_ENABLED(CONFIG_DYNAMIC_DEBUG)
	print_hex_dump_bytes("RX : ", DUMP_PREFIX_NONE, req->rx.buf, req->rx.len);
#endif
	return 0;
}

static irqreturn_t aspeed_peci_irq_handler(int irq, void *arg)
{
	struct aspeed_peci *priv = arg;
	u32 status;

	spin_lock(&priv->lock);
	status = readl(priv->base + ASPEED_PECI_INT_STS);
	writel(status, priv->base + ASPEED_PECI_INT_STS);
	priv->status |= (status & ASPEED_PECI_INT_MASK);

	/*
	 * All commands should be ended up with a ASPEED_PECI_INT_CMD_DONE bit
	 * set even in an error case.
	 */
	if (status & ASPEED_PECI_INT_CMD_DONE)
		complete(&priv->xfer_complete);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static void aspeed_peci_property_sanitize(struct device *dev, const char *propname,
					  u32 min, u32 max, u32 default_val, u32 *propval)
{
	u32 val;
	int ret;

	ret = device_property_read_u32(dev, propname, &val);
	if (ret) {
		val = default_val;
	} else if (val > max || val < min) {
		dev_warn(dev, "Invalid %s: %u, falling back to: %u\n",
			 propname, val, default_val);

		val = default_val;
	}

	*propval = val;
}

static void aspeed_peci_property_setup(struct aspeed_peci *priv)
{
	aspeed_peci_property_sanitize(priv->dev, "aspeed,clock-divider",
				      0, ASPEED_PECI_CLK_DIV_MAX,
				      ASPEED_PECI_CLK_DIV_DEFAULT, &priv->clk_div);
	aspeed_peci_property_sanitize(priv->dev, "aspeed,msg-timing",
				      0, ASPEED_PECI_MSG_TIMING_MAX,
				      ASPEED_PECI_MSG_TIMING_DEFAULT, &priv->msg_timing);
	aspeed_peci_property_sanitize(priv->dev, "aspeed,addr-timing",
				      0, ASPEED_PECI_ADDR_TIMING_MAX,
				      ASPEED_PECI_ADDR_TIMING_DEFAULT, &priv->addr_timing);
	aspeed_peci_property_sanitize(priv->dev, "aspeed,rd-sampling-point",
				      0, ASPEED_PECI_RD_SAMPLING_POINT_MAX,
				      ASPEED_PECI_RD_SAMPLING_POINT_DEFAULT,
				      &priv->rd_sampling_point);
	aspeed_peci_property_sanitize(priv->dev, "cmd-timeout-ms",
				      1, ASPEED_PECI_CMD_TIMEOUT_MS_MAX,
				      ASPEED_PECI_CMD_TIMEOUT_MS_DEFAULT, &priv->cmd_timeout_ms);
}

static struct peci_controller_ops aspeed_ops = {
	.xfer = aspeed_peci_xfer,
};

static void aspeed_peci_reset_control_release(void *data)
{
	reset_control_assert(data);
}

static int devm_aspeed_peci_reset_control_deassert(struct device *dev, struct reset_control *rst)
{
	int ret;

	ret = reset_control_deassert(rst);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, aspeed_peci_reset_control_release, rst);
}

static void aspeed_peci_clk_release(void *data)
{
	clk_disable_unprepare(data);
}

static int devm_aspeed_peci_clk_enable(struct device *dev, struct clk *clk)
{
	int ret;

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, aspeed_peci_clk_release, clk);
}

static int aspeed_peci_probe(struct platform_device *pdev)
{
	struct peci_controller *controller;
	struct aspeed_peci *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	dev_set_drvdata(priv->dev, priv);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->irq = platform_get_irq(pdev, 0);
	if (!priv->irq)
		return priv->irq;

	ret = devm_request_irq(&pdev->dev, priv->irq, aspeed_peci_irq_handler,
			       0, "peci-aspeed", priv);
	if (ret)
		return ret;

	init_completion(&priv->xfer_complete);
	spin_lock_init(&priv->lock);

	priv->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(priv->rst))
		return dev_err_probe(priv->dev, PTR_ERR(priv->rst),
				     "failed to get reset control\n");

	ret = devm_aspeed_peci_reset_control_deassert(priv->dev, priv->rst);
	if (ret)
		return dev_err_probe(priv->dev, ret, "cannot deassert reset control\n");

	priv->clk = devm_clk_get(priv->dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(priv->dev, PTR_ERR(priv->clk), "failed to get clk\n");

	ret = devm_aspeed_peci_clk_enable(priv->dev, priv->clk);
	if (ret)
		return dev_err_probe(priv->dev, ret, "failed to enable clock\n");

	aspeed_peci_property_setup(priv);

	aspeed_peci_init_regs(priv);

	controller = devm_peci_controller_add(priv->dev, &aspeed_ops);
	if (IS_ERR(controller))
		return dev_err_probe(priv->dev, PTR_ERR(controller),
				     "failed to add aspeed peci controller\n");

	priv->controller = controller;

	return 0;
}

static const struct of_device_id aspeed_peci_of_table[] = {
	{ .compatible = "aspeed,ast2400-peci", },
	{ .compatible = "aspeed,ast2500-peci", },
	{ .compatible = "aspeed,ast2600-peci", },
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_peci_of_table);

static struct platform_driver aspeed_peci_driver = {
	.probe  = aspeed_peci_probe,
	.driver = {
		.name           = "peci-aspeed",
		.of_match_table = aspeed_peci_of_table,
	},
};
module_platform_driver(aspeed_peci_driver);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_DESCRIPTION("ASPEED PECI driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PECI);
