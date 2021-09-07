// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/semaphore.h>

#include "mtk_mt8195_hdmi_ddc.h"
#include "mtk_mt8195_hdmi_regs.h"
#include "mtk_mt8195_hdmi.h"

#define EDID_ID 0x50
#define DDC2_CLOK 572 /* BIM=208M/(v*4) = 90Khz */
#define DDC2_CLOK_EDID 832 /* BIM=208M/(v*4) = 62.5Khz */

enum SIF_BIT_T_HDMI {
	SIF_8_BIT_HDMI, /* /< [8 bits data address.] */
	SIF_16_BIT_HDMI, /* /< [16 bits data address.] */
};

enum SIF_BIT_T {
	SIF_8_BIT, /* /< [8 bits data address.] */
	SIF_16_BIT, /* /< [16 bits data address.] */
};

static inline bool mtk_ddc_readbit(struct mtk_hdmi_ddc *ddc, unsigned short reg,
				   unsigned int offset)
{
	return (readl(ddc->regs + reg) & offset) ? true : false;
}

static inline unsigned int mtk_ddc_read(struct mtk_hdmi_ddc *ddc,
					unsigned short reg)
{
	return readl(ddc->regs + reg);
}

static inline void mtk_ddc_write(struct mtk_hdmi_ddc *ddc, unsigned short reg,
				 unsigned int val)
{
	writel(val, ddc->regs + reg);
}

static inline void mtk_ddc_mask(struct mtk_hdmi_ddc *ddc, unsigned int reg,
				unsigned int val, unsigned int mask)
{
	unsigned int tmp;

	tmp = readl(ddc->regs + reg) & ~mask;
	tmp |= (val & mask);
	writel(tmp, ddc->regs + reg);
}

static void hdmi_ddc_request(struct mtk_hdmi_ddc *ddc)
{
	mtk_ddc_mask(ddc, HDCP2X_POL_CTRL, HDCP2X_DIS_POLL_EN,
		     HDCP2X_DIS_POLL_EN);
}

static void DDC_WR_ONE(struct mtk_hdmi_ddc *ddc, unsigned int addr_id,
		       unsigned int offset_id, unsigned char wr_data)
{
	unsigned int i;

	if (mtk_ddc_read(ddc, HDCP2X_DDCM_STATUS) & DDC_I2C_BUS_LOW) {
		mtk_ddc_mask(ddc, DDC_CTRL, (CLOCK_SCL << DDC_CMD_SHIFT),
			     DDC_CMD);
		udelay(250);
	}
	mtk_ddc_mask(ddc, HPD_DDC_CTRL, DDC2_CLOK << DDC_DELAY_CNT_SHIFT,
		     DDC_DELAY_CNT);
	mtk_ddc_write(ddc, SI2C_CTRL, SI2C_ADDR_READ << SI2C_ADDR_SHIFT);
	mtk_ddc_mask(ddc, SI2C_CTRL, wr_data << SI2C_WDATA_SHIFT, SI2C_WDATA);
	mtk_ddc_mask(ddc, SI2C_CTRL, SI2C_WR, SI2C_WR);

	mtk_ddc_write(ddc, DDC_CTRL,
		      (SEQ_WRITE_REQ_ACK << DDC_CMD_SHIFT) +
			      (1 << DDC_DIN_CNT_SHIFT) +
			      (offset_id << DDC_OFFSET_SHIFT) + (addr_id << 1));

	for (i = 0; i < 5; i++)
		udelay(200);

	if ((mtk_ddc_read(ddc, HDCP2X_DDCM_STATUS) &
	     (DDC_I2C_NO_ACK | DDC_I2C_BUS_LOW))) {
		if (mtk_ddc_read(ddc, HDCP2X_DDCM_STATUS) & DDC_I2C_BUS_LOW) {
			mtk_ddc_mask(ddc, DDC_CTRL,
				     (CLOCK_SCL << DDC_CMD_SHIFT), DDC_CMD);
			udelay(250);
		}
	}
}

static unsigned char
_DDCMRead_hdmi(struct mtk_hdmi_ddc *ddc, unsigned char ucCurAddrMode,
	       unsigned int u4ClkDiv, unsigned char ucDev, unsigned int u4Addr,
	       enum SIF_BIT_T_HDMI ucAddrType, unsigned char *pucValue,
	       unsigned int u4Count)
{
	unsigned int i, temp_length, loop_counter, temp_ksvlist, device_n;
	unsigned int ucReadCount, ucIdx;
	unsigned long DdcStartTime, DdcEndTime, DdcTimeOut;

	if ((pucValue == NULL) || (u4Count == 0) || (u4ClkDiv == 0))
		return 0;

	ucIdx = 0;
	if (mtk_ddc_read(ddc, HDCP2X_DDCM_STATUS) & DDC_I2C_BUS_LOW) {
		mtk_ddc_mask(ddc, DDC_CTRL, (CLOCK_SCL << DDC_CMD_SHIFT),
			     DDC_CMD);
		udelay(250);
	}

	mtk_ddc_mask(ddc, DDC_CTRL, (CLEAR_FIFO << DDC_CMD_SHIFT), DDC_CMD);

	if (u4Addr == 0x43) {
		mtk_ddc_write(ddc, DDC_CTRL,
			      (SEQ_READ_NO_ACK << DDC_CMD_SHIFT) +
				      (u4Count << DDC_DIN_CNT_SHIFT) +
				      (u4Addr << DDC_OFFSET_SHIFT) +
				      (ucDev << 1));
		udelay(250);
		udelay(250);
		udelay(200);

		if (u4Count > 10)
			temp_ksvlist = 10;
		else
			temp_ksvlist = u4Count;

		for (ucIdx = 0; ucIdx < temp_ksvlist; ucIdx++) {
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_RD);
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_CONFIRM_READ);

			pucValue[ucIdx] = (mtk_ddc_read(ddc, HPD_DDC_STATUS) &
					   DDC_DATA_OUT) >>
					  DDC_DATA_OUT_SHIFT;
			udelay(100);
		}

		if (u4Count == temp_ksvlist)
			return (ucIdx + 1);

		udelay(250);
		udelay(250);

		if (u4Count / 5 == 3)
			device_n = 5;
		else
			device_n = 10;

		for (ucIdx = 10; ucIdx < (10 + device_n); ucIdx++) {
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_RD);
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_CONFIRM_READ);

			pucValue[ucIdx] = (mtk_ddc_read(ddc, HPD_DDC_STATUS) &
					   DDC_DATA_OUT) >>
					  DDC_DATA_OUT_SHIFT;
			udelay(100);
		}

		if (u4Count == (10 + device_n))
			return (ucIdx + 1);

		udelay(250);
		udelay(250);

		if (u4Count / 5 == 5)
			device_n = 5;
		else
			device_n = 10;

		for (ucIdx = 20; ucIdx < (20 + device_n); ucIdx++) {
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_RD);
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_CONFIRM_READ);

			pucValue[ucIdx] = (mtk_ddc_read(ddc, HPD_DDC_STATUS) &
					   DDC_DATA_OUT) >>
					  DDC_DATA_OUT_SHIFT;
			udelay(100);
		}

		if (u4Count == (20 + device_n))
			return (ucIdx + 1);

		udelay(250);
		udelay(250);

		if (u4Count / 5 == 7)
			device_n = 5;
		else
			device_n = 10;

		for (ucIdx = 30; ucIdx < (30 + device_n); ucIdx++) {
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_RD);
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_CONFIRM_READ);

			pucValue[ucIdx] = (mtk_ddc_read(ddc, HPD_DDC_STATUS) &
					   DDC_DATA_OUT) >>
					  DDC_DATA_OUT_SHIFT;
			udelay(100);
		}

		if (u4Count == (30 + device_n))
			return (ucIdx + 1);

		udelay(250);
		udelay(250);

		for (ucIdx = 40; ucIdx < (40 + 5); ucIdx++) {
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_RD);
			mtk_ddc_write(ddc, SI2C_CTRL,
				      (SI2C_ADDR_READ << SI2C_ADDR_SHIFT) +
					      SI2C_CONFIRM_READ);

			pucValue[ucIdx] = (mtk_ddc_read(ddc, HPD_DDC_STATUS) &
					   DDC_DATA_OUT) >>
					  DDC_DATA_OUT_SHIFT;
			udelay(100);
		}

		if (u4Count == 45)
			return (ucIdx + 1);
	} else {
		if (u4Count >= 16) {
			temp_length = 16;
			loop_counter =
				u4Count / 16 + ((u4Count % 16 == 0) ? 0 : 1);
		} else {
			temp_length = u4Count;
			loop_counter = 1;
		}
		if (ucDev >= EDID_ID) {
			if (u4ClkDiv < DDC2_CLOK_EDID)
				u4ClkDiv = DDC2_CLOK_EDID;
		}
		mtk_ddc_mask(ddc, HPD_DDC_CTRL, u4ClkDiv << DDC_DELAY_CNT_SHIFT,
			     DDC_DELAY_CNT);
		for (i = 0; i < loop_counter; i++) {
			if ((i == (loop_counter - 1)) && (i != 0) &&
			    (u4Count % 16))
				temp_length = u4Count % 16;

			if (ucDev > EDID_ID) {
				mtk_ddc_mask(ddc, SCDC_CTRL,
					     (ucDev - EDID_ID)
						     << DDC_SEGMENT_SHIFT,
					     DDC_SEGMENT);
				mtk_ddc_write(
					ddc, DDC_CTRL,
					(ENH_READ_NO_ACK << DDC_CMD_SHIFT) +
						(temp_length
						 << DDC_DIN_CNT_SHIFT) +
						((u4Addr + i * temp_length)
						 << DDC_OFFSET_SHIFT) +
						(EDID_ID << 1));
			} else {
				mtk_ddc_write(
					ddc, DDC_CTRL,
					(SEQ_READ_NO_ACK << DDC_CMD_SHIFT) +
						(temp_length
						 << DDC_DIN_CNT_SHIFT) +
						((u4Addr + ((u4Addr == 0x43) ?
									  0 :
									  (i * 16)))
						 << DDC_OFFSET_SHIFT) +
						(ucDev << 1));
			}
			mdelay(2);
			DdcStartTime = jiffies;
			DdcTimeOut = temp_length + 5;
			DdcEndTime = DdcStartTime + (DdcTimeOut)*HZ / 1000;
			while (1) {
				if ((mtk_ddc_read(ddc, HPD_DDC_STATUS) &
				     DDC_I2C_IN_PROG) == 0)
					break;

				if (time_after(jiffies, DdcEndTime)) {
					pr_info("[HDMI][DDC] error: time out\n");
					return 0;
				}
				mdelay(1);
			}
			if ((mtk_ddc_read(ddc, HDCP2X_DDCM_STATUS) &
			     (DDC_I2C_NO_ACK | DDC_I2C_BUS_LOW))) {
				if (mtk_ddc_read(ddc, HDCP2X_DDCM_STATUS) &
				    DDC_I2C_BUS_LOW) {
					mtk_ddc_mask(ddc, DDC_CTRL,
						     (CLOCK_SCL
						      << DDC_CMD_SHIFT),
						     DDC_CMD);
					udelay(250);
				}
				return 0;
			}
			for (ucIdx = 0; ucIdx < temp_length; ucIdx++) {
				mtk_ddc_write(ddc, SI2C_CTRL,
					      (SI2C_ADDR_READ
					       << SI2C_ADDR_SHIFT) +
						      SI2C_RD);
				mtk_ddc_write(ddc, SI2C_CTRL,
					      (SI2C_ADDR_READ
					       << SI2C_ADDR_SHIFT) +
						      SI2C_CONFIRM_READ);

				pucValue[i * 16 + ucIdx] =
					(mtk_ddc_read(ddc, HPD_DDC_STATUS) &
					 DDC_DATA_OUT) >>
					DDC_DATA_OUT_SHIFT;
				/*
				 * when reading edid, if hdmi module been reset,
				 * ddc will fail and it's
				 *speed will be set to 400.
				 */
				if (((mtk_ddc_read(ddc, HPD_DDC_CTRL) >> 16) &
				     0xFFFF) < DDC2_CLOK)
					return 0;

				ucReadCount = i * 16 + ucIdx + 1;
			}
		}
		return ucReadCount;
	}
	return 0;
}

static unsigned char vDDCRead(struct mtk_hdmi_ddc *ddc, unsigned int u4ClkDiv,
			      unsigned char ucDev, unsigned int u4Addr,
			      enum SIF_BIT_T_HDMI ucAddrType,
			      unsigned char *pucValue, unsigned int u4Count)
{
	unsigned int u4ReadCount = 0;
	unsigned char ucReturnVaule = 0;

	if ((pucValue == NULL) || (u4Count == 0) || (u4ClkDiv == 0) ||
	    (ucAddrType > SIF_16_BIT_HDMI) ||
	    ((ucAddrType == SIF_8_BIT_HDMI) && (u4Addr > 255)) ||
	    ((ucAddrType == SIF_16_BIT_HDMI) && (u4Addr > 65535))) {
		return 0;
	}

	if (ucAddrType == SIF_8_BIT_HDMI)
		u4ReadCount = ((255 - u4Addr) + 1);
	else if (ucAddrType == SIF_16_BIT_HDMI)
		u4ReadCount = ((65535 - u4Addr) + 1);

	u4ReadCount = (u4ReadCount > u4Count) ? u4Count : u4ReadCount;
	ucReturnVaule = _DDCMRead_hdmi(ddc, 0, u4ClkDiv, ucDev, u4Addr,
				       ucAddrType, pucValue, u4ReadCount);
	return ucReturnVaule;
}

static unsigned char fgDDCDataRead(struct mtk_hdmi_ddc *ddc, unsigned char bDevice,
			    unsigned char bData_Addr, unsigned char bDataCount,
			    unsigned char *prData)
{
	bool flag;

	mutex_lock(&ddc->mtx);

	hdmi_ddc_request(ddc);
	if (vDDCRead(ddc, DDC2_CLOK, (unsigned char)bDevice,
		     (unsigned int)bData_Addr, SIF_8_BIT_HDMI,
		     (unsigned char *)prData,
		     (unsigned int)bDataCount) == bDataCount) {
		flag = true;
	} else {
		flag = false;
	}

	mutex_unlock(&ddc->mtx);
	return flag;
}

static unsigned char fgDDCDataWrite(struct mtk_hdmi_ddc *ddc,
				    unsigned char bDevice,
				    unsigned char bData_Addr,
				    unsigned char bDataCount,
				    unsigned char *prData)
{
	unsigned int i;

	mutex_lock(&ddc->mtx);

	hdmi_ddc_request(ddc);
	for (i = 0; i < bDataCount; i++)
		DDC_WR_ONE(ddc, bDevice, bData_Addr + i, *(prData + i));

	mutex_unlock(&ddc->mtx);
	return 1;
}

static int mtk_hdmi_ddc_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			     int num)
{
	struct mtk_hdmi_ddc *ddc = adapter->algo_data;
	struct device *dev = adapter->dev.parent;
	int ret;
	int i;

	if (!ddc)
		return -EINVAL;

	for (i = 0; i < num; i++) {
		struct i2c_msg *msg = &msgs[i];

		if (msg->flags & I2C_M_RD)
			ret = fgDDCDataRead(ddc, msg->addr, msg->buf[0],
					    (msg->len), &msg->buf[0]);
		else
			ret = fgDDCDataWrite(ddc, msg->addr, msg->buf[0],
					     (msg->len - 1), &msg->buf[1]);

		if (ret <= 0)
			goto xfer_end;
	}

	return i;

xfer_end:
	dev_err(dev, "ddc failed!\n");
	return ret;
}

static u32 mtk_hdmi_ddc_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm mtk_hdmi_ddc_algorithm = {
	.master_xfer = mtk_hdmi_ddc_xfer,
	.functionality = mtk_hdmi_ddc_func,
};

static int mtk_hdmi_ddc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_hdmi_ddc *ddc;
	int ret;

	ddc = devm_kzalloc(dev, sizeof(struct mtk_hdmi_ddc), GFP_KERNEL);
	if (!ddc)
		return -ENOMEM;

	ddc->clk = devm_clk_get(dev, "ddc-i2c");
	if (IS_ERR(ddc->clk)) {
		dev_err(dev, "get ddc_clk failed: %p ,\n", ddc->clk);
		return PTR_ERR(ddc->clk);
	}
	ret = clk_prepare_enable(ddc->clk);
	if (ret) {
		dev_err(dev, "enable ddc clk failed!\n");
		return ret;
	}

	mutex_init(&ddc->mtx);

	strscpy(ddc->adap.name, "mediatek-hdmi-ddc", sizeof(ddc->adap.name));
	ddc->adap.owner = THIS_MODULE;
	ddc->adap.class = I2C_CLASS_DDC;
	ddc->adap.algo = &mtk_hdmi_ddc_algorithm;
	ddc->adap.retries = 3;
	ddc->adap.dev.of_node = dev->of_node;
	ddc->adap.algo_data = ddc;
	ddc->adap.dev.parent = &pdev->dev;

	ret = i2c_add_adapter(&ddc->adap);
	if (ret < 0) {
		dev_err(dev, "failed to add bus to i2c core\n");
		goto err_clk_disable;
	}

	platform_set_drvdata(pdev, ddc);
	return 0;

err_clk_disable:
	clk_disable_unprepare(ddc->clk);
	return ret;
}

static int mtk_hdmi_ddc_remove(struct platform_device *pdev)
{
	struct mtk_hdmi_ddc *ddc = platform_get_drvdata(pdev);

	mutex_destroy(&ddc->mtx);
	i2c_del_adapter(&ddc->adap);
	clk_disable_unprepare(ddc->clk);

	return 0;
}

static const struct of_device_id mtk_hdmi_ddc_match[] = {
	{
		.compatible = "mediatek,mt8195-hdmi-ddc",
	},
	{},
};

struct platform_driver mtk_hdmi_mt8195_ddc_driver = {
	.probe = mtk_hdmi_ddc_probe,
	.remove = mtk_hdmi_ddc_remove,
	.driver = {
		.name = "mediatek-hdmi-mt8195-ddc",
		.of_match_table = mtk_hdmi_ddc_match,
	},
};

MODULE_AUTHOR("Can Zeng <can.zeng@mediatek.com>");
MODULE_DESCRIPTION("MediaTek HDMI DDC Driver");
MODULE_LICENSE("GPL v2");
