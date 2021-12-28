// SPDX-License-Identifier: GPL-2.0
/*
 * nvmem cell transformations
 */

#include <linux/etherdevice.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>

struct nvmem_transformations {
	const char *compatible;
	nvmem_cell_post_process_t pp;
};

static const struct nvmem_transformations nvmem_transformations[] = {
	{}
};

nvmem_cell_post_process_t nvmem_get_transformations(struct device_node *np)
{
	const struct nvmem_transformations *transform = nvmem_transformations;

	for (; transform->compatible; transform++)
		if (of_device_is_compatible(np, transform->compatible))
			return transform->pp;

	return NULL;
}
