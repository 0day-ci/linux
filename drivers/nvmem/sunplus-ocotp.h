/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SP_OCOTP_FW_H_
#define __SP_OCOTP_FW_H_

/**************************************************************************************************/
/* OTP memory											  */
/* Each bank contains 4 words (32 bits).							  */
/* Bank 0 starts at offset 0 from the base.							  */
/*												  */
/**************************************************************************************************/
#define OTP_WORDS_PER_BANK		4
#define OTP_WORD_SIZE			sizeof(u32)
#define OTP_BIT_ADDR_OF_BANK		(8 * OTP_WORD_SIZE * OTP_WORDS_PER_BANK)

#define QAC628_OTP_NUM_BANKS            8
#define QAC628_OTP_SIZE                 (QAC628_OTP_NUM_BANKS * OTP_WORDS_PER_BANK * OTP_WORD_SIZE)

#define I143_OTP_NUM_BANKS		8
#define I143_OTP_SIZE			(I143_OTP_NUM_BANKS * OTP_WORDS_PER_BANK * OTP_WORD_SIZE)

#define QAK645_EFUSE0_NUM_BANKS		8
#define QAK645_EFUSE0_SIZE		(QAK645_EFUSE0_NUM_BANKS * OTP_WORDS_PER_BANK * \
										OTP_WORD_SIZE)

#define QAK645_EFUSE1_NUM_BANKS		64
#define QAK645_EFUSE1_SIZE		(QAK645_EFUSE1_NUM_BANKS * OTP_WORDS_PER_BANK * \
										OTP_WORD_SIZE)

#define QAK645_EFUSE2_NUM_BANKS		8
#define QAK645_EFUSE2_SIZE		(QAK645_EFUSE2_NUM_BANKS * OTP_WORDS_PER_BANK * \
										OTP_WORD_SIZE)

#define OTP_READ_TIMEOUT                20000

/* HB_GPIO register map */
struct sp_hb_gpio_reg {
	unsigned int hb_gpio_rgst_bus32_1;
	unsigned int hb_gpio_rgst_bus32_2;
	unsigned int hb_gpio_rgst_bus32_3;
	unsigned int hb_gpio_rgst_bus32_4;
	unsigned int hb_gpio_rgst_bus32_5;
	unsigned int hb_gpio_rgst_bus32_6;
	unsigned int hb_gpio_rgst_bus32_7;
	unsigned int hb_gpio_rgst_bus32_8;
	unsigned int hb_gpio_rgst_bus32_9;
	unsigned int hb_gpio_rgst_bus32_10;
	unsigned int hb_gpio_rgst_bus32_11;
	unsigned int hb_gpio_rgst_bus32_12;
};

/* OTPRX register map */
struct sp_otprx_reg {
	unsigned int sw_trim;
	unsigned int set_key;
	unsigned int otp_rsv;
	unsigned int otp_prog_ctl;
	unsigned int otp_prog_addr;
	unsigned int otp_prog_csv;
	unsigned int otp_prog_strobe;
	unsigned int otp_prog_load;
	unsigned int otp_prog_pgenb;
	unsigned int otp_prog_wr;
	unsigned int otp_prog_reg25;
	unsigned int otp_prog_state;
	unsigned int otp_usb_phy_trim;
	unsigned int otp_data2;
	unsigned int otp_prog_ps;
	unsigned int otp_rsv2;
	unsigned int key_srst;
	unsigned int otp_ctrl;
	unsigned int otp_cmd;
	unsigned int otp_cmd_status;
	unsigned int otp_addr;
	unsigned int otp_data;
	unsigned int rtc_reset_record;
	unsigned int rtc_battery_ctrl;
	unsigned int rtc_trim_ctrl;
	unsigned int rsv25;
	unsigned int rsv26;
	unsigned int rsv27;
	unsigned int rsv28;
	unsigned int rsv29;
	unsigned int rsv30;
	unsigned int rsv31;
};

// otp_prog_ctl
#define PIO_MODE                        0x07

// otp_prog_addr
#define PROG_OTP_ADDR                   0x1FFF

// otp_prog_pgenb
#define PIO_PGENB                       0x01

// otp_prog_wr
#define PIO_WR                          0x01

// otp_prog_reg25
#define PROGRAM_OTP_DATA                0xFF00
#define PROGRAM_OTP_DATA_SHIFT          8
#define REG25_PD_MODE_SEL               0x10
#define REG25_POWER_SOURCE_SEL          0x02
#define OTP2REG_PD_N_P                  0x01

// otp_prog_state
#define OTPRSV_CMD3                     0xE0
#define OTPRSV_CMD3_SHIFT               5
#define TSMC_OTP_STATE                  0x1F

// otp_ctrl
#define PROG_OTP_PERIOD                 0xFFE0
#define PROG_OTP_PERIOD_SHIFT           5
#define OTP_EN_R                        0x01

// otp_cmd
#define OTP_RD_PERIOD                   0xFF00
#define OTP_RD_PERIOD_SHIFT             8
#define OTP_READ                        0x04

// otp_cmd_status
#define OTP_READ_DONE                   0x10

// otp_addr
#define RD_OTP_ADDRESS                  0x1FFF

extern int sp_ocotp_probe(struct platform_device *pdev);
extern int sp_ocotp_remove(struct platform_device *pdev);

struct sp_otp_vX_t {
	int size;
};

#endif /* __SP_OCOTP_FW_H_ */

