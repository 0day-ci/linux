/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DT_TLV320ADC3XXX_H
#define __DT_TLV320ADC3XXX_H

/* PLL modes, derived from clk_id of set_sysclk callback, or set in
 * devicetree using the ti,pll-mode property.
 *
 * The default behavior is to take the first matching entry in the clock
 * table, which is intended to be the PLL based one if there is more than one.
 *
 * Setting the clock source using simple-card (clocks or
 * system-clock-frequency property) sets clk_id = 0 = ADC3XXX_CLK_DONT_SET,
 * which doesn't change whatever clock setting was previously set up.
 */
#define ADC3XXX_PLL_DONT_SET	0 /* Don't change mode */
#define ADC3XXX_PLL_ENABLE	1 /* Use PLL for clock generation */
#define ADC3XXX_PLL_BYPASS	2 /* Don't use PLL for clock generation */
#define ADC3XXX_PLL_AUTO	3 /* Use first available mode */

#define ADC3XXX_GPIO_DISABLED		0 /* I/O buffers powered down */
#define ADC3XXX_GPIO_INPUT		1 /* Various non-GPIO inputs */
#define ADC3XXX_GPIO_GPI		2 /* General purpose input */
#define ADC3XXX_GPIO_GPO		3 /* General purpose output */
#define ADC3XXX_GPIO_CLKOUT		4 /* Source set in reg. CLKOUT_MUX */
#define ADC3XXX_GPIO_INT1		5 /* INT1 output */
#define ADC3XXX_GPIO_INT2		6 /* INT2 output */
/* value 7 is reserved */
#define ADC3XXX_GPIO_SECONDARY_BCLK	8 /* Codec interface secondary BCLK */
#define ADC3XXX_GPIO_SECONDARY_WCLK	9 /* Codec interface secondary WCLK */
#define ADC3XXX_GPIO_ADC_MOD_CLK	10 /* Clock output for digital mics */
/* values 11-15 reserved */

#endif /* __DT_TLV320ADC3XXX_H */
