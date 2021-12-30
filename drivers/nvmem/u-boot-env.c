// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/crc32.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

enum u_boot_env_format {
	U_BOOT_FORMAT_DEFAULT,
	U_BOOT_FORMAT_BRCM,
};

struct u_boot_env {
	struct device *dev;
	enum u_boot_env_format format;
	struct mtd_info *mtd;
	size_t offset;
	size_t size;
	struct nvmem_cell_info *cells;
	int ncells;
};

struct u_boot_env_image {
	__le32 crc32;
	uint8_t data[0];
} __packed;

struct u_boot_brcm_header {
	__le32 unk;
	__le32 len;
} __packed;

static int u_boot_env_read(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct u_boot_env *priv = context;
	struct device *dev = priv->dev;
	size_t bytes_read;
	int err;

	err = mtd_read(priv->mtd, priv->offset + offset, bytes, &bytes_read, val);
	if (err && !mtd_is_bitflip(err)) {
		dev_err(dev, "Failed to read from mtd: %d\n", err);
		return err;
	}

	if (bytes_read != bytes) {
		dev_err(dev, "Failed to read %zd bytes\n", bytes);
		return err;
	}

	return 0;
}

static int u_boot_env_add_cells(struct u_boot_env *priv, size_t data_offset,
				uint8_t *data, size_t len)
{
	struct device *dev = priv->dev;
	char *var, *value, *eq;
	int idx;

	priv->ncells = 0;
	for (var = data; var < (char *)data + len && *var; var += strlen(var) + 1)
		priv->ncells++;

	priv->cells = devm_kcalloc(dev, priv->ncells, sizeof(*priv->cells), GFP_KERNEL);
	if (!priv->cells)
		return -ENOMEM;

	for (var = data, idx = 0;
	     var < (char *)data + len && *var;
	     var = value + strlen(value) + 1, idx++) {
		eq = strchr(var, '=');
		if (!eq)
			break;
		*eq = '\0';
		value = eq + 1;

		priv->cells[idx].name = devm_kstrdup(dev, var, GFP_KERNEL);
		if (!priv->cells[idx].name)
			return -ENOMEM;
		priv->cells[idx].offset = data_offset + value - (char *)data;
		priv->cells[idx].bytes = strlen(value);
	}

	if (WARN_ON(idx != priv->ncells))
		priv->ncells = idx;

	return 0;
}

static int u_boot_env_parse(struct u_boot_env *priv)
{
	struct device *dev = priv->dev;
	struct u_boot_env_image *image;
	size_t image_offset;
	size_t image_len;
	uint32_t crc32;
	size_t bytes;
	uint8_t *buf;
	int err;

	image_offset = 0;
	image_len = priv->size;
	if (priv->format == U_BOOT_FORMAT_BRCM) {
		struct u_boot_brcm_header header;

		err = mtd_read(priv->mtd, priv->offset, sizeof(header), &bytes,
			       (uint8_t *)&header);
		if (err && !mtd_is_bitflip(err)) {
			dev_err(dev, "Failed to read from mtd: %d\n", err);
			return err;
		}

		image_offset = sizeof(header);
		image_len = le32_to_cpu(header.len);
	}

	buf = kcalloc(1, image_len, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto err_out;
	}
	image = (struct u_boot_env_image *)buf;

	err = mtd_read(priv->mtd, priv->offset + image_offset, image_len, &bytes, buf);
	if (err && !mtd_is_bitflip(err)) {
		dev_err(dev, "Failed to read from mtd: %d\n", err);
		goto err_kfree;
	}

	crc32 = crc32(~0, buf + 4, image_len - 4) ^ ~0L;
	if (crc32 != le32_to_cpu(image->crc32)) {
		dev_err(dev, "Invalid calculated CRC32: 0x%08x\n", crc32);
		err = -EINVAL;
		goto err_kfree;
	}

	buf[image_len - 1] = '\0';
	err = u_boot_env_add_cells(priv, image_offset + sizeof(*image),
				   buf + sizeof(*image),
				   image_len - sizeof(*image));
	if (err)
		dev_err(dev, "Failed to add cells: %d\n", err);

err_kfree:
	kfree(buf);
err_out:
	return err;
}

static const struct of_device_id u_boot_env_of_match_table[] = {
	{ .compatible = "u-boot,env", .data = (void *)U_BOOT_FORMAT_DEFAULT, },
	{ .compatible = "brcm,env", .data = (void *)U_BOOT_FORMAT_BRCM, },
	{},
};

static int u_boot_env_probe(struct platform_device *pdev)
{
	struct nvmem_config config = {
		.name = "u-boot-env",
		.reg_read = u_boot_env_read,
	};
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct of_device_id *of_id;
	struct u_boot_env *priv;
	const char *label;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	of_id = of_match_device(u_boot_env_of_match_table, dev);
	if (!of_id)
		return -EINVAL;
	priv->format = (uintptr_t)of_id->data;

	if (of_property_read_u32(np, "reg", (u32 *)&priv->offset) ||
	    of_property_read_u32_index(np, "reg", 1, (u32 *)&priv->size)) {
		dev_err(dev, "Failed to read \"reg\" property\n");
		return -EINVAL;
	}

	label = of_get_property(np->parent, "label", NULL);
	if (!label)
		label = np->parent->name;

	priv->mtd = get_mtd_device_nm(label);
	if (IS_ERR(priv->mtd)) {
		dev_err(dev, "Failed to find \"%s\" MTD device: %ld\n", label, PTR_ERR(priv->mtd));
		return PTR_ERR(priv->mtd);
	}

	err = u_boot_env_parse(priv);
	if (err)
		return err;

	config.dev = dev;
	config.cells = priv->cells;
	config.ncells = priv->ncells;
	config.priv = priv;
	config.size = priv->size;

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &config));
}

static struct platform_driver u_boot_env_driver = {
	.probe = u_boot_env_probe,
	.driver = {
		.name = "u_boot_env",
		.of_match_table = u_boot_env_of_match_table,
	},
};

static int __init u_boot_env_init(void)
{
	return platform_driver_register(&u_boot_env_driver);
}

subsys_initcall_sync(u_boot_env_init);

MODULE_AUTHOR("Rafał Miłecki");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, u_boot_env_of_match_table);
