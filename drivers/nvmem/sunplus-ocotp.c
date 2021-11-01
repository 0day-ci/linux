// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "sunplus-ocotp.h"

enum base_type {
	HB_GPIO,
	OTPRX,
	BASEMAX,
};

struct sp_otp_data_t {
	struct device *dev;
	void __iomem *base[BASEMAX];
	struct clk *clk;
	struct nvmem_config *config;
};

static int sp_otp_wait(void __iomem *_base)
{
	struct sp_otprx_reg *otprx_reg_ptr = (struct sp_otprx_reg *)(_base);
	int timeout = OTP_READ_TIMEOUT;
	unsigned int status;

	do {
		udelay(10);
		if (timeout-- == 0)
			return -ETIMEDOUT;

		status = readl(&otprx_reg_ptr->otp_cmd_status);
	} while ((status & OTP_READ_DONE) != OTP_READ_DONE);

	return 0;
}

int sp_otp_read_real(struct sp_otp_data_t *_otp, int addr, char *value)
{
	struct sp_hb_gpio_reg *hb_gpio_reg_ptr;
	struct sp_otprx_reg *otprx_reg_ptr;
	unsigned int addr_data;
	unsigned int byte_shift;
	int ret = 0;

	hb_gpio_reg_ptr = (struct sp_hb_gpio_reg *)(_otp->base[HB_GPIO]);
	otprx_reg_ptr = (struct sp_otprx_reg *)(_otp->base[OTPRX]);

	addr_data = addr % (OTP_WORD_SIZE * OTP_WORDS_PER_BANK);
	addr_data = addr_data / OTP_WORD_SIZE;

	byte_shift = addr % (OTP_WORD_SIZE * OTP_WORDS_PER_BANK);
	byte_shift = byte_shift % OTP_WORD_SIZE;

	addr = addr / (OTP_WORD_SIZE * OTP_WORDS_PER_BANK);
	addr = addr * OTP_BIT_ADDR_OF_BANK;

	writel(0x0, &otprx_reg_ptr->otp_cmd_status);
	writel(addr, &otprx_reg_ptr->otp_addr);
	writel(0x1E04, &otprx_reg_ptr->otp_cmd);

	ret = sp_otp_wait(_otp->base[OTPRX]);
	if (ret < 0)
		return ret;

	*value = (readl(&hb_gpio_reg_ptr->hb_gpio_rgst_bus32_9 +
				addr_data) >> (8 * byte_shift)) & 0xFF;

	return ret;
}

static int sp_ocotp_read(void *_c, unsigned int _off, void *_v, size_t _l)
{
	struct sp_otp_data_t *otp = _c;
	unsigned int addr;
	char *buf = _v;
	char value[4];
	int ret;

	dev_dbg(otp->dev, "OTP read %u bytes at %u", _l, _off);

	if ((_off >= QAC628_OTP_SIZE) || (_l == 0) || ((_off + _l) > QAC628_OTP_SIZE))
		return -EINVAL;

	ret = clk_enable(otp->clk);
	if (ret)
		return ret;

	*buf = 0;
	for (addr = _off; addr < (_off + _l); addr++) {
		ret = sp_otp_read_real(otp, addr, value);
		if (ret < 0) {
			dev_err(otp->dev, "OTP read fail:%d at %d", ret, addr);
			goto disable_clk;
		}

		*buf++ = *value;
	}

disable_clk:
	clk_disable(otp->clk);
	dev_dbg(otp->dev, "OTP read complete");

	return ret;
}

static struct nvmem_config sp_ocotp_nvmem_config = {
	.name = "sp-ocotp",
	.read_only = true,
	.word_size = 1,
	.size = QAC628_OTP_SIZE,
	.stride = 1,
	.reg_read = sp_ocotp_read,
	.owner = THIS_MODULE,
};

int sp_ocotp_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct sp_otp_vX_t *sp_otp_vX = NULL;
	struct device *dev = &(pdev->dev);
	struct nvmem_device *nvmem;
	struct sp_otp_data_t *otp;
	struct resource *res;
	int ret;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (match && match->data) {
		sp_otp_vX = match->data;
		// may be used to choose the parameters
	} else {
		dev_err(dev, "OTP vX does not match");
	}

	otp = devm_kzalloc(dev, sizeof(*otp), GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	otp->dev = dev;
	otp->config = &sp_ocotp_nvmem_config;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hb_gpio");
	otp->base[HB_GPIO] = devm_ioremap_resource(dev, res);
	if (IS_ERR(otp->base[HB_GPIO]))
		return PTR_ERR(otp->base[HB_GPIO]);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "otprx");
	otp->base[OTPRX] = devm_ioremap_resource(dev, res);
	if (IS_ERR(otp->base[OTPRX]))
		return PTR_ERR(otp->base[OTPRX]);

	otp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(otp->clk))
		return PTR_ERR(otp->clk);

	ret = clk_prepare(otp->clk);
	if (ret < 0) {
		dev_err(dev, "failed to prepare clk: %d\n", ret);
		return ret;
	}
	clk_enable(otp->clk);

	sp_ocotp_nvmem_config.priv = otp;
	sp_ocotp_nvmem_config.dev = dev;

	// devm_* >= 4.15 kernel
	// nvmem = devm_nvmem_register(dev, &sp_ocotp_nvmem_config);

	nvmem = nvmem_register(&sp_ocotp_nvmem_config);

	if (IS_ERR(nvmem)) {
		dev_err(dev, "error registering nvmem config\n");
		return PTR_ERR(nvmem);
	}

	platform_set_drvdata(pdev, nvmem);

	dev_dbg(dev, "clk:%ld banks:%d x wpb:%d x wsize:%d = %d",
		clk_get_rate(otp->clk),
		QAC628_OTP_NUM_BANKS, OTP_WORDS_PER_BANK,
		OTP_WORD_SIZE, QAC628_OTP_SIZE);

	dev_info(dev, "by Sunplus (C) 2020");

	return 0;
}
EXPORT_SYMBOL_GPL(sp_ocotp_probe);

int sp_ocotp_remove(struct platform_device *pdev)
{
	// disable for devm_*
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	nvmem_unregister(nvmem);
	return 0;
}
EXPORT_SYMBOL_GPL(sp_ocotp_remove);

