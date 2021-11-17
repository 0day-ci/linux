/* SPDX-License-Identifier: GPL-2.0-or-later*/
#ifndef __REG_IOP_H__
#define __REG_IOP_H__
#ifdef CONFIG_SOC_SP7021
#include <mach/io_map.h>
#endif

struct regs_iop_moon0_t {
	unsigned int stamp;/* 00 */
	unsigned int clken[10];/* 01~10 */
	unsigned int gclken[10];/* 11~20 */
	unsigned int reset[10];/* 21~30 */
	unsigned int sfg_cfg_mode;/* 31 */
};



struct regs_iop_t {
	unsigned int iop_control;/* 00 */
	unsigned int iop_reg1;/* 01 */
	unsigned int iop_bp;/* 02 */
	unsigned int iop_regsel;/* 03 */
	unsigned int iop_regout;/* 04 */
	unsigned int iop_reg5;/* 05 */
	unsigned int iop_resume_pcl;/* 06 */
	unsigned int iop_resume_pch;/* 07 */
	unsigned int iop_data0;/* 08 */
	unsigned int iop_data1;/* 09 */
	unsigned int iop_data2;/* 10 */
	unsigned int iop_data3;/* 11 */
	unsigned int iop_data4;/* 12 */
	unsigned int iop_data5;/* 13 */
	unsigned int iop_data6;/* 14 */
	unsigned int iop_data7;/* 15 */
	unsigned int iop_data8;/* 16 */
	unsigned int iop_data9;/* 17 */
	unsigned int iop_data10;/* 18 */
	unsigned int iop_data11;/* 19 */
	unsigned int iop_base_adr_l;/* 20 */
	unsigned int iop_base_adr_h;/* 21 */
	unsigned int Memory_bridge_control;/* 22 */
	unsigned int iop_regmap_adr_l;/* 23 */
	unsigned int iop_regmap_adr_h;/* 24 */
	unsigned int iop_direct_adr;/* 25*/
	unsigned int reserved[6];/* 26~31 */
};


struct regs_iop_qctl_t {
	unsigned int PD_GRP0;/* 00 */
	unsigned int PD_GRP1;/* 01 */
	unsigned int PD_GRP2;/* 02 */
	unsigned int PD_GRP3;/* 03 */
	unsigned int PD_GRP4;/* 04 */
	unsigned int PD_GRP5;/* 05 */
	unsigned int PD_GRP6;/* 06 */
	unsigned int PD_GRP7;/* 07 */
	unsigned int PD_GRP8;/* 08 */
	unsigned int PD_GRP9;/* 09 */
	unsigned int PD_GRP10;/* 10 */
	unsigned int PD_GRP11;/* 11 */
	unsigned int reserved[20];/* 12~31 */
};

struct regs_iop_pmc_t {
	unsigned int PMC_TIMER;/* 00 */
	unsigned int PMC_CTRL;/* 01 */
	unsigned int XTAL27M_PASSWORD_I;/* 02 */
	unsigned int XTAL27M_PASSWORD_II;/* 03 */
	unsigned int XTAL32K_PASSWORD_I;/* 04 */
	unsigned int XTAL32K_PASSWORD_II;/* 05 */
	unsigned int CLK27M_PASSWORD_I;/* 06 */
	unsigned int CLK27M_PASSWORD_II;/* 07 */
	unsigned int PMC_TIMER2;/* 08 */
	unsigned int reserved[23];/* 9~31 */
};

struct regs_iop_rtc_t {
	unsigned int reserved1[16];/* 00~15 */
	unsigned int rtc_ctrl;/* 16 */
	unsigned int rtc_timer_out;/* 17 */
	unsigned int rtc_divider_cnt_out;/* 18 */
	unsigned int rtc_timer_set;/* 19 */
	unsigned int rtc_alarm_set;/* 20 */
	unsigned int rtc_user_data;/* 21 */
	unsigned int rtc_reset_record;/* 22 */
	unsigned int rtc_batt_charge_ctrl;/* 23 */
	unsigned int rtc_trim_ctrl;/* 24 */
	unsigned int rtc_otp_ctrl;/* 25 */
	unsigned int reserved2[5];/* 26~30 */
	unsigned int rtc_ip_version;/* 31 */
};
#endif /* __REG_IOP_H__ */
