// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Axis Communications AB
 */

#include <linux/of.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

#define SIZE_OF_MEM (64)

struct nvmem_mockup {
	struct device *dev;
	u8 mem[SIZE_OF_MEM];
};

static int nvmem_mockup_read(void *context, unsigned int offset,
			     void *val, size_t bytes)
{
	struct nvmem_mockup *priv = context;

	if (bytes + offset > SIZE_OF_MEM || bytes + offset < offset)
		return -EINVAL;

	memcpy(val, &priv->mem[offset], bytes);

	return 0;
}

static int nvmem_mockup_write(void *context, unsigned int offset,
			      void *val, size_t bytes)
{
	struct nvmem_mockup *priv = context;

	if (bytes + offset > SIZE_OF_MEM || bytes + offset < offset)
		return -EINVAL;

	memcpy(&priv->mem[offset], val, bytes);

	return 0;
}

static int nvmem_mockup_probe(struct platform_device *pdev)
{
	struct nvmem_config config = { };
	struct device *dev = &pdev->dev;
	struct nvmem_mockup *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	config.dev = dev;
	config.priv = priv;
	config.name = "nvmem-mockup";
	config.size = SIZE_OF_MEM;
	config.reg_read = nvmem_mockup_read;
	config.reg_write = nvmem_mockup_write;

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &config));
}

static const struct of_device_id nvmem_mockup_match[] = {
	{ .compatible = "nvmem-mockup", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, nvmem_mockup_match);

static struct platform_driver nvmem_mockup_driver = {
	.probe = nvmem_mockup_probe,
	.driver = {
		.name = "nvmem-mockup",
		.of_match_table = nvmem_mockup_match,
	},
};
module_platform_driver(nvmem_mockup_driver);

MODULE_LICENSE("GPL v2");
