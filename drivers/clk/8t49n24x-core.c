// SPDX-License-Identifier: GPL-2.0
/* 8t49n24x-core.c - Program 8T49N24x settings via I2C (common code)
 *
 * Copyright (C) 2018, Renesas Electronics America <david.cater.jc@renesas.com>
 */

#include <linux/i2c.h>
#include <linux/regmap.h>

#include "8t49n24x-core.h"

/*
 * In Timing Commander, Q0 is changed from 25MHz to Q0 75MHz, the following
 * changes occur:
 *
 * 2 bytes change in EEPROM data string.
 *
 * DSM_INT R0025[0],R0026[7:0] : 35 => 30
 * NS2_Q0 R0040[7:0],R0041[7:0] : 14 => 4
 *
 * In EEPROM
 * 1. R0026
 * 2. R0041
 *
 * Note that VCO_Frequency (metadata) also changed (3500 =>3000).
 * This reflects a change to DSM_INT.
 *
 * Note that the Timing Commander code has workarounds in the workflow scripts
 * to handle dividers for the 8T49N241 (because the development of that GUI
 * predates chip override functionality). That affects NS1_Qx (x in 1-3)
 * and NS2_Qx. NS1_Qx contains the upper bits of NS_Qx, and NS2_Qx contains
 * the lower bits. That is NOT the case for Q0, though. In that case NS1_Q0
 * is the 1st stage output divider (/5, /6, /4) and NS2_Q0 is the 16-bit
 * second stage (with actual divide being twice the value stored in the
 * register).
 *
 * NS1_Q0 R003F[1:0]
 */

#define R8T49N24X_VCO_MIN			2999997000u
#define R8T49N24X_VCO_MAX			4000004000u
#define R8T49N24X_VCO_OPT			3500000000u
#define R8T49N24X_MIN_INT_DIVIDER	6
#define R8T49N24X_MIN_NS1			4
#define R8T49N24X_MAX_NS1			6

static const u8 q0_ns1_options[3] = { 5, 6, 4 };

/**
 * __renesas_bits_to_shift - num bits to shift given specified mask
 * @mask:	32-bit word input to count zero bits on right
 *
 * Given a bit mask indicating where a value will be stored in
 * a register, return the number of bits you need to shift the value
 * before ORing it into the register value.
 *
 * Return: number of bits to shift
 */
int __renesas_bits_to_shift(unsigned int mask)
{
	if (mask) {
		// ffs considers the first bit position 1, we need an index
		return ffs(mask) - 1;
	} else {
		return 0;
	}
}

int __renesas_i2c_write_bulk(struct i2c_client *client, struct regmap *map,
			     unsigned int reg, u8 val[], size_t val_count)
{
	u8 block[WRITE_BLOCK_SIZE];
	unsigned int block_offset = reg;
	unsigned int i, err, currentOffset = 0;

	dev_dbg(&client->dev,
		"I2C->0x%04x : [hex] . First byte: %02x, Second byte: %02x",
		reg, reg >> 8, reg & 0xFF);

	print_hex_dump_debug("i2c_write_bulk: ", DUMP_PREFIX_NONE,
			     16, 1, val, val_count, false);

	for (i = 0; i < val_count; i++) {
		block[currentOffset++] = val[i];

		if (i > 0 && (i + 1) % WRITE_BLOCK_SIZE == 0) {
			err = regmap_bulk_write(map, block_offset, block, WRITE_BLOCK_SIZE);
			if (err)
				break;
			block_offset += WRITE_BLOCK_SIZE;
			currentOffset = 0;
		}
	}

	if (err == 0 && currentOffset > 0)
		err = regmap_bulk_write(map, block_offset, block, currentOffset);

	return err;
}

static int __i2c_write(struct i2c_client *client, struct regmap *map,
		       unsigned int reg, unsigned int val)
{
	dev_dbg(&client->dev, "I2C->0x%x : [hex] %x", reg, val);
	return regmap_write(map, reg, val);
}

static int __i2c_write_with_mask(struct i2c_client *client, struct regmap *map,
				 unsigned int reg, u8 val, u8 original, u8 mask)
{
	return __i2c_write(client, map, reg,
			   ((val << __renesas_bits_to_shift(mask)) & mask) | (original & ~mask));
}

void r8t49n24x_get_offsets(u8 output_num, struct clk_register_offsets *offsets)
{
	offsets->oe_offset = 0;
	offsets->oe_mask = 0;
	offsets->dis_mask = 0;
	offsets->n_17_16_offset = 0;
	offsets->n_17_16_mask = 0;
	offsets->n_15_8_offset = 0;
	offsets->n_7_0_offset = 0;
	offsets->nfrac_27_24_offset = 0;
	offsets->nfrac_27_24_mask = 0;
	offsets->nfrac_23_16_offset = 0;
	offsets->nfrac_15_8_offset = 0;
	offsets->nfrac_7_0_offset = 0;

	switch (output_num) {
	case 0:
		offsets->oe_offset = R8T49N24X_REG_OUTEN;
		offsets->oe_mask = R8T49N24X_REG_OUTEN0_MASK;
		offsets->dis_mask = R8T49N24X_REG_Q0_DIS_MASK;
		break;
	case 1:
		offsets->oe_offset = R8T49N24X_REG_OUTEN;
		offsets->oe_mask = R8T49N24X_REG_OUTEN1_MASK;
		offsets->dis_mask = R8T49N24X_REG_Q1_DIS_MASK;
		offsets->n_17_16_offset = R8T49N24X_REG_N_Q1_17_16;
		offsets->n_17_16_mask = R8T49N24X_REG_N_Q1_17_16_MASK;
		offsets->n_15_8_offset = R8T49N24X_REG_N_Q1_15_8;
		offsets->n_7_0_offset = R8T49N24X_REG_N_Q1_7_0;
		offsets->nfrac_27_24_offset = R8T49N24X_REG_NFRAC_Q1_27_24;
		offsets->nfrac_27_24_mask = R8T49N24X_REG_NFRAC_Q1_27_24_MASK;
		offsets->nfrac_23_16_offset = R8T49N24X_REG_NFRAC_Q1_23_16;
		offsets->nfrac_15_8_offset = R8T49N24X_REG_NFRAC_Q1_15_8;
		offsets->nfrac_7_0_offset = R8T49N24X_REG_NFRAC_Q1_7_0;
		break;
	case 2:
		offsets->oe_offset = R8T49N24X_REG_OUTEN;
		offsets->oe_mask = R8T49N24X_REG_OUTEN2_MASK;
		offsets->dis_mask = R8T49N24X_REG_Q2_DIS_MASK;
		offsets->n_17_16_offset = R8T49N24X_REG_N_Q2_17_16;
		offsets->n_17_16_mask = R8T49N24X_REG_N_Q2_17_16_MASK;
		offsets->n_15_8_offset = R8T49N24X_REG_N_Q2_15_8;
		offsets->n_7_0_offset = R8T49N24X_REG_N_Q2_7_0;
		offsets->nfrac_27_24_offset = R8T49N24X_REG_NFRAC_Q2_27_24;
		offsets->nfrac_27_24_mask = R8T49N24X_REG_NFRAC_Q2_27_24_MASK;
		offsets->nfrac_23_16_offset = R8T49N24X_REG_NFRAC_Q2_23_16;
		offsets->nfrac_15_8_offset = R8T49N24X_REG_NFRAC_Q2_15_8;
		offsets->nfrac_7_0_offset = R8T49N24X_REG_NFRAC_Q2_7_0;
		break;
	case 3:
		offsets->oe_offset = R8T49N24X_REG_OUTEN;
		offsets->oe_mask = R8T49N24X_REG_OUTEN3_MASK;
		offsets->dis_mask = R8T49N24X_REG_Q3_DIS_MASK;
		offsets->n_17_16_offset = R8T49N24X_REG_N_Q3_17_16;
		offsets->n_17_16_mask = R8T49N24X_REG_N_Q3_17_16_MASK;
		offsets->n_15_8_offset = R8T49N24X_REG_N_Q3_15_8;
		offsets->n_7_0_offset = R8T49N24X_REG_N_Q3_7_0;
		offsets->nfrac_27_24_offset = R8T49N24X_REG_NFRAC_Q3_27_24;
		offsets->nfrac_27_24_mask = R8T49N24X_REG_NFRAC_Q3_27_24_MASK;
		offsets->nfrac_23_16_offset = R8T49N24X_REG_NFRAC_Q3_23_16;
		offsets->nfrac_15_8_offset = R8T49N24X_REG_NFRAC_Q3_15_8;
		offsets->nfrac_7_0_offset = R8T49N24X_REG_NFRAC_Q3_7_0;
		break;
	}
}

/**
 * r8t49n24x_calc_div_q0 - Calculate dividers and VCO freq to generate
 *		the specified Q0 frequency.
 * @chip:	Device data structure. contains all requested frequencies
 *		for all outputs.
 *
 * The actual output divider is ns1 * ns2 * 2. fOutput = fVCO / (ns1 * ns2 * 2)
 *
 * The options for ns1 (when the source is the VCO) are 4,5,6. ns2 is a
 * 16-bit value.
 *
 * chip->divs: structure for specifying ns1/ns2 values. If 0 after this
 * function, Q0 is not requested
 */
static void r8t49n24x_calc_div_q0(struct clk_r8t49n24x_chip *chip)
{
	unsigned int i;
	unsigned int min_div = 0, max_div = 0, best_vco = 0;
	unsigned int min_ns2 = 0, max_ns2 = 0;
	bool is_lower_vco = false;

	chip->divs.ns1_q0 = 0;
	chip->divs.ns2_q0 = 0;

	if (chip->clk[0].requested == 0)
		return;

	min_div = (R8T49N24X_VCO_MIN / (chip->clk[0].requested * 2)) * 2;
	max_div = (R8T49N24X_VCO_MAX / (chip->clk[0].requested * 2)) * 2;

	dev_dbg(&chip->i2c_client->dev,
		"requested: %lu, min_div: %u, max_div: %u",
		chip->clk[0].requested, min_div, max_div);

	min_ns2 = min_div / (R8T49N24X_MAX_NS1 * 2);
	max_ns2 = max_div / (R8T49N24X_MIN_NS1 * 2);

	dev_dbg(&chip->i2c_client->dev, "min_ns2: %u, max_ns2: %u", min_ns2, max_ns2);

	for (i = 0; i < ARRAY_SIZE(q0_ns1_options); i++) {
		unsigned int j = min_ns2;

		while (j <= max_ns2) {
			unsigned int actual_div = q0_ns1_options[i] * j * 2;
			unsigned int current_vco = actual_div * chip->clk[0].requested;

			if (current_vco < R8T49N24X_VCO_MIN)
				dev_dbg(&chip->i2c_client->dev,
					"ignore div: (ns1=%u * ns2=%u * 2 * %lu) == %u < %u",
					q0_ns1_options[i], j,
					chip->clk[0].requested, current_vco,
					R8T49N24X_VCO_MIN);
			else if (current_vco > R8T49N24X_VCO_MAX) {
				dev_dbg(&chip->i2c_client->dev,
					"ignore div: (ns1=%u * ns2=%u * 2 * %lu) == %u > %u. EXIT LOOP.",
					q0_ns1_options[i], j,
					chip->clk[0].requested, current_vco,
					R8T49N24X_VCO_MAX);
				j = max_ns2;
			} else {
				bool use = false;

				dev_dbg(&chip->i2c_client->dev,
					"contender: (ns1=%u * ns2=%u * 2 * %lu) == %u [in range]",
					q0_ns1_options[i], j,
					chip->clk[0].requested, current_vco);
				if (current_vco <= R8T49N24X_VCO_OPT) {
					if (current_vco > best_vco || !is_lower_vco) {
						is_lower_vco = true;
						use = true;
					}
				} else if (!is_lower_vco && current_vco > best_vco) {
					use = true;
				}
				if (use) {
					chip->divs.ns1_q0 = i;
					chip->divs.ns2_q0 = j;
					best_vco = current_vco;
				}
			}
			j++;
		}
	}

	dev_dbg(&chip->i2c_client->dev,
		"best: (ns1=%u [/%u] * ns2=%u * 2 * %lu) == %u",
		chip->divs.ns1_q0, q0_ns1_options[chip->divs.ns1_q0],
		chip->divs.ns2_q0, chip->clk[0].requested, best_vco);
}

/**
 * r8t49n24x_calc_divs - Calculate dividers to generate the specified frequency.
 * @chip:	Device data structure. contains all requested frequencies
 *		for all outputs.
 *
 * Calculate the clock dividers (dsmint, dsmfrac for vco; ns1/ns2 for q0,
 * n/nfrac for q1-3) for a given target frequency.
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int r8t49n24x_calc_divs(struct clk_r8t49n24x_chip *chip)
{
	unsigned int i;
	unsigned int vco = 0;
	unsigned int pfd = 0;
	u64 rem = 0;

	r8t49n24x_calc_div_q0(chip);

	dev_dbg(&chip->i2c_client->dev,
		"after r8t49n24x_calc_div_q0. ns1: %u [/%u], ns2: %u",
		chip->divs.ns1_q0, q0_ns1_options[chip->divs.ns1_q0],
		chip->divs.ns2_q0);

	chip->divs.dsmint = 0;
	chip->divs.dsmfrac = 0;

	if (chip->clk[0].requested > 0) {
		/* Q0 is in use and is governing the actual VCO freq */
		vco = q0_ns1_options[chip->divs.ns1_q0] * chip->divs.ns2_q0
			* 2 * chip->clk[0].requested;
	} else {
		unsigned int freq = 0;
		unsigned int min_div = 0, max_div = 0;
		unsigned int i = 0;
		bool is_lower_vco = false;

		/*
		 * Q0 is not in use. Use the first requested (fractional)
		 * output frequency as the one controlling the VCO.
		 */
		for (i = 1; i < NUM_OUTPUTS; i++) {
			if (chip->clk[i].requested != 0) {
				freq = chip->clk[i].requested;
				break;
			}
		}

		if (!freq) {
			dev_err(&chip->i2c_client->dev, "NO FREQUENCIES SPECIFIED");
			return -EINVAL;
		}

		/*
		 * First, determine the min/max div for the output frequency.
		 */
		min_div = R8T49N24X_MIN_INT_DIVIDER;
		max_div = (R8T49N24X_VCO_MAX / (freq * 2)) * 2;

		dev_dbg(&chip->i2c_client->dev,
			"calc_divs for fractional output. freq: %u, min_div: %u, max_div: %u",
			freq, min_div, max_div);

		i = min_div;

		while (i <= max_div) {
			unsigned int current_vco = freq * i;

			dev_dbg(&chip->i2c_client->dev,
				"calc_divs for fractional output. walk: %u, freq: %u, vco: %u",
				i, freq, vco);

			if (current_vco >= R8T49N24X_VCO_MIN &&
			    vco <= R8T49N24X_VCO_MAX) {
				if (current_vco <= R8T49N24X_VCO_OPT) {
					if (current_vco > vco || !is_lower_vco) {
						is_lower_vco = true;
						vco = current_vco;
					}
				} else if (!is_lower_vco && current_vco > vco) {
					vco = current_vco;
				}
			}
			/* Divider must be even. */
			i += 2;
		}
	}

	if (!vco) {
		dev_err(&chip->i2c_client->dev, "no integer divider in range found. NOT SUPPORTED.");
		return -EINVAL;
	}

	/* Setup dividers for outputs with fractional dividers. */
	for (i = 1; i < NUM_OUTPUTS; i++) {
		if (chip->clk[i].requested) {
			/*
			 * The value written to the chip is half
			 * the calculated divider.
			 */
			chip->divs.nint[i - 1] = div64_u64_rem((u64)vco,
							       chip->clk[i].requested * 2,
							       &rem);
			chip->divs.nfrac[i - 1] = div64_u64(rem << 28,
							    chip->clk[i].requested * 2);

			dev_dbg(&chip->i2c_client->dev,
				"div to get Q%i freq %lu from vco %u: int part: %u, rem: %llu, frac part: %u",
				i, chip->clk[i].requested,
				vco, chip->divs.nint[i - 1], rem,
				chip->divs.nfrac[i - 1]);
		}
	}

	/* Calculate freq for pfd */
	pfd = chip->input_clk_freq * (chip->doubler_disabled ? 1 : 2);

	/*
	 * Calculate dsmint & dsmfrac:
	 * -----------------------------
	 * dsm = float(vco)/float(pfd)
	 * dsmfrac = dsm-floor(dsm) * 2^21
	 * rem = vco % pfd
	 * therefore:
	 * dsmfrac = (rem * 2^21)/pfd
	 */
	chip->divs.dsmint = div64_u64_rem(vco, pfd, &rem);
	chip->divs.dsmfrac = div64_u64(rem << 21, pfd);

	dev_dbg(&chip->i2c_client->dev,
		"vco: %u, pfd: %u, dsmint: %u, dsmfrac: %u, rem: %llu",
		vco, pfd, chip->divs.dsmint,
		chip->divs.dsmfrac, rem);

	return 0;
}

/**
 * r8t49n24x_enable_output - Enable/disable a particular output
 * @chip:	Device data structure
 * @output:	Output to enable/disable
 * @enable:	Enable (true/false)
 *
 * Return: passes on regmap_write return value.
 */
int r8t49n24x_enable_output(struct clk_r8t49n24x_chip *chip, u8 output, bool enable)
{
	int err;
	struct clk_register_offsets offsets;
	struct i2c_client *client = chip->i2c_client;

	/*
	 * When an output is enabled, enable it in the original
	 * data read from the chip and cached. Otherwise it may be
	 * accidentally	turned off when another output is enabled.
	 *
	 * E.g., the driver starts with all outputs off in reg_out_en_x.
	 * Q1 is enabled with the appropriate mask. Q2 is then enabled,
	 * which results in Q1 being turned back off (because Q1 was off
	 * in reg_out_en_x).
	 */

	r8t49n24x_get_offsets(output, &offsets);

	dev_dbg(&client->dev,
		"q%u enable? %d. reg_out_en_x before: 0x%x, reg_out_mode_0_1 before: 0x%x",
		output, enable, chip->reg_out_en_x, chip->reg_out_mode_0_1);

	dev_dbg(&client->dev, "reg_out_mode_2_3 before: 0x%x, reg_qx_dis before: 0x%x",
		chip->reg_out_mode_2_3, chip->reg_qx_dis);

	chip->reg_out_en_x = chip->reg_out_en_x & ~offsets.oe_mask;
	if (enable)
		chip->reg_out_en_x |= BIT(__renesas_bits_to_shift(offsets.oe_mask));

	chip->reg_qx_dis = chip->reg_qx_dis & ~offsets.dis_mask;
	dev_dbg(&client->dev,
		"q%u enable? %d. reg_qx_dis mask: 0x%x, before checking enable: 0x%x",
		output, enable, offsets.dis_mask, chip->reg_qx_dis);

	if (!enable)
		chip->reg_qx_dis |= BIT(__renesas_bits_to_shift(offsets.dis_mask));

	dev_dbg(&client->dev,
		"q%u enable? %d. reg_out_en_x after: 0x%x, reg_qx_dis after: 0x%x",
		output, enable, chip->reg_out_en_x, chip->reg_qx_dis);

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_OUTEN, chip->reg_out_en_x);
	if (err) {
		dev_err(&client->dev, "error setting %s: %i", "R8T49N24X_REG_OUTEN", err);
		return err;
	}

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_OUTMODE0_1, chip->reg_out_mode_0_1);
	if (err) {
		dev_err(&client->dev, "error setting %s: %i", "R8T49N24X_REG_OUTMODE0_1", err);
		return err;
	}

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_OUTMODE2_3, chip->reg_out_mode_2_3);
	if (err) {
		dev_err(&client->dev, "error setting %s: %i", "R8T49N24X_REG_OUTMODE2_3", err);
		return err;
	}

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_Q_DIS, chip->reg_qx_dis);
	if (err) {
		dev_err(&client->dev, "error setting %s: %i", "R8T49N24X_REG_Q_DIS", err);
		return err;
	}

	return 0;
}

/**
 * r8t49n24x_update_device - write registers to the chip
 * @chip:	Device data structure
 *
 * Write all values to hardware that we	have calculated.
 *
 * Return: passes on regmap_bulk_write return value.
 */
static int r8t49n24x_update_device(struct clk_r8t49n24x_chip *chip)
{
	int err;
	unsigned int i;
	struct i2c_client *client = chip->i2c_client;

	dev_dbg(&client->dev, "setting DSM_INT_8 (val %u @ %u)",
		chip->divs.dsmint >> 8, R8T49N24X_REG_DSM_INT_8);

	err = __i2c_write_with_mask(client, chip->regmap, R8T49N24X_REG_DSM_INT_8,
				    (chip->divs.dsmint >> 8) & R8T49N24X_REG_DSM_INT_8_MASK,
				    chip->reg_dsm_int_8, R8T49N24X_REG_DSM_INT_8_MASK);
	if (err) {
		dev_err(&client->dev, "error setting R8T49N24X_REG_DSM_INT_8: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "setting DSM_INT_7_0 (val %u @ 0x%x)",
		chip->divs.dsmint & 0xFF, R8T49N24X_REG_DSM_INT_7_0);

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_DSM_INT_7_0,
			  chip->divs.dsmint & 0xFF);
	if (err) {
		dev_err(&client->dev, "error setting R8T49N24X_REG_DSM_INT_7_0: %i", err);
		return err;
	}

	dev_dbg(&client->dev,
		"setting R8T49N24X_REG_DSMFRAC_20_16 (val %u @ 0x%x)",
		chip->divs.dsmfrac >> 16,
		R8T49N24X_REG_DSMFRAC_20_16);

	err = __i2c_write_with_mask(client, chip->regmap, R8T49N24X_REG_DSMFRAC_20_16,
				    (chip->divs.dsmfrac >> 16) & R8T49N24X_REG_DSMFRAC_20_16_MASK,
				    chip->reg_dsm_int_8, R8T49N24X_REG_DSMFRAC_20_16_MASK);
	if (err) {
		dev_err(&client->dev, "error setting R8T49N24X_REG_DSMFRAC_20_16: %i", err);
		return err;
	}

	dev_dbg(&client->dev,
		"setting R8T49N24X_REG_DSMFRAC_15_8 (val %u @ 0x%x)",
		(chip->divs.dsmfrac >> 8) & 0xFF,
		R8T49N24X_REG_DSMFRAC_15_8);

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_DSMFRAC_15_8,
			  (chip->divs.dsmfrac >> 8) & 0xFF);
	if (err) {
		dev_err(&client->dev, "error setting R8T49N24X_REG_DSMFRAC_15_8: %i", err);
		return err;
	}

	dev_dbg(&client->dev,
		"setting R8T49N24X_REG_DSMFRAC_7_0 (val %u @ 0x%x)",
		chip->divs.dsmfrac & 0xFF,
		R8T49N24X_REG_DSMFRAC_7_0);

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_DSMFRAC_7_0,
			  chip->divs.dsmfrac & 0xFF);
	if (err) {
		dev_err(&client->dev, "error setting R8T49N24X_REG_DSMFRAC_7_0: %i", err);
		return err;
	}

	dev_dbg(&client->dev,
		"setting R8T49N24X_REG_NS1_Q0 (val %u @ 0x%x)",
		chip->divs.ns1_q0, R8T49N24X_REG_NS1_Q0);

	err = __i2c_write_with_mask(client, chip->regmap, R8T49N24X_REG_NS1_Q0,
				    chip->divs.ns1_q0 & R8T49N24X_REG_NS1_Q0_MASK,
				    chip->reg_ns1_q0, R8T49N24X_REG_NS1_Q0_MASK);
	if (err) {
		dev_err(&client->dev, "error setting R8T49N24X_REG_NS1_Q0: %i", err);
		return err;
	}

	dev_dbg(&client->dev,
		"setting R8T49N24X_REG_NS2_Q0_15_8 (val %u @ 0x%x)",
		(chip->divs.ns2_q0 >> 8) & 0xFF, R8T49N24X_REG_NS2_Q0_15_8);

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_NS2_Q0_15_8,
			  (chip->divs.ns2_q0 >> 8) & 0xFF);
	if (err) {
		dev_err(&client->dev, "error setting R8T49N24X_REG_NS2_Q0_15_8: %i", err);
		return err;
	}

	dev_dbg(&client->dev,
		"setting R8T49N24X_REG_NS2_Q0_7_0 (val %u @ 0x%x)",
		chip->divs.ns2_q0 & 0xFF, R8T49N24X_REG_NS2_Q0_7_0);

	err = __i2c_write(client, chip->regmap, R8T49N24X_REG_NS2_Q0_7_0,
			  chip->divs.ns2_q0 & 0xFF);
	if (err) {
		dev_err(&client->dev, "error setting R8T49N24X_REG_NS2_Q0_7_0: %i", err);
		return err;
	}

	dev_dbg(&client->dev,
		"calling r8t49n24x_enable_output for Q0. requestedFreq: %lu",
		chip->clk[0].requested);
	r8t49n24x_enable_output(chip, 0, chip->clk[0].requested != 0);

	dev_dbg(&client->dev, "writing values for q1-q3");
	for (i = 1; i < NUM_OUTPUTS; i++) {
		struct clk_register_offsets offsets;

		if (chip->clk[i].requested != 0) {
			r8t49n24x_get_offsets(i, &offsets);

			dev_dbg(&client->dev, "(q%u, nint: %u, nfrac: %u)",
				i, chip->divs.nint[i - 1],
				chip->divs.nfrac[i - 1]);

			dev_dbg(&client->dev,
				"setting n_17_16_offset (q%u, val %u @ 0x%x)",
				i, chip->divs.nint[i - 1] >> 16,
				offsets.n_17_16_offset);

			err = __i2c_write_with_mask(client, chip->regmap,
						    offsets.n_17_16_offset,
						    (chip->divs.nint[i - 1] >> 16) &
							offsets.n_17_16_mask,
						    chip->reg_n_qx_17_16[i - 1],
						    offsets.n_17_16_mask);
			if (err) {
				dev_err(&client->dev, "error setting n_17_16_offset: %i", err);
				return err;
			}

			dev_dbg(&client->dev,
				"setting n_15_8_offset (q%u, val %u @ 0x%x)",
				i, (chip->divs.nint[i - 1] >> 8) & 0xFF,
				offsets.n_15_8_offset);

			err = __i2c_write(client, chip->regmap,
					  offsets.n_15_8_offset,
					  (chip->divs.nint[i - 1] >> 8) & 0xFF);
			if (err) {
				dev_err(&client->dev, "error setting n_15_8_offset: %i", err);
				return err;
			}

			dev_dbg(&client->dev,
				"setting n_7_0_offset (q%u, val %u @ 0x%x)",
				i, chip->divs.nint[i - 1] & 0xFF,
				offsets.n_7_0_offset);

			err = __i2c_write(client, chip->regmap,
					  offsets.n_7_0_offset,
					  chip->divs.nint[i - 1] & 0xFF);
			if (err) {
				dev_err(&client->dev, "error setting n_7_0_offset: %i", err);
				return err;
			}

			dev_dbg(&client->dev,
				"setting nfrac_27_24_offset (q%u, val %u @ 0x%x)",
				i, (chip->divs.nfrac[i - 1] >> 24),
				offsets.nfrac_27_24_offset);

			err = __i2c_write_with_mask(client, chip->regmap,
						    offsets.nfrac_27_24_offset,
						    (chip->divs.nfrac[i - 1] >> 24) &
							offsets.nfrac_27_24_mask,
						    chip->reg_nfrac_qx_27_24[i - 1],
						    offsets.nfrac_27_24_mask);
			if (err) {
				dev_err(&client->dev, "error setting nfrac_27_24_offset: %i", err);
				return err;
			}

			dev_dbg(&client->dev,
				"setting nfrac_23_16_offset (q%u, val %u @ 0x%x)",
				i, (chip->divs.nfrac[i - 1] >> 16) & 0xFF,
				offsets.nfrac_23_16_offset);

			err = __i2c_write(client, chip->regmap,
					  offsets.nfrac_23_16_offset,
					  (chip->divs.nfrac[i - 1] >> 16) & 0xFF);
			if (err) {
				dev_err(&client->dev, "error setting nfrac_23_16_offset: %i", err);
				return err;
			}

			dev_dbg(&client->dev,
				"setting nfrac_15_8_offset (q%u, val %u @ 0x%x)",
				i, (chip->divs.nfrac[i - 1] >> 8) & 0xFF,
				offsets.nfrac_15_8_offset);

			err = __i2c_write(client, chip->regmap, offsets.nfrac_15_8_offset,
					  (chip->divs.nfrac[i - 1] >> 8) & 0xFF);
			if (err) {
				dev_err(&client->dev, "error setting nfrac_15_8_offset: %i", err);
				return err;
			}

			dev_dbg(&client->dev,
				"setting nfrac_7_0_offset (q%u, val %u @ 0x%x)",
				i, chip->divs.nfrac[i - 1] & 0xFF,
				offsets.nfrac_7_0_offset);

			err = __i2c_write(client, chip->regmap,
					  offsets.nfrac_7_0_offset,
					  chip->divs.nfrac[i - 1] & 0xFF);
			if (err) {
				dev_err(&client->dev, "error setting nfrac_7_0_offset: %i", err);
				return err;
			}
		}
		r8t49n24x_enable_output(chip, i, chip->clk[i].requested != 0);
		chip->clk[i].actual = chip->clk[i].requested;
	}
	return 0;
}

/**
 * r8t49n24x_set_frequency - Adjust output frequency on the attached chip.
 * @chip:	Device data structure, including all requested frequencies.
 *
 * Return: 0 on success.
 */
int r8t49n24x_set_frequency(struct clk_r8t49n24x_chip *chip)
{
	unsigned int i;
	int err;
	bool all_disabled = true;
	struct i2c_client *client = chip->i2c_client;

	for (i = 0; i < NUM_OUTPUTS; i++) {
		if (chip->clk[i].requested == 0) {
			r8t49n24x_enable_output(chip, i, false);
			chip->clk[i].actual = 0;
		} else {
			all_disabled = false;
		}
	}

	if (all_disabled)
		/*
		 * no requested frequencies, so nothing else to calculate
		 * or write to the chip. If the consumer wants to disable
		 * all outputs, they can request 0 for all frequencies.
		 */
		return 0;

	if (chip->input_clk_freq == 0) {
		dev_err(&client->dev, "no input frequency; can't continue.");
		return -EINVAL;
	}

	err = r8t49n24x_calc_divs(chip);
	if (err)
		return err;

	err = r8t49n24x_update_device(chip);
	if (err)
		return err;

	return 0;
}
