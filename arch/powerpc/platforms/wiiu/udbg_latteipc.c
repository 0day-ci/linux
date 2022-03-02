// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nintendo Wii U udbg support (to Starbuck coprocessor, via chipset IPC)
 *
 * Copyright (C) 2022 The linux-wiiu Team
 *
 * Based on arch/powerpc/platforms/embedded6xx/udbgecko_udbg.c
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008-2009 Albert Herranz
 */

#include <mm/mmu_decl.h>

#include <linux/io.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/fixmap.h>

#define LT_MMIO_BASE ((phys_addr_t)0x0d800000)
#define LT_IPC_PPCMSG 0x00
#define LT_IPC_PPCCTRL 0x04
#define LT_IPC_PPCCTRL_X1 0x01

#define WIIU_LOADER_CMD_PRINT 0x01000000

void __iomem *latteipc_io_base;

/*
 * Transmits a character.
 * Sends over IPC to linux-loader for printing.
 */
static void latteipc_udbg_putc(char c)
{
	void __iomem *ppcmsg_reg = latteipc_io_base + LT_IPC_PPCMSG;
	void __iomem *ppcctrl_reg = latteipc_io_base + LT_IPC_PPCCTRL;

	out_be32(ppcmsg_reg, WIIU_LOADER_CMD_PRINT | (c << 16));
	out_be32(ppcctrl_reg, LT_IPC_PPCCTRL_X1);

	while (in_be32(ppcctrl_reg) & LT_IPC_PPCCTRL_X1)
		barrier();
}

/*
 * Retrieves and prepares the virtual address needed to access the hardware.
 */
static void __iomem *latteipc_udbg_setup_ipc_io_base(struct device_node *np)
{
	void __iomem *ipc_io_base = NULL;
	phys_addr_t paddr;
	const unsigned int *reg;

	reg = of_get_property(np, "reg", NULL);
	if (reg) {
		paddr = of_translate_address(np, reg);
		if (paddr)
			ipc_io_base = ioremap(paddr, reg[1]);
	}
	return ipc_io_base;
}

/*
 * Latte IPC udbg support initialization.
 */
void __init latteipc_udbg_init(void)
{
	struct device_node *np;
	void __iomem *ipc_io_base;

	if (latteipc_io_base)
		udbg_printf("%s: early -> final\n", __func__);

	np = of_find_compatible_node(NULL, NULL, "nintendo,latte-ipc");
	if (!np) {
		udbg_printf("%s: IPC node not found\n", __func__);
		goto out;
	}

	ipc_io_base = latteipc_udbg_setup_ipc_io_base(np);
	if (!ipc_io_base) {
		udbg_printf("%s: failed to setup IPC io base\n", __func__);
		goto done;
	}

	udbg_putc = latteipc_udbg_putc;
	udbg_printf("latteipc_udbg: ready\n");

done:
	of_node_put(np);
out:
	return;
}

#ifdef CONFIG_PPC_EARLY_DEBUG_LATTEIPC

void __init udbg_init_latteipc(void)
{
	/*
	 * At this point we have a BAT already setup that enables I/O
	 * to the IPC hardware.
	 *
	 * The BAT uses a virtual address range reserved at the fixmap.
	 * This must match the virtual address configured in
	 * head_32.S:setup_latteipc_bat().
	 */
	latteipc_io_base = (void __iomem *)__fix_to_virt(FIX_EARLY_DEBUG_BASE);

	/* Assume a firmware is present, add hooks */
	udbg_putc = latteipc_udbg_putc;

	/*
	 * Prepare again the same BAT for MMU_init.
	 * This allows udbg I/O to continue working after the MMU is
	 * turned on for real.
	 * It is safe to continue using the same virtual address as it is
	 * a reserved fixmap area.
	 */
	setbat(1, (unsigned long)latteipc_io_base, LT_MMIO_BASE, 128 * 1024,
	       PAGE_KERNEL_NCG);
}

#endif /* CONFIG_PPC_EARLY_DEBUG_LATTEIPC */
