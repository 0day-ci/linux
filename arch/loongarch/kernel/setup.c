// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1995 Linus Torvalds
 * Copyright (C) 1995 Waldorf Electronics
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 01, 02, 03  Ralf Baechle
 * Copyright (C) 1996 Stoned Elipot
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000, 2001, 2002, 2007	 Maciej W. Rozycki
 */
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/efi.h>
#include <linux/export.h>
#include <linux/screen_info.h>
#include <linux/memblock.h>
#include <linux/initrd.h>
#include <linux/ioport.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/pfn.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/device.h>
#include <linux/dma-map-ops.h>
#include <linux/swiotlb.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/cache.h>
#include <asm/cpu.h>
#include <asm/dma.h>
#include <asm/efi.h>
#include <asm/fw.h>
#include <asm/loongson.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/smp.h>
#include <asm/time.h>

#define SMBIOS_BIOSSIZE_OFFSET		0x09
#define SMBIOS_BIOSEXTERN_OFFSET	0x13
#define SMBIOS_FREQLOW_OFFSET		0x16
#define SMBIOS_FREQHIGH_OFFSET		0x17
#define SMBIOS_FREQLOW_MASK		0xFF
#define SMBIOS_CORE_PACKAGE_OFFSET	0x23
#define LOONGSON_EFI_ENABLE		(1 << 3)

#ifdef CONFIG_VT
struct screen_info screen_info;
#endif

DEFINE_PER_CPU(unsigned long, kernelsp);
unsigned long fw_arg0, fw_arg1, fw_arg2, fw_arg3;
struct cpuinfo_loongarch cpu_data[NR_CPUS] __read_mostly;

EXPORT_SYMBOL(cpu_data);

struct loongson_board_info b_info;
static const char dmi_empty_string[] = "        ";

/*
 * Setup information
 *
 * These are initialized so they are in the .data section
 */

char __initdata arcs_cmdline[COMMAND_LINE_SIZE];
static char __initdata command_line[COMMAND_LINE_SIZE];

static struct resource code_resource = { .name = "Kernel code", };
static struct resource data_resource = { .name = "Kernel data", };
static struct resource bss_resource = { .name = "Kernel bss", };

const char *get_system_type(void)
{
	return "generic-loongson-machine";
}

static const char *dmi_string_parse(const struct dmi_header *dm, u8 s)
{
	const u8 *bp = ((u8 *) dm) + dm->length;

	if (s) {
		s--;
		while (s > 0 && *bp) {
			bp += strlen(bp) + 1;
			s--;
		}

		if (*bp != 0) {
			size_t len = strlen(bp)+1;
			size_t cmp_len = len > 8 ? 8 : len;

			if (!memcmp(bp, dmi_empty_string, cmp_len))
				return dmi_empty_string;

			return bp;
		}
	}

	return "";
}

static void __init parse_cpu_table(const struct dmi_header *dm)
{
	long freq_temp = 0;
	char *dmi_data = (char *)dm;

	freq_temp = ((*(dmi_data + SMBIOS_FREQHIGH_OFFSET) << 8) +
			((*(dmi_data + SMBIOS_FREQLOW_OFFSET)) & SMBIOS_FREQLOW_MASK));
	cpu_clock_freq = freq_temp * 1000000;

	loongson_sysconf.cpuname = (void *)dmi_string_parse(dm, dmi_data[16]);
	loongson_sysconf.cores_per_package = *(dmi_data + SMBIOS_CORE_PACKAGE_OFFSET);

	pr_info("CpuClock = %llu\n", cpu_clock_freq);
}

static void __init parse_bios_table(const struct dmi_header *dm)
{
	int bios_extern;
	char *dmi_data = (char *)dm;

	bios_extern = *(dmi_data + SMBIOS_BIOSEXTERN_OFFSET);
	b_info.bios_size = *(dmi_data + SMBIOS_BIOSSIZE_OFFSET);

	if (bios_extern & LOONGSON_EFI_ENABLE)
		set_bit(EFI_BOOT, &efi.flags);
	else
		clear_bit(EFI_BOOT, &efi.flags);
}

static void __init find_tokens(const struct dmi_header *dm, void *dummy)
{
	switch (dm->type) {
	case 0x0: /* Extern BIOS */
		parse_bios_table(dm);
		break;
	case 0x4: /* Calling interface */
		parse_cpu_table(dm);
		break;
	}
}
static void __init smbios_parse(void)
{
	b_info.bios_vendor = (void *)dmi_get_system_info(DMI_BIOS_VENDOR);
	b_info.bios_version = (void *)dmi_get_system_info(DMI_BIOS_VERSION);
	b_info.bios_release_date = (void *)dmi_get_system_info(DMI_BIOS_DATE);
	b_info.board_vendor = (void *)dmi_get_system_info(DMI_BOARD_VENDOR);
	b_info.board_name = (void *)dmi_get_system_info(DMI_BOARD_NAME);
	dmi_walk(find_tokens, NULL);
}

/*
 * Manage initrd
 */
#ifdef CONFIG_BLK_DEV_INITRD

static unsigned long __init init_initrd(void)
{
	if (!phys_initrd_start || !phys_initrd_size)
		goto disable;

	initrd_start = (unsigned long)phys_to_virt(phys_initrd_start);
	initrd_end   = (unsigned long)phys_to_virt(phys_initrd_start + phys_initrd_size);

	if (!initrd_start || initrd_end <= initrd_start)
		goto disable;

	if (initrd_start & ~PAGE_MASK) {
		pr_err("initrd start must be page aligned\n");
		goto disable;
	}
	if (initrd_start < PAGE_OFFSET) {
		pr_err("initrd start < PAGE_OFFSET\n");
		goto disable;
	}

	ROOT_DEV = Root_RAM0;

	return 0;
disable:
	initrd_start = 0;
	initrd_end = 0;
	return 0;
}

static void __init finalize_initrd(void)
{
	unsigned long size = initrd_end - initrd_start;

	if (size == 0) {
		pr_info("Initrd not found or empty");
		goto disable;
	}
	if (__pa(initrd_end) > PFN_PHYS(max_low_pfn)) {
		pr_err("Initrd extends beyond end of memory");
		goto disable;
	}


	memblock_reserve(__pa(initrd_start), size);
	initrd_below_start_ok = 1;

	pr_info("Initial ramdisk at: 0x%lx (%lu bytes)\n",
		initrd_start, size);
	return;
disable:
	pr_cont(" - disabling initrd\n");
	initrd_start = 0;
	initrd_end = 0;
}

#else  /* !CONFIG_BLK_DEV_INITRD */

static unsigned long __init init_initrd(void)
{
	return 0;
}

#define finalize_initrd()	do {} while (0)

#endif

static int usermem __initdata;

static int __init early_parse_mem(char *p)
{
	phys_addr_t start, size;

	/*
	 * If a user specifies memory size, we
	 * blow away any automatically generated
	 * size.
	 */
	if (usermem == 0) {
		usermem = 1;
		memblock_remove(memblock_start_of_DRAM(),
			memblock_end_of_DRAM() - memblock_start_of_DRAM());
	}
	start = 0;
	size = memparse(p, &p);
	if (*p == '@')
		start = memparse(p + 1, &p);

	memblock_add(start, size);

	return 0;
}
early_param("mem", early_parse_mem);

static int __init early_parse_memmap(char *p)
{
	char *oldp;
	u64 start_at, mem_size;

	if (!p)
		return -EINVAL;

	if (!strncmp(p, "exactmap", 8)) {
		pr_err("\"memmap=exactmap\" invalid on LoongArch\n");
		return 0;
	}

	oldp = p;
	mem_size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;

	if (*p == '@') {
		start_at = memparse(p+1, &p);
		memblock_add(start_at, mem_size);
	} else if (*p == '#') {
		pr_err("\"memmap=nn#ss\" (force ACPI data) invalid on LoongArch\n");
		return -EINVAL;
	} else if (*p == '$') {
		start_at = memparse(p+1, &p);
		memblock_add(start_at, mem_size);
		memblock_reserve(start_at, mem_size);
	} else {
		pr_err("\"memmap\" invalid format!\n");
		return -EINVAL;
	}

	if (*p == '\0') {
		usermem = 1;
		return 0;
	} else
		return -EINVAL;
}
early_param("memmap", early_parse_memmap);

static void __init check_kernel_sections_mem(void)
{
	phys_addr_t start = __pa_symbol(&_text);
	phys_addr_t size = __pa_symbol(&_end) - start;

	if (!memblock_is_region_memory(start, size)) {
		pr_info("Kernel sections are not in the memory maps\n");
		memblock_add(start, size);
	}
}

static void __init bootcmdline_append(const char *s, size_t max)
{
	if (!s[0] || !max)
		return;

	if (boot_command_line[0])
		strlcat(boot_command_line, " ", COMMAND_LINE_SIZE);

	strlcat(boot_command_line, s, max);
}

static void __init bootcmdline_init(char **cmdline_p)
{
	boot_command_line[0] = 0;

	/*
	 * Take arguments from the bootloader at first. Early code should have
	 * filled arcs_cmdline with arguments from the bootloader.
	 */
	bootcmdline_append(arcs_cmdline, COMMAND_LINE_SIZE);

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	parse_early_param();
}

/*
 * arch_mem_init - initialize memory management subsystem
 */
static void __init arch_mem_init(char **cmdline_p)
{
	if (usermem)
		pr_info("User-defined physical RAM map overwrite\n");

	check_kernel_sections_mem();

#ifndef CONFIG_NUMA
	memblock_set_node(0, PHYS_ADDR_MAX, &memblock.memory, 0);
#endif
	pagetable_init();

	/*
	 * Prevent memblock from allocating high memory.
	 * This cannot be done before max_low_pfn is detected, so up
	 * to this point is possible to only reserve physical memory
	 * with memblock_reserve; memblock_alloc* can be used
	 * only after this point
	 */
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));

	/*
	 * In order to reduce the possibility of kernel panic when failed to
	 * get IO TLB memory under CONFIG_SWIOTLB, it is better to allocate
	 * low memory as small as possible before plat_swiotlb_setup(), so
	 * make sparse_init() using top-down allocation.
	 */
	memblock_set_bottom_up(false);
	sparse_init();
	memblock_set_bottom_up(true);

	plat_swiotlb_setup();

	dma_contiguous_reserve(PFN_PHYS(max_low_pfn));

	memblock_dump_all();

	early_memtest(PFN_PHYS(ARCH_PFN_OFFSET), PFN_PHYS(max_low_pfn));
}

static void __init resource_init(void)
{
	u64 i;
	phys_addr_t start, end;

	code_resource.start = __pa_symbol(&_text);
	code_resource.end = __pa_symbol(&_etext) - 1;
	data_resource.start = __pa_symbol(&_etext);
	data_resource.end = __pa_symbol(&_edata) - 1;
	bss_resource.start = __pa_symbol(&__bss_start);
	bss_resource.end = __pa_symbol(&__bss_stop) - 1;

	for_each_mem_range(i, &start, &end) {
		struct resource *res;

		res = memblock_alloc(sizeof(struct resource), SMP_CACHE_BYTES);
		if (!res)
			panic("%s: Failed to allocate %zu bytes\n", __func__,
			      sizeof(struct resource));

		res->start = start;
		/*
		 * In memblock, end points to the first byte after the
		 * range while in resourses, end points to the last byte in
		 * the range.
		 */
		res->end = end - 1;
		res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		res->name = "System RAM";

		request_resource(&iomem_resource, res);

		/*
		 *  We don't know which RAM region contains kernel data,
		 *  so we try it repeatedly and let the resource manager
		 *  test it.
		 */
		request_resource(res, &code_resource);
		request_resource(res, &data_resource);
		request_resource(res, &bss_resource);
	}
}

void __init platform_init(void)
{
	efi_init();
#ifdef CONFIG_ACPI_TABLE_UPGRADE
	acpi_table_upgrade();
#endif
#ifdef CONFIG_ACPI
	acpi_gbl_use_default_register_widths = false;
	acpi_boot_table_init();
	acpi_boot_init();
#endif

#ifndef CONFIG_NUMA
	fw_init_memory();
#else
	fw_init_numa_memory();
#endif
	dmi_setup();
	smbios_parse();
	pr_info("The BIOS Version: %s\n", b_info.bios_version);

	efi_runtime_init();
}

#ifdef CONFIG_SMP
static void __init prefill_possible_map(void)
{
	int i, possible;

	possible = num_processors + disabled_cpus;
	if (possible > nr_cpu_ids)
		possible = nr_cpu_ids;

	pr_info("SMP: Allowing %d CPUs, %d hotplug CPUs\n",
			possible, max((possible - num_processors), 0));

	for (i = 0; i < possible; i++)
		set_cpu_possible(i, true);
	for (; i < NR_CPUS; i++)
		set_cpu_possible(i, false);

	nr_cpu_ids = possible;
}
#else
static inline void prefill_possible_map(void) {}
#endif

void __init setup_arch(char **cmdline_p)
{
	cpu_probe();

	fw_init_cmdline();
	fw_init_environ();
	early_memblock_init();
	bootcmdline_init(cmdline_p);

	init_initrd();
	platform_init();
	finalize_initrd();

	arch_mem_init(cmdline_p);

	resource_init();
	plat_smp_setup();
	prefill_possible_map();

	paging_init();
}

static int __init register_gop_device(void)
{
	void *pd;

	if (screen_info.orig_video_isVGA != VIDEO_TYPE_EFI)
		return 0;
	pd = platform_device_register_data(NULL, "efi-framebuffer", 0,
			&screen_info, sizeof(screen_info));
	return PTR_ERR_OR_ZERO(pd);
}
subsys_initcall(register_gop_device);
