// SPDX-License-Identifier: GPL-2.0
/*
 * LiteX LiteSDCard driver
 *
 * Copyright (C) 2019-2020 Antmicro <www.antmicro.com>
 *
 */

#include <linux/module.h>
#include <linux/litex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#define LITEX_PHY_CARDDETECT  0x00
#define LITEX_PHY_CLOCKERDIV  0x04
#define LITEX_PHY_INITIALIZE  0x08
#define LITEX_PHY_WRITESTATUS 0x0C
#define LITEX_CORE_CMDARG     0x00
#define LITEX_CORE_CMDCMD     0x04
#define LITEX_CORE_CMDSND     0x08
#define LITEX_CORE_CMDRSP     0x0C
#define LITEX_CORE_CMDEVT     0x1C
#define LITEX_CORE_DATAEVT    0x20
#define LITEX_CORE_BLKLEN     0x24
#define LITEX_CORE_BLKCNT     0x28
#define LITEX_BLK2MEM_BASE    0x00
#define LITEX_BLK2MEM_LEN     0x08
#define LITEX_BLK2MEM_ENA     0x0C
#define LITEX_BLK2MEM_DONE    0x10
#define LITEX_BLK2MEM_LOOP    0x14
#define LITEX_MEM2BLK_BASE    0x00
#define LITEX_MEM2BLK_LEN     0x08
#define LITEX_MEM2BLK_ENA     0x0C
#define LITEX_MEM2BLK_DONE    0x10
#define LITEX_MEM2BLK_LOOP    0x14
#define LITEX_MEM2BLK         0x18
#define LITEX_IRQ_STATUS      0x00
#define LITEX_IRQ_PENDING     0x04
#define LITEX_IRQ_ENABLE      0x08

#define SDCARD_CTRL_DATA_TRANSFER_NONE  0
#define SDCARD_CTRL_DATA_TRANSFER_READ  1
#define SDCARD_CTRL_DATA_TRANSFER_WRITE 2

#define SDCARD_CTRL_RESPONSE_NONE       0
#define SDCARD_CTRL_RESPONSE_SHORT      1
#define SDCARD_CTRL_RESPONSE_LONG       2
#define SDCARD_CTRL_RESPONSE_SHORT_BUSY 3

#define SD_OK         0
#define SD_WRITEERROR 1
#define SD_TIMEOUT    2
#define SD_CRCERROR   3
#define SD_ERR_OTHER  4

#define SD_SLEEP_US       5
#define SD_TIMEOUT_US 20000

#define SDIRQ_CARD_DETECT    1
#define SDIRQ_SD_TO_MEM_DONE 2
#define SDIRQ_MEM_TO_SD_DONE 4
#define SDIRQ_CMD_DONE       8

struct litex_mmc_host {
	struct mmc_host *mmc;
	struct platform_device *dev;

	void __iomem *sdphy;
	void __iomem *sdcore;
	void __iomem *sdreader;
	void __iomem *sdwriter;
	void __iomem *sdirq;

	u32 resp[4];
	u16 rca;

	void *buffer;
	size_t buf_size;
	dma_addr_t dma;

	unsigned int freq;
	unsigned int clock;
	bool is_bus_width_set;
	bool app_cmd;

	int irq;
	struct completion cmd_done;
};

static int
sdcard_wait_done(void __iomem *reg)
{
	u8 evt;
	int ret;

	ret = read_poll_timeout(litex_read8, evt, (evt & 0x1),
				SD_SLEEP_US, SD_TIMEOUT_US, false, reg);
	if (ret || (evt & 0x4))
		return SD_TIMEOUT;
	if (evt == 0x1)
		return SD_OK;
	if (evt & 0x2)
		return SD_WRITEERROR;
	if (evt & 0x8)
		return SD_CRCERROR;
	pr_err("%s: unknown error evt=%x\n", __func__, evt);
	return SD_ERR_OTHER;
}

static int
send_cmd(struct litex_mmc_host *host,
	 u8 cmd, u32 arg, u8 response_len, u8 transfer)
{
	void __iomem *reg;
	u8 i;
	int status;

	litex_write32(host->sdcore + LITEX_CORE_CMDARG, arg);
	litex_write32(host->sdcore + LITEX_CORE_CMDCMD,
		      cmd << 8 | transfer << 5 | response_len);
	litex_write8(host->sdcore + LITEX_CORE_CMDSND, 1);

	/* Wait for an interrupt if we have an interrupt and either there is
	 * data to be transferred, or if the card can report busy via DAT0.
	 */
	if (host->irq > 0 &&
	    (transfer != SDCARD_CTRL_DATA_TRANSFER_NONE ||
	     response_len == SDCARD_CTRL_RESPONSE_SHORT_BUSY)) {
		reinit_completion(&host->cmd_done);
		litex_write32(host->sdirq + LITEX_IRQ_ENABLE,
			      SDIRQ_CMD_DONE | SDIRQ_CARD_DETECT);
		wait_for_completion(&host->cmd_done);
	}

	status = sdcard_wait_done(host->sdcore + LITEX_CORE_CMDEVT);

	if (status != SD_OK) {
		pr_err("Command (cmd %d) failed, status %d\n", cmd, status);
		return status;
	}

	if (response_len != SDCARD_CTRL_RESPONSE_NONE) {
		reg = host->sdcore + LITEX_CORE_CMDRSP;
		for (i = 0; i < 4; i++) {
			host->resp[i] = litex_read32(reg);
			reg += sizeof(u32);
		}
	}

	if (!host->app_cmd && cmd == SD_SEND_RELATIVE_ADDR)
		host->rca = (host->resp[3] >> 16) & 0xffff;

	host->app_cmd = (cmd == MMC_APP_CMD);

	if (transfer == SDCARD_CTRL_DATA_TRANSFER_NONE)
		return status; /* SD_OK from prior sdcard_wait_done(cmd_evt) */

	status = sdcard_wait_done(host->sdcore + LITEX_CORE_DATAEVT);
	if (status != SD_OK) {
		pr_err("Data xfer (cmd %d) failed, status %d\n", cmd, status);
		return status;
	}

	/* wait for completion of (read or write) DMA transfer */
	reg = (transfer == SDCARD_CTRL_DATA_TRANSFER_READ) ?
		host->sdreader + LITEX_BLK2MEM_DONE :
		host->sdwriter + LITEX_MEM2BLK_DONE;

	status = read_poll_timeout(litex_read8, i, (i & 0x1),
				   SD_SLEEP_US, SD_TIMEOUT_US, false, reg);
	if (status)
		pr_err("DMA timeout (cmd %d)\n", cmd);

	return status;
}

static inline int
send_app_cmd(struct litex_mmc_host *host)
{
	return send_cmd(host, MMC_APP_CMD, host->rca << 16,
			SDCARD_CTRL_RESPONSE_SHORT,
			SDCARD_CTRL_DATA_TRANSFER_NONE);
}

static inline int
send_app_set_bus_width_cmd(struct litex_mmc_host *host, u32 width)
{
	return send_cmd(host, SD_APP_SET_BUS_WIDTH, width,
			SDCARD_CTRL_RESPONSE_SHORT,
			SDCARD_CTRL_DATA_TRANSFER_NONE);
}

static int
litex_set_bus_width(struct litex_mmc_host *host)
{
	bool app_cmd_sent = host->app_cmd; /* was preceding command app_cmd? */
	int status;

	/* ensure 'app_cmd' precedes 'app_set_bus_width_cmd' */
	if (!app_cmd_sent)
		send_app_cmd(host);

	/* litesdcard only supports 4-bit bus width */
	status = send_app_set_bus_width_cmd(host, MMC_BUS_WIDTH_4);

	/* re-send 'app_cmd' if necessary */
	if (app_cmd_sent)
		send_app_cmd(host);

	return status;
}

static int
litex_get_cd(struct mmc_host *mmc)
{
	struct litex_mmc_host *host = mmc_priv(mmc);
	int ret;

	if (!mmc_card_is_removable(mmc))
		return 1;

	ret = mmc_gpio_get_cd(mmc);
	if (ret >= 0)
		/* GPIO based card-detect explicitly specified in DTS */
		ret = !!ret;
	else
		/* use gateware card-detect bit by default */
		ret = !litex_read8(host->sdphy + LITEX_PHY_CARDDETECT);

	/* ensure bus width will be set (again) upon card (re)insertion */
	if (ret == 0)
		host->is_bus_width_set = false;

	return ret;
}

static irqreturn_t
litex_mmc_interrupt(int irq, void *arg)
{
	struct mmc_host *mmc = arg;
	struct litex_mmc_host *host = mmc_priv(mmc);
	u32 pending = litex_read32(host->sdirq + LITEX_IRQ_PENDING);

	/* Check for card change interrupt */
	if (pending & SDIRQ_CARD_DETECT) {
		litex_write32(host->sdirq + LITEX_IRQ_PENDING,
			      SDIRQ_CARD_DETECT);
		mmc_detect_change(mmc, msecs_to_jiffies(10));
	}

	/* Check for command completed */
	if (pending & SDIRQ_CMD_DONE) {
		/* Disable it so it doesn't keep interrupting */
		litex_write32(host->sdirq + LITEX_IRQ_ENABLE,
			      SDIRQ_CARD_DETECT);
		complete(&host->cmd_done);
	}

	return IRQ_HANDLED;
}

static u32
litex_response_len(struct mmc_command *cmd)
{
	if (cmd->flags & MMC_RSP_136) {
		return SDCARD_CTRL_RESPONSE_LONG;
	} else if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_BUSY)
			return SDCARD_CTRL_RESPONSE_SHORT_BUSY;
		else
			return SDCARD_CTRL_RESPONSE_SHORT;
	}
	return SDCARD_CTRL_RESPONSE_NONE;
}

static int
litex_map_status(int status)
{
	switch (status) {
	case SD_OK:
		return 0;
	case SD_WRITEERROR:
		return -EIO;
	case SD_TIMEOUT:
		return -ETIMEDOUT;
	case SD_CRCERROR:
		return -EILSEQ;
	default:
		break;
	}
	return -EINVAL;
}

static void
litex_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct litex_mmc_host *host = mmc_priv(mmc);
	struct platform_device *pdev = to_platform_device(mmc->parent);
	struct device *dev = &pdev->dev;
	struct mmc_data *data = mrq->data;
	struct mmc_command *sbc = mrq->sbc;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_command *stop = mrq->stop;
	unsigned int retries = cmd->retries;
	int status;
	int sg_count;
	enum dma_data_direction dir = DMA_TO_DEVICE;
	bool direct = false;
	dma_addr_t dma;
	unsigned int len = 0;

	u32 response_len = litex_response_len(cmd);
	u32 transfer = SDCARD_CTRL_DATA_TRANSFER_NONE;

	/* First check that the card is still there */
	if (!litex_get_cd(mmc)) {
		cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}

	/* Send set-block-count command if needed */
	if (sbc) {
		status = send_cmd(host, sbc->opcode, sbc->arg,
				  litex_response_len(sbc),
				  SDCARD_CTRL_DATA_TRANSFER_NONE);
		sbc->error = litex_map_status(status);
		if (status != SD_OK) {
			host->is_bus_width_set = false;
			mmc_request_done(mmc, mrq);
			return;
		}
	}

	if (data) {
		/* LiteSDCard only supports 4-bit bus width; therefore, we MUST
		 * inject a SET_BUS_WIDTH (acmd6) before the very first data
		 * transfer, earlier than when the mmc subsystem would normally
		 * get around to it!
		 */
		if (!host->is_bus_width_set) {
			ulong n = jiffies + 2 * HZ; // 500ms timeout

			while (litex_set_bus_width(host) != SD_OK) {
				if (time_after(jiffies, n)) {
					dev_warn(dev, "Can't set bus width!\n");
					cmd->error = -ETIMEDOUT;
					mmc_request_done(mmc, mrq);
					return;
				}
			}
			host->is_bus_width_set = true;
		}

		/* Try to DMA directly to/from the data buffer.
		 * We can do that if the buffer can be mapped for DMA
		 * in one contiguous chunk.
		 */
		dma = host->dma;
		len = data->blksz * data->blocks;
		if (data->flags & MMC_DATA_READ)
			dir = DMA_FROM_DEVICE;
		sg_count = dma_map_sg(&host->dev->dev,
				      data->sg, data->sg_len, dir);
		if (sg_count == 1) {
			dma = sg_dma_address(data->sg);
			len = sg_dma_len(data->sg);
			direct = true;
		} else if (len > host->buf_size)
			len = host->buf_size;

		if (data->flags & MMC_DATA_READ) {
			litex_write8(host->sdreader + LITEX_BLK2MEM_ENA, 0);
			litex_write64(host->sdreader + LITEX_BLK2MEM_BASE, dma);
			litex_write32(host->sdreader + LITEX_BLK2MEM_LEN, len);
			litex_write8(host->sdreader + LITEX_BLK2MEM_ENA, 1);

			transfer = SDCARD_CTRL_DATA_TRANSFER_READ;
		} else if (data->flags & MMC_DATA_WRITE) {
			if (!direct)
				sg_copy_to_buffer(data->sg, data->sg_len,
						  host->buffer, len);

			litex_write8(host->sdwriter + LITEX_MEM2BLK_ENA, 0);
			litex_write64(host->sdwriter + LITEX_MEM2BLK_BASE, dma);
			litex_write32(host->sdwriter + LITEX_MEM2BLK_LEN, len);
			litex_write8(host->sdwriter + LITEX_MEM2BLK_ENA, 1);

			transfer = SDCARD_CTRL_DATA_TRANSFER_WRITE;
		} else {
			dev_warn(dev, "Data present w/o read or write flag.\n");
			/* Continue: set cmd status, mark req done */
		}

		litex_write16(host->sdcore + LITEX_CORE_BLKLEN, data->blksz);
		litex_write32(host->sdcore + LITEX_CORE_BLKCNT, data->blocks);
	}

	do {
		status = send_cmd(host, cmd->opcode, cmd->arg,
				  response_len, transfer);
	} while (status != SD_OK && retries-- > 0);

	cmd->error = litex_map_status(status);
	if (status != SD_OK)
		/* card may be gone; don't assume bus width is still set */
		host->is_bus_width_set = false;

	if (response_len == SDCARD_CTRL_RESPONSE_SHORT) {
		/* pull short response fields from appropriate host registers */
		cmd->resp[0] = host->resp[3];
		cmd->resp[1] = host->resp[2] & 0xFF;
	} else if (response_len == SDCARD_CTRL_RESPONSE_LONG) {
		cmd->resp[0] = host->resp[0];
		cmd->resp[1] = host->resp[1];
		cmd->resp[2] = host->resp[2];
		cmd->resp[3] = host->resp[3];
	}

	/* Send stop-transmission command if required */
	if (stop && (cmd->error || !sbc)) {
		int stop_stat;

		stop_stat = send_cmd(host, stop->opcode, stop->arg,
				     litex_response_len(stop),
				     SDCARD_CTRL_DATA_TRANSFER_NONE);
		stop->error = litex_map_status(stop_stat);
		if (stop_stat != SD_OK)
			host->is_bus_width_set = false;
	}

	if (data)
		dma_unmap_sg(&host->dev->dev, data->sg, data->sg_len, dir);

	if (status == SD_OK && transfer != SDCARD_CTRL_DATA_TRANSFER_NONE) {
		data->bytes_xfered = min(len, mmc->max_req_size);
		if (transfer == SDCARD_CTRL_DATA_TRANSFER_READ && !direct) {
			sg_copy_from_buffer(data->sg, sg_nents(data->sg),
					    host->buffer, data->bytes_xfered);
		}
	}

	mmc_request_done(mmc, mrq);
}

static void
litex_set_clk(struct litex_mmc_host *host, unsigned int clk_freq)
{
	u32 div = clk_freq ? host->freq / clk_freq : 256;

	div = roundup_pow_of_two(div);
	div = min_t(u32, max_t(u32, div, 2), 256);
	dev_info(&host->dev->dev, "sdclk_freq=%d: set to %d via div=%d\n",
		 clk_freq, host->freq / div, div);
	litex_write16(host->sdphy + LITEX_PHY_CLOCKERDIV, div);
}

static void
litex_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct litex_mmc_host *host = mmc_priv(mmc);

	/* NOTE: Ignore any ios->bus_width updates; they occur right after
	 * the mmc core sends its own acmd6 bus-width change notification,
	 * which is redundant since we snoop on the command flow and inject
	 * an early acmd6 before the first data transfer command is sent!
	 */

	/* update sdcard clock */
	if (ios->clock != host->clock) {
		litex_set_clk(host, ios->clock);
		host->clock = ios->clock;
	}
}

static const struct mmc_host_ops litex_mmc_ops = {
	.get_cd = litex_get_cd,
	.request = litex_request,
	.set_ios = litex_set_ios,
};

static int
litex_mmc_probe(struct platform_device *pdev)
{
	struct litex_mmc_host *host;
	struct mmc_host *mmc;
	struct device_node *cpu;
	int ret;

	mmc = mmc_alloc_host(sizeof(struct litex_mmc_host), &pdev->dev);
	/* NOTE: defaults to max_[req,seg]_size=PAGE_SIZE, max_blk_size=512,
	 * and max_blk_count accordingly set to 8;
	 * If for some reason we need to modify max_blk_count, we must also
	 * re-calculate `max_[req,seg]_size = max_blk_size * max_blk_count;`
	 */
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dev = pdev;

	host->clock = 0;
	cpu = of_get_next_cpu_node(NULL);
	ret = of_property_read_u32(cpu, "clock-frequency", &host->freq);
	of_node_put(cpu);
	if (ret) {
		dev_err(&pdev->dev, "No \"clock-frequency\" property in DT\n");
		goto err_free_host;
	}

	init_completion(&host->cmd_done);
	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0)
		dev_err(&pdev->dev, "Failed to get IRQ, using polling\n");

	/* LiteSDCard only supports 4-bit bus width; therefore, we MUST inject
	 * a SET_BUS_WIDTH (acmd6) before the very first data transfer, earlier
	 * than when the mmc subsystem would normally get around to it!
	 */
	host->is_bus_width_set = false;
	host->app_cmd = false;

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto err_free_host;

	host->buf_size = mmc->max_req_size * 2;
	host->buffer = dma_alloc_coherent(&pdev->dev, host->buf_size,
					  &host->dma, GFP_DMA);
	if (host->buffer == NULL) {
		ret = -ENOMEM;
		goto err_free_host;
	}

	host->sdphy = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(host->sdphy)) {
		ret = PTR_ERR(host->sdphy);
		goto err_free_dma;
	}

	host->sdcore = devm_platform_ioremap_resource_byname(pdev, "core");
	if (IS_ERR(host->sdcore)) {
		ret = PTR_ERR(host->sdcore);
		goto err_free_dma;
	}

	host->sdreader = devm_platform_ioremap_resource_byname(pdev, "reader");
	if (IS_ERR(host->sdreader)) {
		ret = PTR_ERR(host->sdreader);
		goto err_free_dma;
	}

	host->sdwriter = devm_platform_ioremap_resource_byname(pdev, "writer");
	if (IS_ERR(host->sdwriter)) {
		ret = PTR_ERR(host->sdwriter);
		goto err_free_dma;
	}

	if (host->irq > 0) {
		host->sdirq = devm_platform_ioremap_resource_byname(pdev, "irq");
		if (IS_ERR(host->sdirq)) {
			ret = PTR_ERR(host->sdirq);
			goto err_free_dma;
		}
	}

	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->ops = &litex_mmc_ops;

	mmc->f_min = 12.5e6;
	mmc->f_max = 50e6;

	ret = mmc_of_parse(mmc);
	if (ret)
		goto err_free_dma;

	/* force 4-bit bus_width (only width supported by hardware) */
	mmc->caps &= ~MMC_CAP_8_BIT_DATA;
	mmc->caps |= MMC_CAP_4_BIT_DATA;

	/* set default capabilities */
	mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY |
		     MMC_CAP_DRIVER_TYPE_D |
		     MMC_CAP_CMD23;
	mmc->caps2 |= MMC_CAP2_NO_WRITE_PROTECT |
		      MMC_CAP2_FULL_PWR_CYCLE |
		      MMC_CAP2_NO_SDIO;

	platform_set_drvdata(pdev, host);

	ret = mmc_add_host(mmc);
	if (ret < 0)
		goto err_free_dma;

	/* ensure DMA bus masters are disabled */
	litex_write8(host->sdreader + LITEX_BLK2MEM_ENA, 0);
	litex_write8(host->sdwriter + LITEX_MEM2BLK_ENA, 0);

	/* set up interrupt handler */
	if (host->irq > 0) {
		ret = request_irq(host->irq, litex_mmc_interrupt, 0,
				  "litex-mmc", mmc);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"irq setup error %d, using polling\n", ret);
			host->irq = 0;
		}
	}

	/* enable card-change interrupts, or else ask for polling */
	if (host->irq > 0) {
		litex_write32(host->sdirq + LITEX_IRQ_PENDING,
			      SDIRQ_CARD_DETECT);	/* clears it */
		litex_write32(host->sdirq + LITEX_IRQ_ENABLE,
			      SDIRQ_CARD_DETECT);
	} else {
		mmc->caps |= MMC_CAP_NEEDS_POLL;
	}

	return 0;

err_free_dma:
	dma_free_coherent(&pdev->dev, host->buf_size, host->buffer, host->dma);
err_free_host:
	mmc_free_host(mmc);
	return ret;
}

static int
litex_mmc_remove(struct platform_device *pdev)
{
	struct litex_mmc_host *host = dev_get_drvdata(&pdev->dev);

	if (host->irq > 0)
		free_irq(host->irq, host->mmc);
	mmc_remove_host(host->mmc);
	dma_free_coherent(&pdev->dev, host->buf_size, host->buffer, host->dma);
	mmc_free_host(host->mmc);

	return 0;
}

static const struct of_device_id litex_match[] = {
	{ .compatible = "litex,mmc" },
	{ }
};
MODULE_DEVICE_TABLE(of, litex_match);

static struct platform_driver litex_mmc_driver = {
	.probe = litex_mmc_probe,
	.remove = litex_mmc_remove,
	.driver = {
		.name = "litex-mmc",
		.of_match_table = of_match_ptr(litex_match),
	},
};
module_platform_driver(litex_mmc_driver);

MODULE_DESCRIPTION("LiteX SDCard driver");
MODULE_AUTHOR("Antmicro <www.antmicro.com>");
MODULE_LICENSE("GPL v2");
