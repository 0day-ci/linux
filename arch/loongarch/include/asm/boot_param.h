/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_BOOT_PARAM_H_
#define __ASM_BOOT_PARAM_H_

#ifdef CONFIG_VT
#include <linux/screen_info.h>
#endif

#define ADDRESS_TYPE_SYSRAM	1
#define ADDRESS_TYPE_RESERVED	2
#define ADDRESS_TYPE_ACPI	3
#define ADDRESS_TYPE_NVS	4
#define ADDRESS_TYPE_PMEM	5

#define EFI_RUNTIME_MAP_START	   100
#define LOONGSON3_BOOT_MEM_MAP_MAX 128

#define LOONGSON_EFIBOOT_SIGNATURE	"BPI"
#define LOONGSON_MEM_SIGNATURE		"MEM"
#define LOONGSON_VBIOS_SIGNATURE	"VBIOS"
#define LOONGSON_SCREENINFO_SIGNATURE	"SINFO"

/* Values for Version BPI */
enum bpi_version {
	BPI_VERSION_V1 = 1000, /* Signature="BPI01000" */
	BPI_VERSION_V2 = 1001, /* Signature="BPI01001" */
	BPI_VERSION_V3 = 1002, /* Signature="BPI01002" */
};

/* Flags in bootparamsinterface */
#define BPI_FLAGS_UEFI_SUPPORTED BIT(0)

struct _extention_list_hdr {
	u64	signature;
	u32	length;
	u8	revision;
	u8	checksum;
	u64	next_offset;
} __packed;

struct boot_params {
	u64	signature;	/* {"B", "P", "I", "0", "1", ... } */
	void	*systemtable;
	u64	extlist_offset;
	u64	flags;
} __packed;

struct loongsonlist_mem_map {
	struct	_extention_list_hdr header;	/* {"M", "E", "M"} */
	u8	map_count;
	u32	desc_version;
	struct efi_mmap {
		u32 mem_type;
		u32 padding;
		u64 mem_start;
		u64 mem_vaddr;
		u64 mem_size;
		u64 attribute;
	} __packed map[LOONGSON3_BOOT_MEM_MAP_MAX];
} __packed;

struct loongsonlist_vbios {
	struct	_extention_list_hdr header;	/* {"V", "B", "I", "O", "S"} */
	u64	vbios_addr;
} __packed;

struct loongsonlist_screeninfo {
	struct	_extention_list_hdr header;	/* {"S", "I", "N", "F", "O"} */
	struct	screen_info si;
} __packed;

struct loongson_board_info {
	int bios_size;
	char *bios_vendor;
	char *bios_version;
	char *bios_release_date;
	char *board_name;
	char *board_vendor;
};

struct loongson_system_configuration {
	int bpi_ver;
	int nr_cpus;
	int nr_nodes;
	int nr_io_pics;
	int boot_cpu_id;
	int cores_per_node;
	int cores_per_package;
	char *cpuname;
	u64 vgabios_addr;
};

extern struct boot_params *efi_bp;
extern struct loongson_board_info b_info;
extern struct loongsonlist_mem_map *loongson_mem_map;
extern struct loongson_system_configuration loongson_sysconf;
#endif
