// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/delay.h>
#include <linux/io.h>
#include "hal_iop.h"

//#define DEBUG_MESSAGE
//#define early_printk

#define IOP_KDBG_INFO
#define IOP_FUNC_DEBUG
#define IOP_KDBG_ERR
#ifdef IOP_KDBG_INFO
	#define FUNC_DEBUG()	pr_info("K_IOP: %s(%d)\n", __func__, __LINE__)
#else
	#define FUNC_DEBUG()
#endif

#ifdef IOP_FUNC_DEBUG
#define DBG_INFO(fmt, args ...)	pr_info("K_IOP: " fmt, ## args)
#else
#define DBG_INFO(fmt, args ...)
#endif

#ifdef IOP_KDBG_ERR
#define DBG_ERR(fmt, args ...)	pr_err("K_IOP: " fmt, ## args)
#else
#define DBG_ERR(fmt, args ...)
#endif

void hal_iop_init(void __iomem *iopbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;
	unsigned long *IOP_base_for_normal = (unsigned long *)SP_IOP_RESERVE_BASE;
	unsigned char *IOP_kernel_base;
	unsigned int reg;

	IOP_kernel_base = (unsigned char *)ioremap((unsigned long)IOP_base_for_normal,
		NORMAL_CODE_MAX_SIZE);
	memset((unsigned char *)IOP_kernel_base, 0, NORMAL_CODE_MAX_SIZE);
	memcpy((unsigned char *)IOP_kernel_base, IopNormalCode, NORMAL_CODE_MAX_SIZE);
	writel(0x00100010, (void __iomem *)(B_SYSTEM_BASE + 32*4*0 + 4*1));

	reg = readl(&pIopReg->iop_control);
	reg |= 0x01;
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &pIopReg->iop_control);

	reg = (unsigned int) ((unsigned long)(IOP_base_for_normal) & 0xFFFF);
	writel(reg, &pIopReg->iop_base_adr_l);
	reg	= (unsigned int) ((unsigned long)(IOP_base_for_normal) >> 16);
	writel(reg, &pIopReg->iop_base_adr_h);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x01);
	writel(reg, &pIopReg->iop_control);

	iop_code_mode = 0;
}
EXPORT_SYMBOL(hal_iop_init);

void hal_gpio_init(void __iomem *iopbase, unsigned char gpio_number)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;

	writel(0xFE02, &pIopReg->iop_data0);
	writel(gpio_number, &pIopReg->iop_data1);
}
EXPORT_SYMBOL(hal_gpio_init);

void hal_iop_load_normal_code(void __iomem *iopbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;
	unsigned long *IOP_base_for_normal = (unsigned long *)SP_IOP_RESERVE_BASE;
	unsigned char *IOP_kernel_base;
	unsigned int reg;

	IOP_kernel_base = (unsigned char *)ioremap((unsigned long)IOP_base_for_normal,
		RECEIVE_CODE_SIZE);
	memset((unsigned char *)IOP_kernel_base, 0, RECEIVE_CODE_SIZE);
	memcpy((unsigned char *)IOP_kernel_base, NormalCode, RECEIVE_CODE_SIZE);

	writel(0x00100010, (void __iomem *)(B_SYSTEM_BASE + 32*4*0 + 4*1));

	reg = readl(&pIopReg->iop_control);
	reg |= 0x01;
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &pIopReg->iop_control);

	reg = (unsigned int) ((unsigned long)(IOP_base_for_normal) & 0xFFFF);
	writel(reg, &pIopReg->iop_base_adr_l);
	reg	= (unsigned int) ((unsigned long)(IOP_base_for_normal) >> 16);
	writel(reg, &pIopReg->iop_base_adr_h);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x01);
	writel(reg, &pIopReg->iop_control);

	iop_code_mode = 0;
}
EXPORT_SYMBOL(hal_iop_load_normal_code);


void hal_iop_load_standby_code(void __iomem *iopbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;
	unsigned long *IOP_base_for_standby = (unsigned long *)(SP_IOP_RESERVE_BASE);
	unsigned char *IOP_kernel_base;
	unsigned int reg;

	IOP_kernel_base = (unsigned char *)ioremap((unsigned long)IOP_base_for_standby,
		RECEIVE_CODE_SIZE);
	memset((unsigned char *)IOP_kernel_base, 0, RECEIVE_CODE_SIZE);
	memcpy((unsigned char *)IOP_kernel_base, StandbyCode, RECEIVE_CODE_SIZE);

	writel(0x00100010, (void __iomem *)(B_SYSTEM_BASE + 32*4*0 + 4*1));

	reg = readl(&pIopReg->iop_control);
	reg |= 0x01;
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &pIopReg->iop_control);

	reg = (unsigned int) ((unsigned long)(IOP_base_for_standby) & 0xFFFF);
	writel(reg, &pIopReg->iop_base_adr_l);
	reg = (unsigned int) ((unsigned long)(IOP_base_for_standby) >> 16);
	writel(reg, &pIopReg->iop_base_adr_h);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x01);
	writel(reg, &pIopReg->iop_control);

	iop_code_mode = 1;
}
EXPORT_SYMBOL(hal_iop_load_standby_code);

void hal_iop_normalmode(void __iomem *iopbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;
	unsigned long *IOP_base_for_normal = (unsigned long *)SP_IOP_RESERVE_BASE;
	unsigned char *IOP_kernel_base;
	unsigned int reg;

	IOP_kernel_base = (unsigned char *)ioremap((unsigned long)IOP_base_for_normal,
		NORMAL_CODE_MAX_SIZE);
	memset((unsigned char *)IOP_kernel_base, 0, NORMAL_CODE_MAX_SIZE);
	memcpy((unsigned char *)IOP_kernel_base, IopNormalCode, NORMAL_CODE_MAX_SIZE);

	writel(0x00100010, (void __iomem *)(B_SYSTEM_BASE + 32*4*0 + 4*1));

	reg = readl(&pIopReg->iop_control);
	reg |= 0x01;
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &pIopReg->iop_control);

	reg = (unsigned int) ((unsigned long)(IOP_base_for_normal) & 0xFFFF);
	writel(reg, &pIopReg->iop_base_adr_l);
	reg	= (unsigned int) ((unsigned long)(IOP_base_for_normal) >> 16);
	writel(reg, &pIopReg->iop_base_adr_h);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x01);
	writel(reg, &pIopReg->iop_control);

	iop_code_mode = 0;
}
EXPORT_SYMBOL(hal_iop_normalmode);

void hal_iop_standbymode(void __iomem *iopbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;
	unsigned long *IOP_base_for_standby = (unsigned long *)(SP_IOP_RESERVE_BASE);
	unsigned char *IOP_kernel_base;
	unsigned int reg;

	IOP_kernel_base = (unsigned char *)ioremap((unsigned long)IOP_base_for_standby,
		STANDBY_CODE_MAX_SIZE);
	memset((unsigned char *)IOP_kernel_base, 0, STANDBY_CODE_MAX_SIZE);
	memcpy((unsigned char *)IOP_kernel_base, IopStandbyCode, STANDBY_CODE_MAX_SIZE);

	writel(0x00100010, (void __iomem *)(B_SYSTEM_BASE + 32*4*0 + 4*1));

	reg = readl(&pIopReg->iop_control);
	reg |= 0x01;
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &pIopReg->iop_control);

	reg = (unsigned int) ((unsigned long)(IOP_base_for_standby) & 0xFFFF);
	writel(reg, &pIopReg->iop_base_adr_l);
	reg = (unsigned int) ((unsigned long)(IOP_base_for_standby) >> 16);
	writel(reg, &pIopReg->iop_base_adr_h);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x01);
	writel(reg, &pIopReg->iop_control);

	iop_code_mode = 1;
}
EXPORT_SYMBOL(hal_iop_standbymode);

void hal_iop_get_iop_data(void __iomem *iopbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;
	unsigned short value_0, value_1, value_2, value_3, value_4, value_5;
	unsigned short value_6, value_7, value_8, value_9, value_10, value_11;

	value_0 = readl(&pIopReg->iop_data0);
	value_1 = readl(&pIopReg->iop_data1);
	value_2 = readl(&pIopReg->iop_data2);
	value_3 = readl(&pIopReg->iop_data3);
	value_4 = readl(&pIopReg->iop_data4);
	value_5 = readl(&pIopReg->iop_data5);
	value_6 = readl(&pIopReg->iop_data6);
	value_7 = readl(&pIopReg->iop_data7);
	value_8 = readl(&pIopReg->iop_data8);
	value_9 = readl(&pIopReg->iop_data9);
	value_10 = readl(&pIopReg->iop_data10);
	value_11 = readl(&pIopReg->iop_data11);
	DBG_INFO("%s(%d) iop_data0=%x iop_data1=%x iop_data2=%x iop_data3=%x\n",
		__func__, __LINE__, value_0, value_1, value_2, value_3);
	DBG_INFO("%s(%d) iop_data6=%x iop_data7=%x iop_data8=%x iop_data9=%x\n",
		__func__, __LINE__, value_6, value_7, value_8, value_9);
}
EXPORT_SYMBOL(hal_iop_get_iop_data);

void hal_iop_set_iop_data(void __iomem *iopbase, unsigned int num,
	unsigned int value)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;

	switch (num) {
	case '0':
	case 0:
		writel(value, &pIopReg->iop_data0);
		break;
	case '1':
	case 1:
		writel(value, &pIopReg->iop_data1);
		break;
	case '2':
	case 2:
		writel(value, &pIopReg->iop_data2);
		break;
	case '3':
	case 3:
		writel(value, &pIopReg->iop_data3);
		break;
	case '4':
	case 4:
		writel(value, &pIopReg->iop_data4);
		break;
	case '5':
	case 5:
		writel(value, &pIopReg->iop_data5);
		break;
	case '6':
	case 6:
		writel(value, &pIopReg->iop_data6);
		break;
	case '7':
	case 7:
		writel(value, &pIopReg->iop_data7);
		break;
	case '8':
	case 8:
		writel(value, &pIopReg->iop_data8);
		break;
	case '9':
	case 9:
		writel(value, &pIopReg->iop_data9);
		break;
	case 'A':
		writel(value, &pIopReg->iop_data10);
		break;
	case 'B':
		writel(value, &pIopReg->iop_data11);
		break;
	}
}
EXPORT_SYMBOL(hal_iop_set_iop_data);

#define IOP_READY	0x4
#define RISC_READY	0x8

void hal_iop_suspend(void __iomem *iopbase, void __iomem *ioppmcbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;
	struct regs_iop_pmc_t *pIopPmcReg = (struct regs_iop_pmc_t *)ioppmcbase;
	//regs_iop_rtc_t *pIopRtcReg = (regs_iop_rtc_t *)ioprtcbase;
	unsigned int reg;
	unsigned long *IOP_base_for_standby = (unsigned long *)(SP_IOP_RESERVE_BASE);

	writel(0x00100010, (void __iomem *)(B_SYSTEM_BASE + 32*4*0 + 4*1));

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg |= 0x1;
	writel(reg, &pIopReg->iop_control);

	//PMC set
	writel(0x00010001, &pIopPmcReg->PMC_TIMER);
	reg = readl(&pIopPmcReg->PMC_CTRL);
	reg |= 0x23;// disable system reset PMC, enalbe power down 27M, enable gating 27M
	writel(reg, &pIopPmcReg->PMC_CTRL);
	writel(0x55aa00ff, &pIopPmcReg->XTAL27M_PASSWORD_I);
	writel(0x00ff55aa, &pIopPmcReg->XTAL27M_PASSWORD_II);
	writel(0xaa00ff55, &pIopPmcReg->XTAL32K_PASSWORD_I);
	writel(0xff55aa00, &pIopPmcReg->XTAL32K_PASSWORD_II);
	writel(0xaaff0055, &pIopPmcReg->CLK27M_PASSWORD_I);
	writel(0x5500aaff, &pIopPmcReg->CLK27M_PASSWORD_II);
	writel(0x01000100, &pIopPmcReg->PMC_TIMER2);

	//IOP Hardware IP reset
	reg = readl((void __iomem *)(B_SYSTEM_BASE + 4 * 21));
	reg |= 0x10;
	writel(reg, (void __iomem *)(B_SYSTEM_BASE + 4 * 21));
	reg &= ~(0x10);
	writel(reg, (void __iomem *)(B_SYSTEM_BASE + 4 * 21));


	writel(0x00ff0085, (void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*1));

	reg = readl((void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*2));
	reg |= 0x08000800;
	writel(reg, (void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*2));

	reg = readl(&pIopReg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &pIopReg->iop_control);

	reg = (unsigned int) ((unsigned long)(IOP_base_for_standby) & 0xFFFF);
	writel(reg, &pIopReg->iop_base_adr_l);
	reg = (unsigned int) ((unsigned long)(IOP_base_for_standby) >> 16);
	writel(reg, &pIopReg->iop_base_adr_h);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x01);
	writel(reg, &pIopReg->iop_control);

	while ((pIopReg->iop_data2&IOP_READY) != IOP_READY)
		;

	reg = readl(&pIopReg->iop_data2);
	reg |= RISC_READY;
	writel(reg, &pIopReg->iop_control);

	writel(0x00, &pIopReg->iop_data5);
	writel(0x60, &pIopReg->iop_data6);

	while (1) {
		if (pIopReg->iop_data7 == 0xaaaa)
			break;
	}

	writel(0xdd, &pIopReg->iop_data1);//8051 bin file call Ultra low function.
	FUNC_DEBUG();
}
EXPORT_SYMBOL(hal_iop_suspend);

void hal_iop_shutdown(void __iomem *iopbase, void __iomem *ioppmcbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;
	struct regs_iop_pmc_t *pIopPmcReg = (struct regs_iop_pmc_t *)ioppmcbase;
	unsigned int reg;
	unsigned long *IOP_base_for_standby = (unsigned long *)(SP_IOP_RESERVE_BASE);

	FUNC_DEBUG();

	writel(0x00100010, (void __iomem *)(B_SYSTEM_BASE + 32*4*0 + 4*1));

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x8000);
	writel(reg, &pIopReg->iop_control);

	reg = readl(&pIopReg->iop_control);
	reg |= 0x1;
	writel(reg, &pIopReg->iop_control);

	//PMC set
	writel(0x00010001, &pIopPmcReg->PMC_TIMER);
	reg = readl(&pIopPmcReg->PMC_CTRL);
	reg |= 0x23;// disable system reset PMC, enalbe power down 27M, enable gating 27M
	writel(reg, &pIopPmcReg->PMC_CTRL);

	writel(0x55aa00ff, &pIopPmcReg->XTAL27M_PASSWORD_I);
	writel(0x00ff55aa, &pIopPmcReg->XTAL27M_PASSWORD_II);
	writel(0xaa00ff55, &pIopPmcReg->XTAL32K_PASSWORD_I);
	writel(0xff55aa00, &pIopPmcReg->XTAL32K_PASSWORD_II);
	writel(0xaaff0055, &pIopPmcReg->CLK27M_PASSWORD_I);
	writel(0x5500aaff, &pIopPmcReg->CLK27M_PASSWORD_II);
	writel(0x01000100, &pIopPmcReg->PMC_TIMER2);

	//IOP Hardware IP reset
	reg = readl((void __iomem *)(B_SYSTEM_BASE + 4 * 21));
	reg |= 0x10;
	writel(reg, (void __iomem *)(B_SYSTEM_BASE + 4 * 21));
	reg &= ~(0x10);
	writel(reg, (void __iomem *)(B_SYSTEM_BASE + 4 * 21));

	writel(0x00ff0085, (void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*1));

	reg = readl((void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*2));
	reg |= 0x08000800;
	writel(reg, (void __iomem *)(B_SYSTEM_BASE + 32*4*1 + 4*2));

	reg = readl(&pIopReg->iop_control);
	reg |= 0x0200;//disable watchdog event reset IOP
	writel(reg, &pIopReg->iop_control);

	reg = (unsigned int) ((unsigned long)(IOP_base_for_standby) & 0xFFFF);
	writel(reg, &pIopReg->iop_base_adr_l);
	reg = (unsigned int) ((unsigned long)(IOP_base_for_standby) >> 16);
	writel(reg, &pIopReg->iop_base_adr_h);

	reg = readl(&pIopReg->iop_control);
	reg &= ~(0x01);
	writel(reg, &pIopReg->iop_control);

	while ((pIopReg->iop_data2&IOP_READY) != IOP_READY)
		;

	writel(RISC_READY, &pIopReg->iop_data2);
	writel(0x00, &pIopReg->iop_data5);
	writel(0x60, &pIopReg->iop_data6);

	while (1) {
		if (pIopReg->iop_data7 == 0xaaaa)
			break;
	}

	writel(0xdd, &pIopReg->iop_data1);//8051 bin file call Ultra low function.
	mdelay(10);
}
EXPORT_SYMBOL(hal_iop_shutdown);

void hal_iop_S1mode(void __iomem *iopbase)
{
	struct regs_iop_t *pIopReg = (struct regs_iop_t *)iopbase;

	while ((pIopReg->iop_data2&IOP_READY) != IOP_READY)
		;

	writel(RISC_READY, &pIopReg->iop_data2);
	writel(0x00, &pIopReg->iop_data5);
	writel(0x60, &pIopReg->iop_data6);

	while (1) {
		if (pIopReg->iop_data7 == 0xaaaa)
			break;
	}

	writel(0xee, &pIopReg->iop_data1);//8051 bin file call S1_mode function.
	FUNC_DEBUG();

}
EXPORT_SYMBOL(hal_iop_S1mode);


