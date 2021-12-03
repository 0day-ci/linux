// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/of_platform.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include "sunplus_iop.h"

#define NORMAL_CODE_MAX_SIZE 0X1000
#define STANDBY_CODE_MAX_SIZE 0x4000
unsigned char iop_normal_code[NORMAL_CODE_MAX_SIZE];
unsigned char iop_standby_code[STANDBY_CODE_MAX_SIZE];
/* ---------------------------------------------------------------------------------------------- */
resource_size_t SP_IOP_RESERVE_BASE;
resource_size_t SP_IOP_RESERVE_SIZE;
/* ---------------------------------------------------------------------------------------------- */
struct sp_iop {
	struct miscdevice dev;			// iop device
	struct mutex write_lock;
	void __iomem *iop_regs;
	void __iomem *pmc_regs;
	void __iomem *moon0_regs;
	int irq;
};
/*****************************************************************
 *						  G L O B A L	 D A T A
 ******************************************************************/
static struct sp_iop *iop;

void iop_normal_mode(void)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	struct regs_moon0 *p_moon0_reg = (struct regs_moon0 *)iop->moon0_regs;
	void __iomem *iop_kernel_base;
	unsigned int reg;

	iop_kernel_base = ioremap(SP_IOP_RESERVE_BASE, NORMAL_CODE_MAX_SIZE);
	memset(iop_kernel_base, 0, NORMAL_CODE_MAX_SIZE);
	memcpy(iop_kernel_base, iop_normal_code, NORMAL_CODE_MAX_SIZE);

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

	reg = (SP_IOP_RESERVE_BASE & 0xFFFF);
	writel(reg, &p_iop_reg->iop_base_adr_l);
	reg	= (SP_IOP_RESERVE_BASE >> 16);
	writel(reg, &p_iop_reg->iop_base_adr_h);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x01);
	writel(reg, &p_iop_reg->iop_control);
}

void iop_standby_mode(void)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	struct regs_moon0 *p_moon0_reg = (struct regs_moon0 *)iop->moon0_regs;
	void __iomem *iop_kernel_base;
	unsigned long reg;

	iop_kernel_base = ioremap(SP_IOP_RESERVE_BASE, STANDBY_CODE_MAX_SIZE);
	memset(iop_kernel_base, 0, STANDBY_CODE_MAX_SIZE);
	memcpy(iop_kernel_base, iop_standby_code, STANDBY_CODE_MAX_SIZE);

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

	reg = (SP_IOP_RESERVE_BASE & 0xFFFF);
	writel(reg, &p_iop_reg->iop_base_adr_l);
	reg = (SP_IOP_RESERVE_BASE >> 16);
	writel(reg, &p_iop_reg->iop_base_adr_h);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x01);
	writel(reg, &p_iop_reg->iop_control);
}

void get_iop_data(struct device *dev)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	unsigned short value_0, value_1, value_2, value_3, value_4, value_5;
	unsigned short value_6, value_7, value_8, value_9, value_10, value_11;

	value_0 = readl(&p_iop_reg->iop_data0);
	value_1 = readl(&p_iop_reg->iop_data1);
	value_2 = readl(&p_iop_reg->iop_data2);
	value_3 = readl(&p_iop_reg->iop_data3);
	value_4 = readl(&p_iop_reg->iop_data4);
	value_5 = readl(&p_iop_reg->iop_data5);
	value_6 = readl(&p_iop_reg->iop_data6);
	value_7 = readl(&p_iop_reg->iop_data7);
	value_8 = readl(&p_iop_reg->iop_data8);
	value_9 = readl(&p_iop_reg->iop_data9);
	value_10 = readl(&p_iop_reg->iop_data10);
	value_11 = readl(&p_iop_reg->iop_data11);
	dev_info(dev, "%s(%d) iop_data0=%x iop_data1=%x iop_data2=%x iop_data3=%x\n",
		__func__, __LINE__, value_0, value_1, value_2, value_3);
	dev_info(dev, "%s(%d) iop_data4=%x iop_data5=%x iop_data6=%x iop_data7=%x\n",
		__func__, __LINE__, value_4, value_5, value_6, value_7);
	dev_info(dev, "%s(%d) iop_data8=%x iop_data9=%x iop_data10=%x iop_data11=%x\n",
		__func__, __LINE__, value_8, value_9, value_10, value_11);
}

#define IOP_READY	0x4
#define RISC_READY	0x8
void iop_suspend(void)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	struct regs_moon0 *p_moon0_reg = (struct regs_moon0 *)iop->moon0_regs;
	struct regs_iop_pmc *p_iop_pmc_reg = (struct regs_iop_pmc *)iop->pmc_regs;
	unsigned long reg;

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

	writel(0x00ff0085, (iop->moon0_regs + 32*4*1 + 4*1));

	reg = readl(iop->moon0_regs + 32*4*1 + 4*2);
	reg |= 0x08000800;
	writel(reg, (iop->moon0_regs + 32*4*1 + 4*2));

	reg = readl(&p_iop_reg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &p_iop_reg->iop_control);

	reg = (SP_IOP_RESERVE_BASE & 0xFFFF);
	writel(reg, &p_iop_reg->iop_base_adr_l);
	reg = (SP_IOP_RESERVE_BASE >> 16);
	writel(reg, &p_iop_reg->iop_base_adr_h);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x01);
	writel(reg, &p_iop_reg->iop_control);

	while ((p_iop_reg->iop_data2&IOP_READY) != IOP_READY)
		;

	reg = readl(&p_iop_reg->iop_data2);
	reg |= RISC_READY;
	writel(reg, &p_iop_reg->iop_control);

	writel(0x00, &p_iop_reg->iop_data5);
	writel(0x60, &p_iop_reg->iop_data6);

	while (1) {
		if (p_iop_reg->iop_data7 == 0xaaaa)
			break;
	}

	writel(0xdd, &p_iop_reg->iop_data1);//8051 bin file call Ultra low function.
}

void iop_shutdown(void)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;
	struct regs_moon0 *p_moon0_reg = (struct regs_moon0 *)iop->moon0_regs;
	struct regs_iop_pmc *p_iop_pmc_reg = (struct regs_iop_pmc *)iop->pmc_regs;
	unsigned int reg;

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

	writel(0x00ff0085, (iop->moon0_regs + 32*4*1 + 4*1));

	reg = readl(iop->moon0_regs + 32*4*1 + 4*2);
	reg |= 0x08000800;
	writel(reg, (iop->moon0_regs + 32*4*1 + 4*2));

	reg = readl(&p_iop_reg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &p_iop_reg->iop_control);

	reg = (SP_IOP_RESERVE_BASE & 0xFFFF);
	writel(reg, &p_iop_reg->iop_base_adr_l);
	reg = (SP_IOP_RESERVE_BASE >> 16);
	writel(reg, &p_iop_reg->iop_base_adr_h);

	reg = readl(&p_iop_reg->iop_control);
	reg &= ~(0x01);
	writel(reg, &p_iop_reg->iop_control);

	while ((p_iop_reg->iop_data2&IOP_READY) != IOP_READY)
		;

	writel(RISC_READY, &p_iop_reg->iop_data2);
	writel(0x00, &p_iop_reg->iop_data5);
	writel(0x60, &p_iop_reg->iop_data6);

	while (1) {
		if (p_iop_reg->iop_data7 == 0xaaaa)
			break;
	}

	writel(0xdd, &p_iop_reg->iop_data1);//8051 bin file call Ultra low function.
	mdelay(10);
}

void iop_s1mode(void)
{
	struct regs_iop *p_iop_reg = (struct regs_iop *)iop->iop_regs;

	while ((p_iop_reg->iop_data2&IOP_READY) != IOP_READY)
		;

	writel(RISC_READY, &p_iop_reg->iop_data2);
	writel(0x00, &p_iop_reg->iop_data5);
	writel(0x60, &p_iop_reg->iop_data6);

	while (1) {
		if (p_iop_reg->iop_data7 == 0xaaaa)
			break;
	}

	writel(0xee, &p_iop_reg->iop_data1);//8051 bin file call S1_mode function.
}

static int  get_normal_code(struct device *dev)
{
	const struct firmware *fw;
	static const char file[] = "normal.bin";
	unsigned int err, i;

	dev_info(dev, "normal code\n");
	err = request_firmware(&fw, file, dev);
	if (err) {
		dev_info(dev, "get bin file error\n");
		return err;
	}

	for (i = 0; i < NORMAL_CODE_MAX_SIZE; i++) {
		char temp;

		temp = fw->data[i];
		iop_normal_code[i] = temp;
	}
	release_firmware(fw);
	return err;
}

static int  get_standby_code(struct device *dev)
{
	const struct firmware *fw;
	static const char file[] = "standby.bin";
	unsigned int err, i;

	dev_info(dev, "standby code\n");
	err = request_firmware(&fw, file, dev);
	if (err) {
		dev_info(dev, "get bin file error\n");
		return err;
	}

	for (i = 0; i < STANDBY_CODE_MAX_SIZE; i++) {
		char temp;

		temp = fw->data[i];
		iop_standby_code[i] = temp;
	}
	release_firmware(fw);
	return err;
}

static int sp_iop_open(struct inode *inode, struct file *pfile)
{
	return 0;
}

static ssize_t sp_iop_read(struct file *pfile, char __user *ubuf,
			size_t length, loff_t *offset)
{
	return 0;
}

static ssize_t sp_iop_write(struct file *pfile, const char __user *ubuf,
	size_t length, loff_t *offset)
{
	return 0;
}

static int sp_iop_release(struct inode *inode, struct file *pfile)
{
	//dev_dbg(iop->dev, "Sunplus IOP module release\n");
	return 0;
}

static const struct file_operations sp_iop_fops = {
	.owner			= THIS_MODULE,
	.open			= sp_iop_open,
	.read			= sp_iop_read,
	.write			= sp_iop_write,
	.release		= sp_iop_release,
};

static int sp_iop_get_resources(struct platform_device *pdev,
	struct sp_iop *pstSpIOPInfo)
{
	struct resource *r;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iop");
	pstSpIOPInfo->iop_regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pstSpIOPInfo->iop_regs)) {
		dev_err(&pdev->dev, "ioremap fail\n");
		return PTR_ERR(pstSpIOPInfo->iop_regs);
	}

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iop_pmc");
	pstSpIOPInfo->pmc_regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pstSpIOPInfo->pmc_regs)) {
		dev_err(&pdev->dev, "ioremap fail\n");
		return PTR_ERR(pstSpIOPInfo->pmc_regs);
	}

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "moon0");
	pstSpIOPInfo->moon0_regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pstSpIOPInfo->moon0_regs)) {
		dev_err(&pdev->dev, "ioremap fail\n");
		return PTR_ERR(pstSpIOPInfo->moon0_regs);
	}
	return IOP_SUCCESS;
}

static int sp_iop_platform_driver_probe(struct platform_device *pdev)
{
	int ret = -ENXIO;
	int rc;
	struct device_node *memnp;
	struct resource mem_res;

	iop = devm_kzalloc(&pdev->dev, sizeof(struct sp_iop), GFP_KERNEL);
	if (iop == NULL) {
		ret	= -ENOMEM;
		goto fail_kmalloc;
	}
	/* init */
	mutex_init(&iop->write_lock);
	/* register device */
	iop->dev.name  = "sp_iop";
	iop->dev.minor = MISC_DYNAMIC_MINOR;
	iop->dev.fops  = &sp_iop_fops;
	ret = misc_register(&iop->dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "sp_iop device register fail\n");
		goto fail_regdev;
	}

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

	SP_IOP_RESERVE_BASE = mem_res.start;
	SP_IOP_RESERVE_SIZE = resource_size(&mem_res);

	ret = get_normal_code(&pdev->dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "get normal code err=%d\n", ret);
		return ret;
	}

	ret = get_standby_code(&pdev->dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "get standby code err=%d\n", ret);
		return ret;
	}

	iop_normal_mode();
	return 0;

fail_regdev:
	mutex_destroy(&iop->write_lock);
fail_kmalloc:
	return ret;


}

static int sp_iop_platform_driver_remove(struct platform_device *pdev)
{
	return 0;
}

static int sp_iop_platform_driver_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static void sp_iop_platform_driver_shutdown(struct platform_device *pdev)
{

}

void sp_iop_platform_driver_poweroff(void)
{
	iop_standby_mode();
	iop_shutdown();
}

static int sp_iop_platform_driver_resume(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id sp_iop_of_match[] = {
	{ .compatible = "sunplus,sp7021-iop" },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, sp_iop_of_match);

static struct platform_driver sp_iop_platform_driver = {
	.probe		= sp_iop_platform_driver_probe,
	.remove		= sp_iop_platform_driver_remove,
	.suspend	= sp_iop_platform_driver_suspend,
	.shutdown	= sp_iop_platform_driver_shutdown,
	.resume		= sp_iop_platform_driver_resume,
	.driver = {
		.name	= "sunplus,sp7021-iop",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sp_iop_of_match),
	}
};

module_platform_driver(sp_iop_platform_driver);

MODULE_AUTHOR("Tony Huang <tonyhuang.sunplus@gmail.com>");
MODULE_DESCRIPTION("Sunplus IOP Driver");
MODULE_LICENSE("GPL v2");
