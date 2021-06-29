/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides clock numbers for the ingenic,jz4775-cgu DT binding.
 *
 * They are roughly ordered as:
 *   - external clocks
 *   - PLLs
 *   - muxes/dividers in the order they appear in the jz4775 programmers manual
 *   - gates in order of their bit in the CLKGR* registers
 */

#ifndef __DT_BINDINGS_CLOCK_JZ4775_CGU_H__
#define __DT_BINDINGS_CLOCK_JZ4775_CGU_H__

#define JZ4775_CLK_EXCLK		0
#define JZ4775_CLK_RTCLK		1
#define JZ4775_CLK_APLL			2
#define JZ4775_CLK_MPLL			3
#define JZ4775_CLK_OTGPHY		4
#define JZ4775_CLK_SCLKA		5
#define JZ4775_CLK_UHC			6
#define JZ4775_CLK_UHCPHY		7
#define JZ4775_CLK_CPUMUX		8
#define JZ4775_CLK_CPU			9
#define JZ4775_CLK_L2CACHE		10
#define JZ4775_CLK_AHB0			11
#define JZ4775_CLK_AHB2PMUX		12
#define JZ4775_CLK_AHB2			13
#define JZ4775_CLK_PCLK			14
#define JZ4775_CLK_DDR			15
#define JZ4775_CLK_VPU			16
#define JZ4775_CLK_OTG			17
#define JZ4775_CLK_EXCLK_DIV2	18
#define JZ4775_CLK_I2S			19
#define JZ4775_CLK_LCD			20
#define JZ4775_CLK_MSCMUX		21
#define JZ4775_CLK_MSC0			22
#define JZ4775_CLK_MSC1			23
#define JZ4775_CLK_MSC2			24
#define JZ4775_CLK_SSI			25
#define JZ4775_CLK_CIM0			26
#define JZ4775_CLK_CIM1			27
#define JZ4775_CLK_PCM			28
#define JZ4775_CLK_BCH			29
#define JZ4775_CLK_EXCLK_DIV512	30
#define JZ4775_CLK_RTC			31
#define JZ4775_CLK_NEMC			32
#define JZ4775_CLK_I2C0			33
#define JZ4775_CLK_I2C1			34
#define JZ4775_CLK_I2C2			35
#define JZ4775_CLK_SADC			36
#define JZ4775_CLK_UART0		37
#define JZ4775_CLK_UART1		38
#define JZ4775_CLK_UART2		39
#define JZ4775_CLK_UART3		40
#define JZ4775_CLK_PDMA			41
#define JZ4775_CLK_MAC			42

#endif /* __DT_BINDINGS_CLOCK_JZ4775_CGU_H__ */
