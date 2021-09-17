// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/memblock.h>

#include <asm/bootinfo.h>
#include <asm/loongson.h>
#include <asm/sections.h>

void __init early_memblock_init(void)
{
	int i;
	u32 mem_type;
	u64 mem_start, mem_end, mem_size;

	/* parse memory information */
	for (i = 0; i < loongson_mem_map->map_count; i++) {
		mem_type = loongson_mem_map->map[i].mem_type;
		mem_start = loongson_mem_map->map[i].mem_start;
		mem_size = loongson_mem_map->map[i].mem_size;
		mem_end = mem_start + mem_size;

		switch (mem_type) {
		case ADDRESS_TYPE_SYSRAM:
			memblock_add(mem_start, mem_size);
			if (max_low_pfn < (mem_end >> PAGE_SHIFT))
				max_low_pfn = mem_end >> PAGE_SHIFT;
			break;
		}
	}
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));
}

void __init fw_init_memory(void)
{
	int i;
	u32 mem_type;
	u64 mem_start, mem_end, mem_size;
	unsigned long start_pfn, end_pfn;
	static unsigned long num_physpages;

	/* parse memory information */
	for (i = 0; i < loongson_mem_map->map_count; i++) {
		mem_type = loongson_mem_map->map[i].mem_type;
		mem_start = loongson_mem_map->map[i].mem_start;
		mem_size = loongson_mem_map->map[i].mem_size;
		mem_end = mem_start + mem_size;

		switch (mem_type) {
		case ADDRESS_TYPE_SYSRAM:
			mem_start = PFN_ALIGN(mem_start);
			mem_end = PFN_ALIGN(mem_end - PAGE_SIZE + 1);
			num_physpages += (mem_size >> PAGE_SHIFT);
			memblock_set_node(mem_start, mem_size, &memblock.memory, 0);
			break;
		case ADDRESS_TYPE_ACPI:
		case ADDRESS_TYPE_RESERVED:
			memblock_reserve(loongson_mem_map->map[i].mem_start,
					 loongson_mem_map->map[i].mem_size);
			break;
		}
	}

	get_pfn_range_for_nid(0, &start_pfn, &end_pfn);
	pr_info("start_pfn=0x%lx, end_pfn=0x%lx, num_physpages:0x%lx\n",
				start_pfn, end_pfn, num_physpages);

	NODE_DATA(0)->node_start_pfn = start_pfn;
	NODE_DATA(0)->node_spanned_pages = end_pfn - start_pfn;

	/* used by finalize_initrd() */
	max_low_pfn = end_pfn;

	/* Reserve the first 2MB */
	memblock_reserve(PHYS_OFFSET, 0x200000);

	/* Reserve the kernel text/data/bss */
	memblock_reserve(__pa_symbol(&_text),
			 __pa_symbol(&_end) - __pa_symbol(&_text));
}
