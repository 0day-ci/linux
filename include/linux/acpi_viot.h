/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ACPI_VIOT_H__
#define __ACPI_VIOT_H__

#include <linux/acpi.h>

#ifdef CONFIG_ACPI_VIOT
void __init acpi_viot_init(void);
int acpi_viot_dma_setup(struct device *dev, enum dev_dma_attr attr);
int acpi_viot_set_iommu_ops(struct device *dev, struct iommu_ops *ops);
#else
static inline void acpi_viot_init(void) {}
static inline int acpi_viot_dma_setup(struct device *dev,
				      enum dev_dma_attr attr)
{
	return 0;
}
static inline int acpi_viot_set_iommu_ops(struct device *dev,
					  struct iommu_ops *ops)
{
	return -ENODEV;
}
#endif

#endif /* __ACPI_VIOT_H__ */
