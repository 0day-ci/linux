// SPDX-License-Identifier: GPL-2.0
/*
 * The IOP driver for Sunplus SP7021
 *
 * Copyright (C) 2021 Sunplus Technology Inc.
 *
 * All Rights Reserved.
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

enum IOP_Status_e {
	IOP_SUCCESS,                /* successful */
	IOP_ERR_IOP_BUSY,           /* IOP is busy */
};

struct regs_moon0 {
	u32 stamp;         /* 00 */
	u32 clken[10];     /* 01~10 */
	u32 gclken[10];    /* 11~20 */
	u32 reset[10];     /* 21~30 */
	u32 sfg_cfg_mode;  /* 31 */
};

struct regs_iop {
	u32 iop_control;/* 00 */
	u32 iop_reg1;/* 01 */
	u32 iop_bp;/* 02 */
	u32 iop_regsel;/* 03 */
	u32 iop_regout;/* 04 */
	u32 iop_reg5;/* 05 */
	u32 iop_resume_pcl;/* 06 */
	u32 iop_resume_pch;/* 07 */
	u32 iop_data0;/* 08 */
	u32 iop_data1;/* 09 */
	u32 iop_data2;/* 10 */
	u32 iop_data3;/* 11 */
	u32 iop_data4;/* 12 */
	u32 iop_data5;/* 13 */
	u32 iop_data6;/* 14 */
	u32 iop_data7;/* 15 */
	u32 iop_data8;/* 16 */
	u32 iop_data9;/* 17 */
	u32 iop_data10;/* 18 */
	u32 iop_data11;/* 19 */
	u32 iop_base_adr_l;/* 20 */
	u32 iop_base_adr_h;/* 21 */
	u32 memory_bridge_control;/* 22 */
	u32 iop_regmap_adr_l;/* 23 */
	u32 iop_regmap_adr_h;/* 24 */
	u32 iop_direct_adr;/* 25*/
	u32 reserved[6];/* 26~31 */
};

struct regs_iop_pmc {
	u32 PMC_TIMER;/* 00 */
	u32 PMC_CTRL;/* 01 */
	u32 XTAL27M_PASSWORD_I;/* 02 */
	u32 XTAL27M_PASSWORD_II;/* 03 */
	u32 XTAL32K_PASSWORD_I;/* 04 */
	u32 XTAL32K_PASSWORD_II;/* 05 */
	u32 CLK27M_PASSWORD_I;/* 06 */
	u32 CLK27M_PASSWORD_II;/* 07 */
	u32 PMC_TIMER2;/* 08 */
	u32 reserved[23];/* 9~31 */
};

#define NORMAL_CODE_MAX_SIZE 0X1000
#define STANDBY_CODE_MAX_SIZE 0x4000
struct sp_iop {
	struct miscdevice dev;			// iop device
	struct mutex write_lock;
	void *iop_regs;
	void *pmc_regs;
	void *moon0_regs;
	int irq;
	int gpio_wakeup;
	unsigned char iop_normal_code[NORMAL_CODE_MAX_SIZE];
	unsigned char iop_standby_code[STANDBY_CODE_MAX_SIZE];
	resource_size_t iop_mem_start;
	resource_size_t iop_mem_size;
	bool mode;
};

static void sp_iop_normal_mode(struct sp_iop *iop)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	struct regs_moon0 *p_moon0_reg = (struct regs_moon0 *)iop->moon0_regs;
	void *iop_kernel_base;
	unsigned int reg;

	iop_kernel_base = ioremap(iop->iop_mem_start, NORMAL_CODE_MAX_SIZE);
	memset(iop_kernel_base, 0, NORMAL_CODE_MAX_SIZE);
	memcpy(iop_kernel_base, iop->iop_normal_code, NORMAL_CODE_MAX_SIZE);

	writel(0x00100010, &p_moon0_reg->clken[0]);

	reg = readl(&p_iop_reg->iop_control);
	reg |= 0x01;
	writel(reg, &p_iop_reg->iop_control);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &p_iop_reg->iop_control);

	reg = readl(&p_iop_reg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &p_iop_reg->iop_control);

	reg = (iop->iop_mem_start & 0xFFFF);
	writel(reg, &p_iop_reg->iop_base_adr_l);
	reg	= (iop->iop_mem_start >> 16);
	writel(reg, &p_iop_reg->iop_base_adr_h);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x01);
	writel(reg, &p_iop_reg->iop_control);
	iop->mode = 0;
}

static void sp_iop_standby_mode(struct sp_iop *iop)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	struct regs_moon0 *p_moon0_reg = (struct regs_moon0 *)iop->moon0_regs;
	void *iop_kernel_base;
	unsigned long reg;

	iop_kernel_base = ioremap(iop->iop_mem_start, STANDBY_CODE_MAX_SIZE);
	memset(iop_kernel_base, 0, STANDBY_CODE_MAX_SIZE);
	memcpy(iop_kernel_base, iop->iop_standby_code, STANDBY_CODE_MAX_SIZE);

	writel(0x00100010, &p_moon0_reg->clken[0]);

	reg = readl(&p_iop_reg->iop_control);
	reg |= 0x01;
	writel(reg, &p_iop_reg->iop_control);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &p_iop_reg->iop_control);

	reg = readl(&p_iop_reg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &p_iop_reg->iop_control);

	reg = (iop->iop_mem_start & 0xFFFF);
	writel(reg, &p_iop_reg->iop_base_adr_l);
	reg = (iop->iop_mem_start >> 16);
	writel(reg, &p_iop_reg->iop_base_adr_h);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x01);
	writel(reg, &p_iop_reg->iop_control);
	iop->mode = 1;
}

#define IOP_READY	0x4
#define RISC_READY	0x8
#define WAKEUP_PIN	0xFE02
#define S1	0x5331
#define S3	0x5333
static int sp_iop_s3mode(struct device *dev, struct sp_iop *iop)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	struct regs_moon0 *p_moon0_reg = (struct regs_moon0 *)iop->moon0_regs;
	struct regs_iop_pmc *p_iop_pmc_reg = (struct regs_iop_pmc *)iop->pmc_regs;
	unsigned int reg;
	int ret, value;

	writel(0x00100010, &p_moon0_reg->clken[0]);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &p_iop_reg->iop_control);

	reg = readl(&p_iop_reg->iop_control);
	reg |= 0x1;
	writel(reg, &p_iop_reg->iop_control);

	//PMC set
	writel(0x00010001, &p_iop_pmc_reg->PMC_TIMER);
	reg = readl(&p_iop_pmc_reg->PMC_CTRL);
	reg |= 0x23;// disable system reset PMC, enalbe power down 27M, enable gating 27M
	writel(reg, &p_iop_pmc_reg->PMC_CTRL);

	writel(0x55aa00ff, &p_iop_pmc_reg->XTAL27M_PASSWORD_I);
	writel(0x00ff55aa, &p_iop_pmc_reg->XTAL27M_PASSWORD_II);
	writel(0xaa00ff55, &p_iop_pmc_reg->XTAL32K_PASSWORD_I);
	writel(0xff55aa00, &p_iop_pmc_reg->XTAL32K_PASSWORD_II);
	writel(0xaaff0055, &p_iop_pmc_reg->CLK27M_PASSWORD_I);
	writel(0x5500aaff, &p_iop_pmc_reg->CLK27M_PASSWORD_II);
	writel(0x01000100, &p_iop_pmc_reg->PMC_TIMER2);

	//IOP Hardware IP reset
	reg = readl(&p_moon0_reg->reset[0]);
	reg |= 0x10;
	writel(reg, (&p_moon0_reg->reset[0]));
	reg &= ~(0x10);
	writel(reg, (&p_moon0_reg->reset[0]));

	writel(0x00ff0085, (iop->moon0_regs + 32 * 4 * 1 + 4 * 1));

	reg = readl(iop->moon0_regs + 32 * 4 * 1 + 4 * 2);
	reg |= 0x08000800;
	writel(reg, (iop->moon0_regs + 32 * 4 * 1 + 4 * 2));

	reg = readl(&p_iop_reg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &p_iop_reg->iop_control);

	reg = (iop->iop_mem_start & 0xFFFF);
	writel(reg, &p_iop_reg->iop_base_adr_l);
	reg = (iop->iop_mem_start >> 16);
	writel(reg, &p_iop_reg->iop_base_adr_h);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x01);
	writel(reg, &p_iop_reg->iop_control);

	writel(WAKEUP_PIN, &p_iop_reg->iop_data0);
	writel(iop->gpio_wakeup, &p_iop_reg->iop_data1);

	ret = readl_poll_timeout(&p_iop_reg->iop_data2, value,
				 (value & IOP_READY) == IOP_READY, 1000, 10000);
	if (ret) {
		dev_err(dev, "timed out\n");
		return ret;
	}

	writel(RISC_READY, &p_iop_reg->iop_data2);
	writel(0x00, &p_iop_reg->iop_data5);
	writel(0x60, &p_iop_reg->iop_data6);

	ret = readl_poll_timeout(&p_iop_reg->iop_data7, value,
				 value == 0xaaaa, 1000, 10000);
	if (ret) {
		dev_err(dev, "timed out\n");
		return ret;
	}

	writel(0xdd, &p_iop_reg->iop_data1);//8051 bin file call Ultra low function.
	mdelay(10);
	return 0;
}

static int sp_iop_s1mode(struct device *dev, struct sp_iop *iop)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	int ret, value;

	ret = readl_poll_timeout(&p_iop_reg->iop_data2, value,
				 (value & IOP_READY) == IOP_READY, 1000, 10000);
	if (ret) {
		dev_err(dev, "timed out\n");
		return ret;
	}

	writel(RISC_READY, &p_iop_reg->iop_data2);
	writel(0x00, &p_iop_reg->iop_data5);
	writel(0x60, &p_iop_reg->iop_data6);

	ret = readl_poll_timeout(&p_iop_reg->iop_data7, value,
				 value == 0xaaaa, 1000, 10000);
	if (ret) {
		dev_err(dev, "timed out\n");
		return ret;
	}

	writel(0xee, &p_iop_reg->iop_data1);//8051 bin file call S1_mode function.
	mdelay(10);
	return 0;
}

static ssize_t sp_iop_mailbox_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sp_iop *iop = dev_get_drvdata(dev);
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	unsigned int mailbox;

	mailbox = readl(&p_iop_reg->iop_data0);
	return sysfs_emit(buf, "mailbox = 0x%x\n", mailbox);
}

static ssize_t sp_iop_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sp_iop *iop = dev_get_drvdata(dev);

	return sysfs_emit(buf, "bin code mode = 0x%x\n", iop->mode);
}

static ssize_t sp_iop_mode_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct sp_iop *iop = dev_get_drvdata(dev);

	if (sysfs_streq(buf, "0"))
		sp_iop_normal_mode(iop);
	else if (sysfs_streq(buf, "1"))
		sp_iop_standby_mode(iop);
	return count;
}

static DEVICE_ATTR_RO(sp_iop_mailbox);
static DEVICE_ATTR_RW(sp_iop_mode);

static int  sp_iop_get_normal_code(struct device *dev, struct sp_iop *iop)
{
	const struct firmware *fw;
	static const char file[] = "normal.bin";
	unsigned int err, i;

	err = request_firmware(&fw, file, dev);
	if (err) {
		dev_err(dev, "get bin file error\n");
		return err;
	}

	for (i = 0; i < NORMAL_CODE_MAX_SIZE; i++) {
		char temp;

		temp = fw->data[i];
		iop->iop_normal_code[i] = temp;
	}
	release_firmware(fw);
	return err;
}

static int  sp_iop_get_standby_code(struct device *dev, struct sp_iop *iop)
{
	const struct firmware *fw;
	static const char file[] = "standby.bin";
	unsigned int err, i;

	err = request_firmware(&fw, file, dev);
	if (err) {
		dev_err(dev, "get bin file error\n");
		return err;
	}

	for (i = 0; i < STANDBY_CODE_MAX_SIZE; i++) {
		char temp;

		temp = fw->data[i];
		iop->iop_standby_code[i] = temp;
	}
	release_firmware(fw);
	return err;
}

static int sp_iop_get_resources(struct platform_device *pdev, struct sp_iop *p_sp_iop_info)
{
	struct resource *r;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iop");
	p_sp_iop_info->iop_regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(p_sp_iop_info->iop_regs)) {
		dev_err(&pdev->dev, "ioremap fail\n");
		return PTR_ERR(p_sp_iop_info->iop_regs);
	}

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iop_pmc");
	p_sp_iop_info->pmc_regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(p_sp_iop_info->pmc_regs)) {
		dev_err(&pdev->dev, "ioremap fail\n");
		return PTR_ERR(p_sp_iop_info->pmc_regs);
	}

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "moon0");
	p_sp_iop_info->moon0_regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(p_sp_iop_info->moon0_regs)) {
		dev_err(&pdev->dev, "ioremap fail\n");
		return PTR_ERR(p_sp_iop_info->moon0_regs);
	}
	return IOP_SUCCESS;
}

static int sp_iop_platform_driver_probe(struct platform_device *pdev)
{
	int ret = -ENXIO;
	int rc;
	struct sp_iop *iop;
	struct device_node *memnp;
	struct resource mem_res;

	iop = devm_kzalloc(&pdev->dev, sizeof(struct sp_iop), GFP_KERNEL);
	if (!iop) {
		ret	= -ENOMEM;
		goto fail_kmalloc;
	}
	/* init */
	mutex_init(&iop->write_lock);
	ret = sp_iop_get_resources(pdev, iop);

	//Get reserve address
	memnp = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!memnp) {
		dev_err(&pdev->dev, "no memory-region node\n");
		return -EINVAL;
	}

	rc = of_address_to_resource(memnp, 0, &mem_res);
	of_node_put(memnp);
	if (rc) {
		dev_err(&pdev->dev, "failed to translate memory-region to a resource\n");
		return -EINVAL;
	}

	iop->iop_mem_start = mem_res.start;
	iop->iop_mem_size = resource_size(&mem_res);

	ret = sp_iop_get_normal_code(&pdev->dev, iop);
	if (ret != 0) {
		dev_err(&pdev->dev, "get normal code err=%d\n", ret);
		return ret;
	}

	ret = sp_iop_get_standby_code(&pdev->dev, iop);
	if (ret != 0) {
		dev_err(&pdev->dev, "get standby code err=%d\n", ret);
		return ret;
	}

	sp_iop_normal_mode(iop);
	platform_set_drvdata(pdev, iop);
	device_create_file(&pdev->dev, &dev_attr_sp_iop_mailbox);
	device_create_file(&pdev->dev, &dev_attr_sp_iop_mode);
	iop->gpio_wakeup = of_get_named_gpio(pdev->dev.of_node, "iop-wakeup", 0);
	return 0;

fail_kmalloc:
	return ret;
}

static void sp_iop_platform_driver_shutdown(struct platform_device *pdev)
{
	struct sp_iop *iop = platform_get_drvdata(pdev);
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	unsigned int value;

	sp_iop_standby_mode(iop);
	mdelay(10);
	value = readl(&p_iop_reg->iop_data11);
	if (value == S1)
		sp_iop_s1mode(&pdev->dev, iop);
	else
		sp_iop_s3mode(&pdev->dev, iop);
}

static const struct of_device_id sp_iop_of_match[] = {
	{ .compatible = "sunplus,sp7021-iop" },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, sp_iop_of_match);

static struct platform_driver sp_iop_platform_driver = {
	.probe		= sp_iop_platform_driver_probe,
	.shutdown	= sp_iop_platform_driver_shutdown,
	.driver = {
		.name	= "sunplus,sp7021-iop",
		.of_match_table = sp_iop_of_match,
	}
};

module_platform_driver(sp_iop_platform_driver);

MODULE_AUTHOR("Tony Huang <tonyhuang.sunplus@gmail.com>");
MODULE_DESCRIPTION("Sunplus IOP Driver");
MODULE_LICENSE("GPL v2");
