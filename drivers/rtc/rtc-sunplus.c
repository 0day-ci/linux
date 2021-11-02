// SPDX-License-Identifier: GPL-2.0-or-later

/**************************************************************************************************/
/* How to test RTC:										  */
/*												  */
/* 1. use kernel commands									  */
/* hwclock - query and set the hardware clock (RTC)						  */
/*												  */
/* (for i in `seq 5`; do (echo ------ && echo -n 'date      : ' && date && echo -n 'hwclock -r: ' */
/*								&& hwclock -r; sleep 1); done)	  */
/* date 121209002014 # Set system to 2014/Dec/12 09:00						  */
/* (for i in `seq 5`; do (echo ------ && echo -n 'date      : ' && date && echo -n 'hwclock -r: ' */
/*								&& hwclock -r; sleep 1); done)	  */
/* hwclock -s # Set the System Time from the Hardware Clock					  */
/* (for i in `seq 5`; do (echo ------ && echo -n 'date      : ' && date && echo -n 'hwclock -r: ' */
/*								&& hwclock -r; sleep 1); done)	  */
/* date 121213002014 # Set system to 2014/Dec/12 13:00						  */
/* (for i in `seq 5`; do (echo ------ && echo -n 'date      : ' && date && echo -n 'hwclock -r: ' */
/*								&& hwclock -r; sleep 1); done)	  */
/* hwclock -w # Set the Hardware Clock to the current System Time				  */
/* (for i in `seq 10000`; do (echo ------ && echo -n 'date  : ' && date && echo -n 'hwclock -r: ' */
/*								&& hwclock -r; sleep 1); done)	  */
/*												  */
/* How to setup alarm (e.g., 10 sec later):							  */
/*     echo 0 > /sys/class/rtc/rtc0/wakealarm && nnn=`date '+%s'` && echo $nnn && \		  */
/*     nnn=`expr $nnn + 10` && echo $nnn > /sys/class/rtc/rtc0/wakealarm			  */
/*												  */
/* 2. use RTC Driver Test Program (\linux\application\module_test\rtc\rtc-test.c)		  */
/*												  */
/**************************************************************************************************/
#include <linux/module.h>
#include <linux/err.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/of.h>
#include <linux/ktime.h>
#include <linux/io.h>

/* ---------------------------------------------------------------------------------------------- */
#define FUNC_DEBUG() pr_debug("[RTC] Debug: %s(%d)\n", __func__, __LINE__)

#define RTC_DEBUG(fmt, args ...) pr_debug("[RTC] Debug: " fmt, ## args)
#define RTC_INFO(fmt, args ...) pr_info("[RTC] Info: " fmt, ## args)
#define RTC_WARN(fmt, args ...) pr_warn("[RTC] Warning: " fmt, ## args)
#define RTC_ERR(fmt, args ...) pr_err("[RTC] Error: " fmt, ## args)
/* ---------------------------------------------------------------------------------------------- */

struct sunplus_rtc {
	struct clk *rtcclk;
	struct reset_control *rstc;
	unsigned long set_alarm_again;
	u32 charging_mode;
};

struct sunplus_rtc sp_rtc;

#define RTC_REG_NAME		"rtc_reg"

struct sp_rtc_reg {
	unsigned int rsv00;
	unsigned int rsv01;
	unsigned int rsv02;
	unsigned int rsv03;
	unsigned int rsv04;
	unsigned int rsv05;
	unsigned int rsv06;
	unsigned int rsv07;
	unsigned int rsv08;
	unsigned int rsv09;
	unsigned int rsv10;
	unsigned int rsv11;
	unsigned int rsv12;
	unsigned int rsv13;
	unsigned int rsv14;
	unsigned int rsv15;
	unsigned int rtc_ctrl;
	unsigned int rtc_timer_out;
	unsigned int rtc_divider;
	unsigned int rtc_timer_set;
	unsigned int rtc_alarm_set;
	unsigned int rtc_user_data;
	unsigned int rtc_reset_record;
	unsigned int rtc_battery_ctrl;
	unsigned int rtc_trim_ctrl;
	unsigned int rsv25;
	unsigned int rsv26;
	unsigned int rsv27;
	unsigned int rsv28;
	unsigned int rsv29;
	unsigned int rsv30;
	unsigned int rsv31;
};

static struct sp_rtc_reg *rtc_reg_ptr;

static void sp_get_seconds(unsigned long *secs)
{
	*secs = (unsigned long)readl(&rtc_reg_ptr->rtc_timer_out);
}

static void sp_set_seconds(unsigned long secs)
{
	writel((u32)secs, &rtc_reg_ptr->rtc_timer_set);
}

static int sp_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long secs;

	sp_get_seconds(&secs);
	rtc_time64_to_tm(secs, tm);
	RTC_DEBUG("%s:  RTC date/time to %d-%d-%d, %02d:%02d:%02d.\r\n",
		__func__, tm->tm_mday, tm->tm_mon + 1, tm->tm_year,
					tm->tm_hour, tm->tm_min, tm->tm_sec);

	return rtc_valid_tm(tm);
}

int sp_rtc_get_time(struct rtc_time *tm)
{
	unsigned long secs;

	sp_get_seconds(&secs);
	rtc_time64_to_tm(secs, tm);
	return 0;
}
EXPORT_SYMBOL(sp_rtc_get_time);

static int sp_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	FUNC_DEBUG();

	// Keep RTC from system reset
	writel((1 << (16+4)) | (1 << 4), &rtc_reg_ptr->rtc_ctrl);

	return 0;
}

static int sp_rtc_resume(struct platform_device *pdev)
{
	/*						*/
	/* Because RTC is still powered during suspend,	*/
	/* there is nothing to do here.			*/
	/*						*/
	FUNC_DEBUG();

	// Keep RTC from system reset
	writel((1 << (16+4)) | (1 << 4), &rtc_reg_ptr->rtc_ctrl);

	return 0;
}

static int sp_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long secs;

	secs = rtc_tm_to_time64(tm);
	RTC_DEBUG("%s, secs = %lu\n", __func__, secs);
	sp_set_seconds(secs);

	return 0;
}

static int sp_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_device *rtc = dev_get_drvdata(dev);
	unsigned long alarm_time;

	alarm_time = rtc_tm_to_time64(&alrm->time);
	RTC_DEBUG("%s, alarm_time: %u\n", __func__, (u32)(alarm_time));

	if (alarm_time > 0xFFFFFFFF)
		return -EINVAL;

	if ((rtc->aie_timer.enabled) && (rtc->aie_timer.node.expires == ktime_set(alarm_time, 0))) {
		if (rtc->uie_rtctimer.enabled)
			sp_rtc.set_alarm_again = 1;
	}

	writel((u32)alarm_time, &rtc_reg_ptr->rtc_alarm_set);
	wmb();			// make sure settings are effective.

	// enable alarm for update irq
	if (rtc->uie_rtctimer.enabled)
		writel((0x003F << 16) | 0x17, &rtc_reg_ptr->rtc_ctrl);
	else if (!rtc->aie_timer.enabled)
		writel((0x0007 << 16) | 0x0, &rtc_reg_ptr->rtc_ctrl);

	return 0;
}

static int sp_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	unsigned int alarm_time;

	alarm_time = readl(&rtc_reg_ptr->rtc_alarm_set);
	RTC_DEBUG("%s, alarm_time: %u\n", __func__, alarm_time);
	rtc_time64_to_tm((unsigned long)(alarm_time), &alrm->time);

	return 0;
}

static int sp_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rtc_device *rtc = dev_get_drvdata(dev);

	if (enabled)
		writel((0x003F << 16) | 0x17, &rtc_reg_ptr->rtc_ctrl);
	else if (!rtc->uie_rtctimer.enabled)
		writel((0x0007 << 16) | 0x0, &rtc_reg_ptr->rtc_ctrl);

	return 0;
}

static const struct rtc_class_ops sp_rtc_ops = {
	.read_time =		sp_rtc_read_time,
	.set_time =		sp_rtc_set_time,
	.set_alarm =		sp_rtc_set_alarm,
	.read_alarm =		sp_rtc_read_alarm,
	.alarm_irq_enable =	sp_rtc_alarm_irq_enable,
};

static irqreturn_t rtc_irq_handler(int irq, void *dev_id)
{
	struct platform_device *plat_dev = dev_id;
	struct rtc_device *rtc = platform_get_drvdata(plat_dev);

	if (rtc->uie_rtctimer.enabled) {
		rtc_update_irq(rtc, 1, RTC_IRQF | RTC_UF);
		RTC_DEBUG("[RTC] update irq\n");

		if (sp_rtc.set_alarm_again == 1) {
			sp_rtc.set_alarm_again = 0;
			rtc_update_irq(rtc, 1, RTC_IRQF | RTC_AF);
			RTC_DEBUG("[RTC] alarm irq\n");
		}
	} else {
		rtc_update_irq(rtc, 1, RTC_IRQF | RTC_AF);
		RTC_DEBUG("[RTC] alarm irq\n");
	}

	return IRQ_HANDLED;
}

/* ---------------------------------------------------------------------------------------------- */
/* mode   bat_charge_rsel   bat_charge_dsel   bat_charge_en     Remarks				  */
/* 0xE            x              x                 0            Disable				  */
/* 0x1            0              0                 1            0.86mA (2K Ohm with diode)	  */
/* 0x5            1              0                 1            1.81mA (250 Ohm with diode)	  */
/* 0x9            2              0                 1            2.07mA (50 Ohm with diode)	  */
/* 0xD            3              0                 1            16.0mA (0 Ohm with diode)	  */
/* 0x3            0              1                 1            1.36mA (2K Ohm without diode)	  */
/* 0x7            1              1                 1            3.99mA (250 Ohm without diode)	  */
/* 0xB            2              1                 1            4.41mA (50 Ohm without diode)	  */
/* 0xF            3              1                 1            16.0mA (0 Ohm without diode)	  */
/* ---------------------------------------------------------------------------------------------- */
static void sp_rtc_set_batt_charge_ctrl(u32 _mode)
{
	u8 m = _mode & 0x000F;

	RTC_DEBUG("battery charge mode: 0x%X\n", m);
	writel((0x000F << 16) | m, &rtc_reg_ptr->rtc_battery_ctrl);
}

static int sp_rtc_probe(struct platform_device *plat_dev)
{
	int ret;
	int err, irq;
	struct rtc_device *rtc = NULL;
	struct resource *res;
	void __iomem *reg_base = NULL;

	FUNC_DEBUG();

	memset(&sp_rtc, 0, sizeof(sp_rtc));

	// find and map our resources
	res = platform_get_resource_byname(plat_dev, IORESOURCE_MEM, RTC_REG_NAME);
	RTC_DEBUG("res = 0x%x\n", res->start);

	if (res) {
		reg_base = devm_ioremap_resource(&plat_dev->dev, res);
		if (IS_ERR(reg_base))
			RTC_ERR("%s devm_ioremap_resource fail\n", RTC_REG_NAME);
	}
	RTC_DEBUG("reg_base = 0x%lx\n", (unsigned long)reg_base);

	// clk
	sp_rtc.rtcclk = devm_clk_get(&plat_dev->dev, NULL);
	RTC_DEBUG("sp_rtc->clk = 0x%lx\n", (unsigned long)sp_rtc.rtcclk);
	if (IS_ERR(sp_rtc.rtcclk))
		RTC_DEBUG("devm_clk_get fail\n");

	ret = clk_prepare_enable(sp_rtc.rtcclk);

	// reset
	sp_rtc.rstc = devm_reset_control_get(&plat_dev->dev, NULL);
	RTC_DEBUG("sp_rtc->rstc = 0x%lx\n", (unsigned long)sp_rtc.rstc);
	if (IS_ERR(sp_rtc.rstc)) {
		ret = PTR_ERR(sp_rtc.rstc);
		RTC_ERR("SPI failed to retrieve reset controller: %d\n", ret);
		goto free_clk;
	}

	ret = reset_control_deassert(sp_rtc.rstc);
	if (ret)
		goto free_clk;

	rtc_reg_ptr = (struct sp_rtc_reg *)(reg_base);

	// Keep RTC from system reset
	writel((1 << (16+4)) | (1 << 4), &rtc_reg_ptr->rtc_ctrl);

	// request irq
	irq = platform_get_irq(plat_dev, 0);
	if (irq < 0) {
		RTC_ERR("platform_get_irq failed\n");
		goto free_reset_assert;
	}

	err = devm_request_irq(&plat_dev->dev, irq, rtc_irq_handler,
					IRQF_TRIGGER_RISING, "rtc irq", plat_dev);
	if (err) {
		RTC_ERR("devm_request_irq failed: %d\n", err);
		goto free_reset_assert;
	}

	// Get charging-mode.
	ret = of_property_read_u32(plat_dev->dev.of_node, "charging-mode", &sp_rtc.charging_mode);
	if (ret) {
		RTC_ERR("Failed to retrieve \'charging-mode\'!\n");
		goto free_reset_assert;
	}
	sp_rtc_set_batt_charge_ctrl(sp_rtc.charging_mode);

	device_init_wakeup(&plat_dev->dev, 1);

	rtc = devm_rtc_device_register(&plat_dev->dev, "sp7021-rtc", &sp_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		goto free_reset_assert;
	}

	platform_set_drvdata(plat_dev, rtc);

	RTC_INFO("sp7021-rtc loaded\n");

	return 0;

free_reset_assert:
	reset_control_assert(sp_rtc.rstc);
free_clk:
	clk_disable_unprepare(sp_rtc.rtcclk);

	return ret;
}

static int sp_rtc_remove(struct platform_device *plat_dev)
{
	reset_control_assert(sp_rtc.rstc);

	return 0;
}

static const struct of_device_id sp_rtc_of_match[] = {
	{ .compatible = "sunplus,sp7021-rtc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sp_rtc_of_match);

static struct platform_driver sp_rtc_driver = {
	.probe   = sp_rtc_probe,
	.remove  = sp_rtc_remove,
	.suspend = sp_rtc_suspend,
	.resume  = sp_rtc_resume,
	.driver  = {
		.name = "sp7021-rtc",
		.owner = THIS_MODULE,
		.of_match_table = sp_rtc_of_match,
	},
};
module_platform_driver(sp_rtc_driver);

MODULE_AUTHOR("Vincent Shih <vincent.shih@sunplus.com>");
MODULE_DESCRIPTION("Sunplus RTC driver");
MODULE_LICENSE("GPL v2");

