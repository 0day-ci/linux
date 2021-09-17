// SPDX-License-Identifier: GPL-2.0
/*
 * EFI initialization
 *
 * Author: Jianmin Lv <lvjianmin@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */

#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/efi-bgrt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/memblock.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>

#include <asm/efi.h>
#include <asm/boot_param.h>

static efi_config_table_type_t arch_tables[] __initdata = {{},};

void __init efi_runtime_init(void)
{
	if (!efi_enabled(EFI_BOOT))
		return;

	if (!efi.runtime)
		return;

	if (efi_runtime_disabled()) {
		pr_info("EFI runtime services will be disabled.\n");
		return;
	}

	efi_native_runtime_setup();
	set_bit(EFI_RUNTIME_SERVICES, &efi.flags);
}

void __init efi_init(void)
{
	unsigned long efi_config_table;
	efi_system_table_t *efi_systab;

	if (!efi_bp)
		return;

	efi_systab = (efi_system_table_t *)efi_bp->systemtable;
	if (!efi_systab) {
		pr_err("Can't find EFI system table.\n");
		return;
	}

	set_bit(EFI_64BIT, &efi.flags);
	efi_config_table = (unsigned long)efi_systab->tables;
	efi.runtime	 = (efi_runtime_services_t *)efi_systab->runtime;
	efi.runtime_version = efi.runtime ? (unsigned int)efi.runtime->hdr.revision : 0;

	efi_config_parse_tables((void *)efi_systab->tables, efi_systab->nr_tables, arch_tables);
}

static ssize_t boardinfo_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,
		"BIOS Information\n"
		"Vendor\t\t\t: %s\n"
		"Version\t\t\t: %s\n"
		"ROM Size\t\t: %d KB\n"
		"Release Date\t\t: %s\n\n"
		"Board Information\n"
		"Manufacturer\t\t: %s\n"
		"Board Name\t\t: %s\n"
		"Family\t\t\t: LOONGSON64\n\n",
		b_info.bios_vendor, b_info.bios_version,
		b_info.bios_size, b_info.bios_release_date,
		b_info.board_vendor, b_info.board_name);
}

static struct kobj_attribute boardinfo_attr = __ATTR(boardinfo, 0444,
						     boardinfo_show, NULL);

static int __init boardinfo_init(void)
{
	if (!efi_kobj)
		return -EINVAL;

	return sysfs_create_file(efi_kobj, &boardinfo_attr.attr);
}
late_initcall(boardinfo_init);
