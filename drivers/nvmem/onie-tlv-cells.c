// SPDX-License-Identifier: GPL-2.0+
/*
 * ONIE NVMEM cells provider
 *
 * Author: Vadym Kochan <vadym.kochan@plvision.eu>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/slab.h>

#define ONIE_NVMEM_TLV_MAX_LEN	2048

#define ONIE_NVMEM_HDR_ID	"TlvInfo"

struct onie_tlv_hdr {
	u8 id[8];
	u8 version;
	__be16 data_len;
} __packed;

struct onie_tlv {
	u8 type;
	u8 len;
	u8 val[0];
} __packed;

struct onie_nvmem_attr {
	struct list_head head;
	const char *name;
	unsigned int offset;
	unsigned int len;
};

struct onie_tlv_parser {
	unsigned int attr_count;
	struct list_head attrs;
	struct device *dev;

	struct nvmem_cell_lookup *lookup;
	int			nlookups;
};

static struct nvmem_parser *nvmem_parser;

static bool onie_nvmem_hdr_is_valid(struct onie_tlv_hdr *hdr)
{
	if (memcmp(hdr->id, ONIE_NVMEM_HDR_ID, sizeof(hdr->id)) != 0)
		return false;
	if (hdr->version != 0x1)
		return false;

	return true;
}

static void onie_nvmem_attrs_free(struct onie_tlv_parser *parser)
{
	struct onie_nvmem_attr *attr, *tmp;

	list_for_each_entry_safe(attr, tmp, &parser->attrs, head) {
		list_del(&attr->head);
		kfree(attr);
	}
}

static const char *onie_nvmem_attr_name(u8 type)
{
	switch (type) {
	case 0x21: return "product-name";
	case 0x22: return "part-number";
	case 0x23: return "serial-number";
	case 0x24: return "mac-address";
	case 0x25: return "manufacture-date";
	case 0x26: return "device-version";
	case 0x27: return "label-revision";
	case 0x28: return "platforn-name";
	case 0x29: return "onie-version";
	case 0x2A: return "num-macs";
	case 0x2B: return "manufacturer";
	case 0x2C: return "country-code";
	case 0x2D: return "vendor";
	case 0x2E: return "diag-version";
	case 0x2F: return "service-tag";
	case 0xFD: return "vendor-extension";
	case 0xFE: return "crc32";

	default: return "unknown";
	}
}

static int onie_nvmem_tlv_parse(struct onie_tlv_parser *parser, u8 *data, u16 len)
{
	unsigned int hlen = sizeof(struct onie_tlv_hdr);
	unsigned int offset = 0;
	int err;

	parser->attr_count = 0;

	while (offset < len) {
		struct onie_nvmem_attr *attr;
		struct onie_tlv *tlv;

		tlv = (struct onie_tlv *)(data + offset);

		if (offset + tlv->len >= len) {
			pr_err("TLV len is too big(0x%x) at 0x%x\n",
				tlv->len, hlen + offset);

			/* return success in case something was parsed */
			return 0;
		}

		attr = kmalloc(sizeof(*attr), GFP_KERNEL);
		if (!attr) {
			err = -ENOMEM;
			goto err_attr_alloc;
		}

		attr->name = onie_nvmem_attr_name(tlv->type);
		/* skip 'type' and 'len' */
		attr->offset = hlen + offset + 2;
		attr->len = tlv->len;

		list_add(&attr->head, &parser->attrs);
		parser->attr_count++;

		offset += sizeof(*tlv) + tlv->len;
	}

	if (!parser->attr_count)
		return -EINVAL;

	return 0;

err_attr_alloc:
	onie_nvmem_attrs_free(parser);
	return err;
}

static int onie_nvmem_decode(struct onie_tlv_parser *parser, struct nvmem_device *nvmem)
{
	struct onie_tlv_hdr hdr;
	u8 *data;
	u16 len;
	int ret;

	ret = nvmem_device_read(nvmem, 0, sizeof(hdr), &hdr);
	if (ret < 0)
		return ret;

	if (!onie_nvmem_hdr_is_valid(&hdr)) {
		pr_err("invalid ONIE TLV header\n");
		return -EINVAL;
	}

	len = be16_to_cpu(hdr.data_len);

	if (len > ONIE_NVMEM_TLV_MAX_LEN)
		len = ONIE_NVMEM_TLV_MAX_LEN;

	data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = nvmem_device_read(nvmem, sizeof(hdr), len, data);
	if (ret < 0)
		goto err_data_read;

	ret = onie_nvmem_tlv_parse(parser, data, len);
	if (ret)
		goto err_info_parse;

	kfree(data);

	return 0;

err_info_parse:
err_data_read:
	kfree(data);
	return ret;
}

static int onie_nvmem_cells_parse(struct onie_tlv_parser *parser,
				  struct nvmem_device *nvmem,
				  struct nvmem_cell_table *table)
{
	struct nvmem_cell_info *cells;
	struct onie_nvmem_attr *attr;
	unsigned int ncells = 0;
	int err;

	INIT_LIST_HEAD(&parser->attrs);
	parser->attr_count = 0;

	err = onie_nvmem_decode(parser, nvmem);
	if (err)
		return err;

	cells = kmalloc_array(parser->attr_count, sizeof(*cells), GFP_KERNEL);
	if (!cells) {
		err = -ENOMEM;
		goto err_cells_alloc;
	}

	parser->lookup = kmalloc_array(parser->attr_count,
				     sizeof(struct nvmem_cell_lookup),
				     GFP_KERNEL);
	if (!parser->lookup) {
		err = -ENOMEM;
		goto err_lookup_alloc;
	}

	list_for_each_entry(attr, &parser->attrs, head) {
		struct nvmem_cell_lookup *lookup;
		struct nvmem_cell_info *cell;

		cell = &cells[ncells];

		lookup = &parser->lookup[ncells];
		lookup->con_id = NULL;

		cell->offset = attr->offset;
		cell->name = attr->name;
		cell->bytes = attr->len;
		cell->bit_offset = 0;
		cell->nbits = 0;

		lookup->cell_name = cell->name;
		lookup->con_id = cell->name;

		ncells++;
	}

	table->ncells = ncells;
	table->cells = cells;

	parser->nlookups = ncells;

	onie_nvmem_attrs_free(parser);

	return 0;

err_lookup_alloc:
	kfree(cells);
err_cells_alloc:
	onie_nvmem_attrs_free(parser);

	return err;
}

static int onie_cells_parse(struct nvmem_device *nvmem,
			    struct nvmem_parser_data *data)
{
	struct onie_tlv_parser parser;
	int err;

	err = onie_nvmem_cells_parse(&parser, nvmem, &data->table);
	if (err) {
		pr_err("failed to parse ONIE attributes\n");
		return err;
	}

	data->nlookups = parser.nlookups;
	data->lookup = parser.lookup;

	return 0;
}

static int __init onie_tlv_init(void)
{
	struct nvmem_parser_config parser_config = { };

	parser_config.cells_parse = onie_cells_parse;
	parser_config.owner = THIS_MODULE;
	parser_config.name = "onie-tlv-cells";

	nvmem_parser = nvmem_parser_register(&parser_config);
	if (IS_ERR(nvmem_parser)) {
		pr_err("failed to register %s parser\n", parser_config.name);
		return PTR_ERR(nvmem_parser);
	}

	pr_info("registered %s parser\n", parser_config.name);

	return 0;
}

static void __exit onie_tlv_exit(void)
{
	nvmem_parser_unregister(nvmem_parser);
}

module_init(onie_tlv_init);
module_exit(onie_tlv_exit);

MODULE_AUTHOR("Vadym Kochan <vadym.kochan@plvision.eu>");
MODULE_DESCRIPTION("ONIE TLV NVMEM cells parser");
MODULE_LICENSE("GPL");
