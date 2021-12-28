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

/**
 * nvmem_transform_mac_address_offset() - Add an offset to a mac address cell
 *
 * A simple transformation which treats the index argument as an offset and add
 * it to a mac address. This is useful, if the nvmem cell stores a base
 * ethernet address.
 *
 * @index: nvmem cell index
 * @data: nvmem data
 * @bytes: length of the data
 *
 * Return: 0 or negative error code on failure.
 */
static int nvmem_transform_mac_address_offset(int index, unsigned int offset,
					      void *data, size_t bytes)
{
	if (bytes != ETH_ALEN)
		return -EINVAL;

	if (index < 0)
		return -EINVAL;

	if (!is_valid_ether_addr(data))
		return -EINVAL;

	eth_addr_add(data, index);

	return 0;
}

static int nvmem_kontron_sl28_vpd_pp(void *priv, const char *id, int index,
				     unsigned int offset, void *data,
				     size_t bytes)
{
	if (!id)
		return 0;

	if (!strcmp(id, "mac-address"))
		return nvmem_transform_mac_address_offset(index, offset, data,
							  bytes);

	return 0;
}

static const struct nvmem_transformations nvmem_transformations[] = {
	{ .compatible = "kontron,sl28-vpd", .pp = nvmem_kontron_sl28_vpd_pp },
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
