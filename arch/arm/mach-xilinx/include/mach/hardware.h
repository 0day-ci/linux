/* arch/arm/mach-xilinx/include/mach/hardware.h
 *
 *  Copyright (C) 2009 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#define __ASM_ARCH_HARDWARE_H__

#ifdef DEBUG
#define xilinx_debug(x, ...)	printk(x, ...)
#else
#define xilinx_debug(x, ...)
#endif

/*
 * defines the clock rates
 */
#define PERIPHERAL_CLOCK_RATE	2500000
#define CLOCK_TICK_RATE		PERIPHERAL_CLOCK_RATE / 32 /* prescaled in timer */

/* There are a couple ram addresses needed for communication between the boot
 * loader software and the linux kernel with multiple cpus in the kernel (SMP).
 * A single page of memory is reserved so that the primary CPU can map it in
 * the MMU. 

 * The register addresses are reserved in the on-chip RAM and these addresses 
 * are mapped flat (virtual = physical). The page must be mapped early before 
 * the VM system is running for the SMP code to use it. Stay away from the end
 * of the page (0xFFC) as it seems to cause issues and it maybe related to 64
 * bit accesses on the bus for on chip memory.
 */

#define BOOT_REG_BASE		0xFFFE7000

#define BOOT_ADDRREG_OFFSET	0xFF0	
#define BOOT_LOCKREG_OFFSET	0xFF4   

#define BOOT_LOCK_KEY		0xFACECAFE

/*
 * Device base addresses, all are mapped flat such that virtual = physical, is
 * still true now with EPA9 addresses?
 */

#define IO_BASE			0xE0000000

/* The following are older and need to be cleaned up
 * and corrected.
 */

#define SMC_BASE		(IO_BASE + 0x0000E000)
#define NOR_BASE		(IO_BASE + 0x04000000)
#define WDT0_BASE		(IO_BASE + 0x0C002000)

/* Cleaned up addresses start here, please keep addresses in order to make
 * them easier to read.
 */

#define UART0_BASE		(IO_BASE)
#define UART1_BASE		(IO_BASE + 0x1000)
#define USB0_BASE		(IO_BASE + 0x2000)
#define USB1_BASE		(IO_BASE + 0x3000)
#define I2C0_BASE		(IO_BASE + 0x4000)
#define I2C1_BASE		(IO_BASE + 0x5000)
#define SPI0_BASE		(IO_BASE + 0x6000)
#define SPI1_BASE		(IO_BASE + 0x7000)
#define CAN0_BASE		(IO_BASE + 0x8000)
#define	CAN1_BASE		(IO_BASE + 0x9000)
#define GPIO0_BASE		(IO_BASE + 0xA000)
#define ETH0_BASE               (IO_BASE + 0xB000)
#define ETH1_BASE               (IO_BASE + 0xC000)

#define PERIPH_BASE		0xF8000000

#define SLC_REG			(PERIPH_BASE)
#define TTC0_BASE		(PERIPH_BASE + 0x1000)
#define TTC1_BASE		(PERIPH_BASE + 0x2000)
#define DMAC0_BASE		(PERIPH_BASE + 0x3000)
#define DMAC1_BASE		(PERIPH_BASE + 0x4000)
#define WDT_BASE		(PERIPH_BASE + 0x5000)

#define SCU_PERIPH_BASE		0xF8F00000

#define SCU_GIC_CPU_BASE	(SCU_PERIPH_BASE + 0x100)
#define SCU_GLOBAL_TIMER_BASE	(SCU_PERIPH_BASE + 0x200)
#define SCU_CPU_TIMER_BASE	(SCU_PERIPH_BASE + 0x600)
#define SCU_WDT_BASE		(SCU_PERIPH_BASE + 0x620)
#define SCU_GIC_DIST_BASE	(SCU_PERIPH_BASE + 0x1000)

#define PL310_L2CC_BASE		0xF8F02000

/*
 * GIC Interrupts for Pele
 */

#define IRQ_SCU_GLOBAL_TIMER	27
#define IRQ_FABRIC_NFIQ		28
#define IRQ_SCU_CPU_TIMER	29
#define IRQ_SCU_WDT		30
#define IRQ_FABRIC_NIRQ		31

/* Shared peripheral interrupts */

#define IRQ_GIC_SPI_START	32
#define IRQ_TIMERCOUNTER0	42
#define IRQ_DMAC0		45
#define IRQ_GPIO0		52
#define IRQ_ETH0                54
#define IRQ_I2C0		57
#define IRQ_SPI0		58
#define IRQ_UART0		59
#define IRQ_TIMERCOUNTER1	69
#define IRQ_DMAC1		72
#define IRQ_ETH1                77
#define IRQ_I2C1		80
#define IRQ_SPI1		81
#define IRQ_UART1		82

/*
 * Start and size of physical RAM
 */
#define PHYS_OFFSET             0x0
#define MEM_SIZE		(128 * 1024 * 1024)

/*
 * Mandatory for CONFIG_LL_DEBUG
 */
#define MXC_LL_UART_PADDR	UART0_BASE
#define MXC_LL_UART_VADDR	UART0_BASE

#endif /* __ASM_ARCH_HARDWARE_H__ */
