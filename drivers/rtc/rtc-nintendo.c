// SPDX-License-Identifier: GPL-2.0
/*
 * Nintendo GameCube, Wii and Wii U RTC driver
 *
 * This driver is for the MX23L4005, more specifically its real-time clock and
 * SRAM storage.  The value returned by the RTC counter must be added with the
 * offset stored in a bias register in SRAM (on the GameCube and Wii) or in
 * /config/rtc.xml (on the Wii U).  The latter being very impractical to access
 * from Linux, this driver assumes the bootloader has read it and stored it in
 * SRAM like for the other two consoles.
 *
 * This device sits on a bus named EXI (which is similar to SPI), channel 0,
 * device 1.  This driver assumes no other user of the EXI bus, which is
 * currently the case but would have to be reworked to add support for other
 * GameCube hardware exposed on this bus.
 *
 * Note that this device is very much not Y2k38 aware.
 *
 * References:
 * - https://wiiubrew.org/wiki/Hardware/RTC
 * - https://wiibrew.org/wiki/MX23L4005
 *
 * Copyright (C) 2018 rw-r-r-0644
 * Copyright (C) 2021 Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>
 *
 * Based on rtc-gcn.c
 * Copyright (C) 2004-2009 The GameCube Linux Team
 * Copyright (C) 2005,2008,2009 Albert Herranz
 * Based on gamecube_time.c from Torben Nielsen.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/time.h>

/* EXI registers */
#define EXICSR	0
#define EXICR	12
#define EXIDATA	16

/* EXI register values */
#define EXICSR_DEV		0x380
	#define EXICSR_DEV1	0x100
#define EXICSR_CLK		0x070
	#define EXICSR_CLK_1MHZ	0x000
	#define EXICSR_CLK_2MHZ	0x010
	#define EXICSR_CLK_4MHZ	0x020
	#define EXICSR_CLK_8MHZ	0x030
	#define EXICSR_CLK_16MHZ 0x040
	#define EXICSR_CLK_32MHZ 0x050
#define EXICSR_INT		0x008
	#define EXICSR_INTSET	0x008

#define EXICR_TSTART		0x001
#define EXICR_TRSMODE		0x002
	#define EXICR_TRSMODE_IMM 0x000
#define EXICR_TRSTYPE		0x00C
	#define EXICR_TRSTYPE_R	0x000
	#define EXICR_TRSTYPE_W	0x004
#define EXICR_TLEN		0x030
	#define EXICR_TLEN32	0x030

/* EXI registers values to access the RTC */
#define RTC_EXICSR	(EXICSR_DEV1 | EXICSR_CLK_8MHZ | EXICSR_INTSET)
#define RTC_EXICR_W	(EXICR_TSTART | EXICR_TRSMODE_IMM | EXICR_TRSTYPE_W | EXICR_TLEN32)
#define RTC_EXICR_R	(EXICR_TSTART | EXICR_TRSMODE_IMM | EXICR_TRSTYPE_R | EXICR_TLEN32)
#define RTC_EXIDATA_W	0x80000000

/* RTC registers */
#define RTC_COUNTER	0x20000000
#define RTC_SRAM	0x20000100
#define RTC_SRAM_BIAS	0x20000400
#define RTC_SNAPSHOT	0x20400000
#define RTC_ONTMR	0x21000000
#define RTC_OFFTMR	0x21000100
#define RTC_TEST0	0x21000400
#define RTC_TEST1	0x21000500
#define RTC_TEST2	0x21000600
#define RTC_TEST3	0x21000700
#define RTC_CONTROL0	0x21000C00
#define RTC_CONTROL1	0x21000D00

struct nintendo_rtc_drvdata {
	void __iomem *iob;
	u32 rtc_bias;
};

static u32 exi_read(void __iomem *iob, u32 reg)
{
	u32 data;

	/* The spin loops here loop about 15~16 times each, so there is no need
	 * to use a more expensive sleep method. */

	/* Write register offset */
	iowrite32be(RTC_EXICSR, iob + EXICSR);
	iowrite32be(reg, iob + EXIDATA);
	iowrite32be(RTC_EXICR_W, iob + EXICR);
	while (!(ioread32be(iob + EXICSR) & EXICSR_INTSET))
		cpu_relax();

	/* Read data */
	iowrite32be(RTC_EXICSR, iob + EXICSR);
	iowrite32be(RTC_EXICR_R, iob + EXICR);
	while (!(ioread32be(iob + EXICSR) & EXICSR_INTSET))
		cpu_relax();
	data = ioread32be(iob + EXIDATA);

	/* Clear channel parameters */
	iowrite32be(0, iob + EXICSR);

	return data;
}

static void exi_write(void __iomem *iob, u32 reg, u32 data)
{
	/* The spin loops here loop about 15~16 times each, so there is no need
	 * to use a more expensive sleep method. */

	/* Write register offset */
	iowrite32be(RTC_EXICSR, iob + EXICSR);
	iowrite32be(reg | RTC_EXIDATA_W, iob + EXIDATA);
	iowrite32be(RTC_EXICR_W, iob + EXICR);
	while (!(ioread32be(iob + EXICSR) & EXICSR_INTSET))
		cpu_relax();

	/* Write data */
	iowrite32be(RTC_EXICSR, iob + EXICSR);
	iowrite32be(data, iob + EXIDATA);
	iowrite32be(RTC_EXICR_W, iob + EXICR);
	while (!(ioread32be(iob + EXICSR) & EXICSR_INTSET))
		cpu_relax();

	/* Clear channel parameters */
	iowrite32be(0, iob + EXICSR);
}

static int nintendo_rtc_read_time(struct device *dev, struct rtc_time *t)
{
	time64_t timestamp;
	struct nintendo_rtc_drvdata *d = dev_get_drvdata(dev);

	/* Add the counter and the bias to obtain the timestamp */
	timestamp = exi_read(d->iob, RTC_COUNTER) + d->rtc_bias;
	rtc_time64_to_tm(timestamp, t);

	return 0;
}

static int nintendo_rtc_set_time(struct device *dev, struct rtc_time *t)
{
	time64_t timestamp;
	struct nintendo_rtc_drvdata *d = dev_get_drvdata(dev);

	/* Subtract the timestamp and the bias to obtain the counter value */
	timestamp = rtc_tm_to_time64(t);
	exi_write(d->iob, RTC_COUNTER, timestamp - d->rtc_bias);

	return 0;
}

static const struct rtc_class_ops nintendo_rtc_ops = {
	.read_time      = nintendo_rtc_read_time,
	.set_time       = nintendo_rtc_set_time,
};

#ifdef DEBUG
static void nintendo_rtc_dumpregs(void __iomem *iob)
{
	int i;
	u32 sram_addr = RTC_SRAM;

	printk("RTC_COUNTER:  %08X\n", exi_read(iob, RTC_COUNTER));
	printk("RTC_SNAPSHOT: %08X\n", exi_read(iob, RTC_SNAPSHOT));
	printk("RTC_ONTMR:    %08X\n", exi_read(iob, RTC_ONTMR));
	printk("RTC_OFFTMR:   %08X\n", exi_read(iob, RTC_OFFTMR));
	printk("RTC_TEST0:    %08X\n", exi_read(iob, RTC_TEST0));
	printk("RTC_TEST1:    %08X\n", exi_read(iob, RTC_TEST1));
	printk("RTC_TEST2:    %08X\n", exi_read(iob, RTC_TEST2));
	printk("RTC_TEST3:    %08X\n", exi_read(iob, RTC_TEST3));
	printk("RTC_CONTROL0: %08X\n", exi_read(iob, RTC_CONTROL0));
	printk("RTC_CONTROL1: %08X\n", exi_read(iob, RTC_CONTROL1));
	printk("RTC_SRAM:\n");
	for(i = 0; i < 4; i++) {
		printk("%08X %08X %08X %08X\n",
		       exi_read(iob, sram_addr + 0x100 * 0),
		       exi_read(iob, sram_addr + 0x100 * 1),
		       exi_read(iob, sram_addr + 0x100 * 2),
		       exi_read(iob, sram_addr + 0x100 * 3));
		sram_addr += 0x400;
	}
}
#endif

static int nintendo_rtc_read_offset_from_sram(struct nintendo_rtc_drvdata *d)
{
	struct device_node *np;
	int ret;
	struct resource res;
	void __iomem *hw_srnprot;
	u32 old;

	np = of_find_compatible_node(NULL, NULL, "nintendo,latte-srnprot");
	if (!np) {
		pr_err("HW_SRNPROT not found\n");
		return -1;
	}

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		pr_err("no io memory range found\n");
		return -1;
	}

	hw_srnprot = ioremap(res.start, resource_size(&res));
	old = ioread32be(hw_srnprot);

	/* TODO: figure out why we use this magic constant.  I obtained it by
	 * reading the leftover value after boot, after IOSU already ran.
	 *
	 * On my Wii U, setting this register to 1 prevents the console from
	 * rebooting properly, so wiiubrew.org must be missing something.
	 *
	 * See https://wiiubrew.org/wiki/Hardware/Latte_registers
	 */
	if (old != 0x7bf)
		iowrite32be(0x7bf, hw_srnprot);

#ifdef DEBUG
	nintendo_rtc_dumpregs(d->iob);
#endif

	/* Get the offset from RTC SRAM.
	 *
	 * Its default location on the GameCube and on the Wii is in the SRAM,
	 * while on the Wii U the bootloader needs to fill it with the contents
	 * of /config/rtc.xml on the SLC (the eMMC).  We don’t do that from
	 * Linux since it requires implementing a proprietary filesystem and do
	 * file decryption, instead we require the bootloader to fill the same
	 * SRAM address as on previous consoles.
	 */
	d->rtc_bias = exi_read(d->iob, RTC_SRAM_BIAS);

	/* Reset SRAM access to how it was before, our job here is done. */
	iowrite32be(old, hw_srnprot);
	iounmap(hw_srnprot);

	return 0;
}

static int nintendo_rtc_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	struct rtc_device *rtc;
	struct resource	*res;
	struct nintendo_rtc_drvdata *d;
	int ret;

	d = devm_kzalloc(dev, sizeof(struct nintendo_rtc_drvdata), GFP_KERNEL);
	if (IS_ERR(d))
		return PTR_ERR(d);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	d->iob = devm_ioremap_resource(dev, res);
	if (IS_ERR(d->iob))
		return PTR_ERR(d->iob);

	ret = nintendo_rtc_read_offset_from_sram(d);
	if (ret)
		return ret;
	dev_dbg(dev, "SRAM bias: 0x%x", d->rtc_bias);

	dev_set_drvdata(dev, d);

	rtc = devm_rtc_device_register(dev, dev_name(dev), &nintendo_rtc_ops,
				       THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	return 0;
}

static const struct of_device_id nintendo_rtc_of_match[] = {
	{.compatible = "nintendo,latte-exi" },
	{.compatible = "nintendo,hollywood-exi" },
	{.compatible = "nintendo,flipper-exi" },
	{ }
};
MODULE_DEVICE_TABLE(of, nintendo_rtc_of_match);

static struct platform_driver nintendo_rtc_driver = {
	.probe		= nintendo_rtc_probe,
	.driver		= {
		.name	= "rtc-nintendo",
		.of_match_table	= nintendo_rtc_of_match,
	},
};
module_platform_driver(nintendo_rtc_driver);

MODULE_AUTHOR("Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>");
MODULE_DESCRIPTION("Nintendo GameCube, Wii and Wii U RTC driver");
MODULE_LICENSE("GPL");
