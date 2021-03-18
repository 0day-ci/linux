// SPDX-License-Identifier: GPL-2.0

#include <linux/mtd/spi-nor.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/sysfs.h>

#include "core.h"

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_mem *spimem = spi_get_drvdata(spi);
	struct spi_nor *nor = spi_mem_get_drvdata(spimem);

	return sprintf(buf, "%s\n", nor->info->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t jedec_id_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_mem *spimem = spi_get_drvdata(spi);
	struct spi_nor *nor = spi_mem_get_drvdata(spimem);

	return sprintf(buf, "%*phN\n", nor->info->id_len, nor->info->id);
}
static DEVICE_ATTR_RO(jedec_id);

static struct attribute *spi_nor_sysfs_entries[] = {
	&dev_attr_name.attr,
	&dev_attr_jedec_id.attr,
	NULL
};

static ssize_t sfdp_read(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *bin_attr, char *buf,
			 loff_t off, size_t count)
{
	struct spi_device *spi = to_spi_device(kobj_to_dev(kobj));
	struct spi_mem *spimem = spi_get_drvdata(spi);
	struct spi_nor *nor = spi_mem_get_drvdata(spimem);
	struct sfdp *sfdp = nor->sfdp;
	size_t sfdp_size = sfdp->num_dwords * sizeof(*sfdp->dwords);

	return memory_read_from_buffer(buf, count, &off, nor->sfdp->dwords,
				       sfdp_size);
}
static BIN_ATTR_RO(sfdp, PAGE_SIZE);

static struct bin_attribute *spi_nor_sysfs_bin_entries[] = {
	&bin_attr_sfdp,
	NULL
};

static umode_t spi_nor_sysfs_is_bin_visible(struct kobject *kobj,
					    struct bin_attribute *attr, int n)
{
	struct spi_device *spi = to_spi_device(kobj_to_dev(kobj));
	struct spi_mem *spimem = spi_get_drvdata(spi);
	struct spi_nor *nor = spi_mem_get_drvdata(spimem);

	if (attr == &bin_attr_sfdp && nor->sfdp)
		return 0444;

	return 0;
}

static struct attribute_group spi_nor_sysfs_attr_group = {
	.name		= NULL,
	.is_bin_visible	= spi_nor_sysfs_is_bin_visible,
	.attrs		= spi_nor_sysfs_entries,
	.bin_attrs	= spi_nor_sysfs_bin_entries,
};

int spi_nor_sysfs_create(struct spi_nor *nor)
{
	return sysfs_create_group(&nor->dev->kobj, &spi_nor_sysfs_attr_group);
}

void spi_nor_sysfs_remove(struct spi_nor *nor)
{
	sysfs_remove_group(&nor->dev->kobj, &spi_nor_sysfs_attr_group);
}
