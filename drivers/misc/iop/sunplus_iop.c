// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/firmware.h>
#include <asm/irq.h>
#include <linux/sysfs.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include "hal_iop.h"
#include "sunplus_iop.h"

/* ---------------------------------------------------------------------------------------------- */
//#define IOP_KDBG_INFO
#define IOP_FUNC_DEBUG
#define IOP_KDBG_ERR

int IOP_GPIO;
unsigned long SP_IOP_RESERVE_BASE;
unsigned long SP_IOP_RESERVE_SIZE;
#ifdef IOP_KDBG_INFO
#define FUNC_DEBUG()	pr_info("K_IOP: %s(%d)\n", __func__, __LINE__)
#else
#define FUNC_DEBUG()
#endif

#ifdef IOP_FUNC_DEBUG
#define DBG_INFO(fmt, args ...)	pr_info("K_IOP: " fmt, ## args)
#else
#define DBG_INFO(fmt, args ...)
#endif

#ifdef IOP_KDBG_ERR
#define DBG_ERR(fmt, args ...)	pr_err("K_IOP: " fmt, ## args)
#else
#define DBG_ERR(fmt, args ...)
#endif
/* ---------------------------------------------------------------------------------------------- */
#define IOP_REG_NAME		"iop"
#define IOP_PMC_REG_NAME	   "iop_pmc"

#define DEVICE_NAME			"sunplus,sp7021-iop"

struct sp_iop_t {
	struct miscdevice dev;			// iop device
	struct mutex write_lock;
	void __iomem *iop_regs;
	void __iomem *moon0_regs;
	void __iomem *qctl_regs;
	void __iomem *pmc_regs;
	void __iomem *rtc_regs;
	int irq;
};

/*****************************************************************
 *						  G L O B A L	 D A T A
 ******************************************************************/
static struct sp_iop_t *iop;
bool iop_code_mode;//0:normal code, 1:standby code
bool iop_wake_in;//0:wake_in disable , 1:wake_in enable
unsigned int RECEIVE_CODE_SIZE;
unsigned char NormalCode[NORMAL_CODE_MAX_SIZE];
unsigned char StandbyCode[STANDBY_CODE_MAX_SIZE];
unsigned char GetCodeFromDram[STANDBY_CODE_MAX_SIZE];

static ssize_t normalcode_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t offset, size_t count)
{
	unsigned long *IOP_base_for_normal = (unsigned long *)SP_IOP_RESERVE_BASE;
	unsigned char *IOP_kernel_base;

	IOP_kernel_base = (unsigned char *)ioremap((unsigned long)IOP_base_for_normal,
		NORMAL_CODE_MAX_SIZE);
	memcpy(buf, (unsigned char *)IOP_kernel_base+offset, count);

	if (offset == (NORMAL_CODE_MAX_SIZE - count))
		DBG_INFO("get_standbycode\n");
	return count;
}

static ssize_t normalcode_write(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t offset, size_t count)
{
	int i = 0;

	if (offset != RECEIVE_CODE_SIZE) {
		DBG_INFO("Code size is incorrect\n");
		RECEIVE_CODE_SIZE = 0;
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		char temp;

		temp = buf[i];
		NormalCode[RECEIVE_CODE_SIZE] = temp;
		RECEIVE_CODE_SIZE += 1;
	}

	if (RECEIVE_CODE_SIZE == NORMAL_CODE_MAX_SIZE) {
		DBG_INFO("source code size=%x\n", RECEIVE_CODE_SIZE);
		hal_iop_load_normal_code(iop->iop_regs);
		hal_iop_get_iop_data(iop->iop_regs);
		DBG_INFO("Update normal code 64K\n");
		RECEIVE_CODE_SIZE = 0;
	}
	return count;
}

static ssize_t standbycode_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t offset, size_t count)
{
	unsigned long *IOP_base_for_normal = (unsigned long *)SP_IOP_RESERVE_BASE;
	unsigned char *IOP_kernel_base;

	IOP_kernel_base = (unsigned char *)ioremap((unsigned long)IOP_base_for_normal,
		STANDBY_CODE_MAX_SIZE);
	memcpy(buf, (unsigned char *)IOP_kernel_base+offset, count);
	//DBG_INFO("filp->f_pos=%llx\n", filp->f_pos);
	if (offset == (STANDBY_CODE_MAX_SIZE - count))
		DBG_INFO("get_standbycode\n");
	return count;
}

static ssize_t standbycode_write(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t offset, size_t count)
{
	int i = 0;

	if (offset != RECEIVE_CODE_SIZE) {
		DBG_INFO("Code size is incorrect\n");
		RECEIVE_CODE_SIZE = 0;
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		char temp;

		temp = buf[i];
		StandbyCode[RECEIVE_CODE_SIZE] = temp;
		RECEIVE_CODE_SIZE += 1;
	}

	if (RECEIVE_CODE_SIZE == STANDBY_CODE_MAX_SIZE) {
		DBG_INFO("source code size=%x\n", RECEIVE_CODE_SIZE);
		hal_iop_load_standby_code(iop->iop_regs);
		hal_iop_get_iop_data(iop->iop_regs);
		DBG_INFO("Update standby code 16K\n");
		RECEIVE_CODE_SIZE = 0;
	}
	return count;
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t len = 0;

	if (iop_code_mode == 0)
		DBG_INFO("iop_normal_mode\n");
	else if (iop_code_mode == 1)
		DBG_INFO("iop_standby_mode\n");
	return len;
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	unsigned char ret = count;

	if (buf[0] == '0') {
		hal_iop_normalmode(iop->iop_regs);
		hal_iop_get_iop_data(iop->iop_regs);
		DBG_INFO("Switch to normal mode.\n");
	} else if (buf[0] == '1') {
		hal_iop_standbymode(iop->iop_regs);
		hal_iop_get_iop_data(iop->iop_regs);
		DBG_INFO("Switch to standby mode.\n");
	} else {
		DBG_INFO("echo 0 or 1 mode\n");
		DBG_INFO("0:normal mode\n");
		DBG_INFO("cat 0 or 1 mode\n");
		DBG_INFO("0:normal mode\n");
		DBG_INFO("1:standby mode\n");
	}
	return ret;
}

static ssize_t wakein_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t len = 0;

	if (iop_wake_in == 1)
		DBG_INFO("WAKE_IN is enabled\n");
	else
		DBG_INFO("WAKE_IN is disabled\n");
	return len;
}

static ssize_t wakein_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	unsigned char ret = count;
	unsigned int reg;

	if (buf[0] == '0') {
		DBG_INFO("Disable WAKE_IN\n");
		reg = readl((void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*2));
		reg = 0x08000000;
		writel(reg, (void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*2));
		iop_wake_in = 0;
	} else if (buf[0] == '1') {
		DBG_INFO("Enable WAKE_IN\n");
		reg = readl((void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*2));
		reg |= 0x08000800;
		writel(reg, (void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*2));
		iop_wake_in = 1;
	} else {
		DBG_INFO("echo 0 or 1 mode\n");
		DBG_INFO("0:Disable WAKE_IN\n");
		DBG_INFO("1:Enable WAKE_IN\n");
	}
	return ret;
}

static ssize_t getdata_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t len = 0;

	DBG_INFO("iop_show_getdata\n");
	hal_iop_get_iop_data(iop->iop_regs);
	return len;
}

static ssize_t getdata_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret = count;

	DBG_INFO("iop_store_getdata\n");
	return ret;
}

static ssize_t setdata_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t len = 0;

	DBG_INFO("iop_show_setdata\n");
	return len;
}

static ssize_t setdata_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	unsigned char num[1], value[4];
	unsigned char ret = count;
	unsigned int i, setnum, setvalue;
	unsigned long val;
	ssize_t status;

	num[0] = buf[0];
	for (i = 0; i < 4; i++)
		value[i] = buf[2+i];

	status = kstrtoul(value, 16, &val);
	if (status)
		return status;

	setnum = (unsigned int)num[0];
	setvalue = val;
	DBG_INFO("setnum=%x\n", setnum);
	DBG_INFO("setvalue=%x\n", setvalue);
	hal_iop_set_iop_data(iop->iop_regs, setnum, setvalue);
	hal_iop_get_iop_data(iop->iop_regs);
	return ret;
}

static ssize_t setgpio_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t len = 0;

	DBG_INFO("iop_store_setgpio\n");
	return len;
}

static ssize_t setgpio_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret = count;
	unsigned char num[1];
	unsigned int setnum;
	unsigned long val;
	ssize_t status;

	DBG_INFO("iop_store_setgpio\n");
	num[0] = buf[0];
	status = kstrtoul(buf, 16, &val);
	if (status)
		return status;
	setnum = val;
	DBG_INFO("set gpio number = %x\n", IOP_GPIO);
	hal_gpio_init(iop->iop_regs, IOP_GPIO);
	return ret;
}

static ssize_t S1mode_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t len = 0;

	hal_iop_standbymode(iop->iop_regs);
	mdelay(10);//Need time to update iop_data
	hal_iop_S1mode(iop->iop_regs);
	return len;
}

static ssize_t S1mode_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	ssize_t len = 0;

	return len;
}

static DEVICE_ATTR_RW(mode);
static DEVICE_ATTR_RW(wakein);
static DEVICE_ATTR_RW(getdata);
static DEVICE_ATTR_RW(setdata);
static DEVICE_ATTR_RW(setgpio);
static DEVICE_ATTR_RW(S1mode);
static BIN_ATTR_RW(normalcode, 0x10000);
static BIN_ATTR_RW(standbycode, 0x4000);

static struct bin_attribute *iop_bin_attrs[] = {
	&bin_attr_standbycode,
	&bin_attr_normalcode,
	NULL,
};

static struct attribute_group iop_attribute_group = {
	.bin_attrs = iop_bin_attrs,
};

static int sp_iop_open(struct inode *inode, struct file *pfile)
{
	DBG_INFO("Sunplus IOP module open\n");
	return 0;
}

static ssize_t sp_iop_read(struct file *pfile, char __user *ubuf,
			size_t length, loff_t *offset)
{
	DBG_INFO("Sunplus IOP module read\n");
	return 0;
}

static ssize_t sp_iop_write(struct file *pfile, const char __user *ubuf,
	size_t length, loff_t *offset)
{

	unsigned char num[3];
	char *tmp;
	unsigned int setnum, setvalue;

	tmp = memdup_user(ubuf, length);
	num[0] = tmp[0];
	num[1] = tmp[1];
	num[2] = tmp[2];

	setnum = (unsigned int)num[0];
	setvalue = ((num[1]<<8)|num[2]);
	DBG_INFO("setnum=%x\n", setnum);
	DBG_INFO("setvalue=%x\n", setvalue);
	hal_iop_set_iop_data(iop->iop_regs, setnum, setvalue);
	DBG_INFO("Sunplus IOP module write\n");
	return 0;
}

static int sp_iop_release(struct inode *inode, struct file *pfile)
{
	DBG_INFO("Sunplus IOP module release\n");
	return 0;
}

static const struct file_operations sp_iop_fops = {
	.owner			= THIS_MODULE,
	.open			= sp_iop_open,
	.read			= sp_iop_read,
	.write			= sp_iop_write,
	.release		= sp_iop_release,
};

static int _sp_iop_get_register_base(struct platform_device *pdev,
	unsigned long *membase, const char *res_name)
{
	struct resource *r;
	void __iomem *p;

	FUNC_DEBUG();
	//DBG_INFO("register name  : %s!!\n", res_name);

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, res_name);
	if (r == NULL) {
		DBG_INFO("platform_get_resource_byname fail\n");
		return -ENODEV;
	}

	p = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(p)) {
		DBG_ERR("ioremap fail\n");
		return PTR_ERR(p);
	}

	*membase = (unsigned long)p;

	return IOP_SUCCESS;
}

static int _sp_iop_get_resources(struct platform_device *pdev,
	struct sp_iop_t *pstSpIOPInfo)
{
	int ret;
	unsigned long membase = 0;

	FUNC_DEBUG();

	ret = _sp_iop_get_register_base(pdev, &membase, IOP_REG_NAME);
	if (ret) {
		DBG_ERR("%s (%d) ret = %d\n", __func__, __LINE__, ret);
		return ret;
	}
	pstSpIOPInfo->iop_regs = (void __iomem *)membase;

	ret = _sp_iop_get_register_base(pdev, &membase, IOP_PMC_REG_NAME);
	if (ret) {
		DBG_ERR("%s (%d) ret = %d\n", __func__, __LINE__, ret);
		return ret;
	}
	pstSpIOPInfo->pmc_regs = (void __iomem *)membase;

	return IOP_SUCCESS;
}

static int sp_iop_start(struct sp_iop_t *iopbase)
{
	FUNC_DEBUG();
	hal_iop_init(iopbase->iop_regs);
	return IOP_SUCCESS;
}

static int sp_iop_shutdown(struct sp_iop_t *iopbase)
{
	FUNC_DEBUG();
	hal_iop_shutdown(iopbase->iop_regs, iopbase->pmc_regs);
	return IOP_SUCCESS;
}

static int sp_iop_platform_driver_probe(struct platform_device *pdev)
{
	int ret = -ENXIO;
	int rc;
	struct device_node *memnp;
	struct resource mem_res;

	FUNC_DEBUG();
	iop = devm_kzalloc(&pdev->dev, sizeof(struct sp_iop_t), GFP_KERNEL);
	if (iop == NULL) {
		DBG_INFO("sp_iop_t malloc fail\n");
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
		DBG_INFO("sp_iop device register fail\n");
		goto fail_regdev;
	}

	ret = _sp_iop_get_resources(pdev, iop);

	//Get reserve address
	memnp = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!memnp) {
		DBG_ERR("no memory-region node\n");
		return -EINVAL;
	}

	rc = of_address_to_resource(memnp, 0, &mem_res);
	of_node_put(memnp);
	if (rc) {
		DBG_ERR("failed to translate memory-region to a resource\n");
		return -EINVAL;
	}

	SP_IOP_RESERVE_BASE = mem_res.start;
	SP_IOP_RESERVE_SIZE = resource_size(&mem_res);

	ret = sp_iop_start(iop);
	if (ret != 0) {
		DBG_ERR("sp iop init err=%d\n", ret);
		return ret;
	}

	device_create_file(&pdev->dev, &dev_attr_mode);
	device_create_file(&pdev->dev, &dev_attr_wakein);
	device_create_file(&pdev->dev, &dev_attr_getdata);
	device_create_file(&pdev->dev, &dev_attr_setdata);
	device_create_file(&pdev->dev, &dev_attr_setgpio);
	device_create_file(&pdev->dev, &dev_attr_S1mode);

	rc = sysfs_create_group(&pdev->dev.kobj, &iop_attribute_group);
	if (rc)
		dev_err(&pdev->dev, "Error creating sysfs files!\n");

	return 0;

fail_regdev:
		mutex_destroy(&iop->write_lock);
fail_kmalloc:
	return ret;


}

static int sp_iop_platform_driver_remove(struct platform_device *pdev)
{
	FUNC_DEBUG();
	gpio_free(IOP_GPIO);
	return 0;
}

static int sp_iop_platform_driver_suspend(struct platform_device *pdev, pm_message_t state)
{
	FUNC_DEBUG();
	return 0;
}

static void sp_iop_platform_driver_shutdown(struct platform_device *pdev)
{
	FUNC_DEBUG();
}

void sp_iop_platform_driver_poweroff(void)
{
	int ret = 0;

	hal_iop_standbymode(iop->iop_regs);
	FUNC_DEBUG();
	ret = sp_iop_shutdown(iop);
	if (ret != 0)
		DBG_ERR("[IOP] sp suspend init err=%d\n", ret);
}

static int sp_iop_platform_driver_resume(struct platform_device *pdev)
{
	FUNC_DEBUG();
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
		.name	= DEVICE_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sp_iop_of_match),
	}
};

module_platform_driver(sp_iop_platform_driver);

MODULE_AUTHOR("Tony Huang <tony.huang@sunplus.com>");
MODULE_DESCRIPTION("Sunplus IOP Driver");
MODULE_LICENSE("GPL v2");
