// SPDX-License-Identifier: GPL-2.0
/* 8t49n24x.c - Program 8T49N24x settings via I2C.
 *
 * Copyright (C) 2018, Renesas Electronics America <david.cater.jc@renesas.com>
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "8t49n24x-core.h"

#define OUTPUTMODE_HIGHZ		0
#define OUTPUTMODE_LVDS			2
#define RENESAS24X_MIN_FREQ		1000000L
#define RENESAS24X_MAX_FREQ		300000000L

enum clk_renesas24x_variant { renesas24x };

static u32 __mask_and_shift(u32 value, u8 mask)
{
	value &= mask;
	return value >> __renesas_bits_to_shift(mask);
}

/**
 * renesas24x_set_output_mode - Set the mode for a particular clock
 * output in the register.
 * @reg:	The current register value before setting the mode.
 * @mask:	The bitmask identifying where in the register the
 *		output mode is stored.
 * @mode:	The mode to set.
 *
 * Return: the new register value with the specified mode bits set.
 */
static int renesas24x_set_output_mode(u32 reg, u8 mask, u8 mode)
{
	if (((reg & mask) >> __renesas_bits_to_shift(mask)) == OUTPUTMODE_HIGHZ) {
		reg = reg & ~mask;
		reg |= OUTPUTMODE_LVDS << __renesas_bits_to_shift(mask);
	}
	return reg;
}

/**
 * renesas24x_read_from_hw - Get the current values on the hw
 * @chip:	Device data structure
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int renesas24x_read_from_hw(struct clk_renesas24x_chip *chip)
{
	int err = 0;
	u32 tmp = 0, tmp2 = 0;
	u8 output = 0;
	struct i2c_client *client = chip->i2c_client;

	err = regmap_read(chip->regmap, RENESAS24X_REG_DSM_INT_8, &chip->reg_dsm_int_8);
	if (err) {
		dev_err(&client->dev, "error reading RENESAS24X_REG_DSM_INT_8: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_dsm_int_8: 0x%x", chip->reg_dsm_int_8);

	err = regmap_read(chip->regmap, RENESAS24X_REG_DSMFRAC_20_16_MASK,
			  &chip->reg_dsm_frac_20_16);
	if (err) {
		dev_err(&client->dev, "error reading RENESAS24X_REG_DSMFRAC_20_16_MASK: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_dsm_frac_20_16: 0x%x", chip->reg_dsm_frac_20_16);

	err = regmap_read(chip->regmap, RENESAS24X_REG_OUTEN, &chip->reg_out_en_x);
	if (err) {
		dev_err(&client->dev, "error reading RENESAS24X_REG_OUTEN: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_out_en_x: 0x%x", chip->reg_out_en_x);

	err = regmap_read(chip->regmap, RENESAS24X_REG_OUTMODE0_1, &tmp);
	if (err) {
		dev_err(&client->dev, "error reading RENESAS24X_REG_OUTMODE0_1: %i", err);
		return err;
	}

	tmp2 = renesas24x_set_output_mode(tmp, RENESAS24X_REG_OUTMODE0_MASK, OUTPUTMODE_LVDS);
	tmp2 = renesas24x_set_output_mode(tmp2, RENESAS24X_REG_OUTMODE1_MASK, OUTPUTMODE_LVDS);
	dev_dbg(&client->dev,
		"reg_out_mode_0_1 original: 0x%x. After OUT0/1 to LVDS if necessary: 0x%x",
		tmp, tmp2);

	chip->reg_out_mode_0_1 = tmp2;
	err = regmap_read(chip->regmap, RENESAS24X_REG_OUTMODE2_3, &tmp);
	if (err) {
		dev_err(&client->dev, "error reading RENESAS24X_REG_OUTMODE2_3: %i", err);
		return err;
	}

	tmp2 = renesas24x_set_output_mode(tmp, RENESAS24X_REG_OUTMODE2_MASK, OUTPUTMODE_LVDS);
	tmp2 = renesas24x_set_output_mode(tmp2, RENESAS24X_REG_OUTMODE3_MASK, OUTPUTMODE_LVDS);
	dev_dbg(&client->dev,
		"reg_out_mode_2_3 original: 0x%x. After OUT2/3 to LVDS if necessary: 0x%x",
		tmp, tmp2);

	chip->reg_out_mode_2_3 = tmp2;
	err = regmap_read(chip->regmap, RENESAS24X_REG_Q_DIS, &chip->reg_qx_dis);
	if (err) {
		dev_err(&client->dev, "error reading RENESAS24X_REG_Q_DIS: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_qx_dis: 0x%x", chip->reg_qx_dis);

	err = regmap_read(chip->regmap, RENESAS24X_REG_NS1_Q0, &chip->reg_ns1_q0);
	if (err) {
		dev_err(&client->dev, "error reading RENESAS24X_REG_NS1_Q0: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_ns1_q0: 0x%x", chip->reg_ns1_q0);

	for (output = 1; output <= 3; output++) {
		struct clk_register_offsets offsets;

		err = renesas24x_get_offsets(output, &offsets);
		if (err) {
			dev_err(&client->dev, "error calling renesas24x_get_offsets: %i", err);
			return err;
		}

		err = regmap_read(chip->regmap, offsets.n_17_16_offset,
				  &chip->reg_n_qx_17_16[output - 1]);
		if (err) {
			dev_err(&client->dev,
				"error reading n_17_16_offset output %d (offset: 0x%x): %i",
				output, offsets.n_17_16_offset, err);
			return err;
		}

		dev_dbg(&client->dev, "reg_n_qx_17_16[Q%u]: 0x%x", output,
			chip->reg_n_qx_17_16[output - 1]);

		err = regmap_read(chip->regmap, offsets.nfrac_27_24_offset,
				  &chip->reg_nfrac_qx_27_24[output - 1]);
		if (err) {
			dev_err(&client->dev,
				"error reading nfrac_27_24_offset output %d (offset: 0x%x): %i",
				output, offsets.nfrac_27_24_offset,
				err);
			return err;
		}

		dev_dbg(&client->dev, "reg_nfrac_qx_27_24[Q%u]: 0x%x", output,
			chip->reg_nfrac_qx_27_24[output - 1]);
	}

	dev_info(&client->dev, "initial values read from chip successfully");

	/* Also read DBL_DIS to determine whether the doubler is disabled. */
	err = regmap_read(chip->regmap, RENESAS24X_REG_DBL_DIS, &tmp);
	if (err) {
		dev_err(&client->dev, "error reading RENESAS24X_REG_DBL_DIS: %i", err);
		return err;
	}

	chip->doubler_disabled = __mask_and_shift(tmp, RENESAS24X_REG_DBL_DIS_MASK);
	dev_dbg(&client->dev, "doubler_disabled: %d", chip->doubler_disabled);

	return 0;
}

/**
 * renesas24x_set_rate - Sets the specified output clock to the specified rate.
 * @hw:		clk_hw struct that identifies the specific output clock.
 * @rate:	the rate (in Hz) for the specified clock.
 * @parent_rate:(not sure) the rate for a parent signal (e.g.,
 *		the VCO feeding the output)
 *
 * This function will call renesas24x_set_frequency, which means it will
 * calculate divider for all requested outputs and update the attached
 * device (issue I2C commands to update the registers).
 *
 * Return: 0 on success.
 */
static int renesas24x_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	int err = 0;

	/*
	 * hw->clk is the pointer to the specific output clock the user is
	 * requesting. We use hw to get back to the output structure for
	 * the output clock. Set the requested rate in the output structure.
	 * Note that container_of cannot be used to find the device structure
	 * (clk_renesas24x_chip) from clk_hw, because clk_renesas24x_chip has an array
	 * of renesas24x_output structs. That is why it is necessary to use
	 * output->chip to access the device structure.
	 */
	struct renesas24x_output *output = to_renesas24x_output(hw);
	struct i2c_client *client = output->chip->i2c_client;

	if (rate < output->chip->min_freq || rate > output->chip->max_freq) {
		dev_err(&client->dev,
			"requested frequency (%luHz) is out of range\n", rate);
		return -EINVAL;
	}

	/*
	 * Set the requested frequency in the output data structure, and then
	 * call renesas24x_set_frequency. renesas24x_set_frequency considers all
	 * requested frequencies when deciding on a vco frequency and
	 * calculating dividers.
	 */
	output->requested = rate;

	dev_info(&client->dev, "calling renesas24x_set_frequency for Q%u. rate: %lu",
		 output->index, rate);
	err = renesas24x_set_frequency(output->chip);
	if (err)
		dev_err(&client->dev, "error calling set_frequency: %d", err);

	return err;
}

/**
 * renesas24x_round_rate - get valid rate that is closest to the requested rate
 * @hw:		clk_hw struct that identifies the specific output clock.
 * @rate:	the rate (in Hz) for the specified clock.
 * @parent_rate:(not sure) the rate for a parent signal (e.g., the VCO
 *		feeding the output). This is an i/o param.
 *		If the driver supports a parent clock for the output (e.g.,
 *		the VCO(?), then set this param to indicate what the rate of
 *		the parent would be (e.g., the VCO frequency) if the rounded
 *		rate is used.
 *
 * Returns the closest rate to the requested rate actually supported by the
 * chip.
 *
 * Return: adjusted rate
 */
static long renesas24x_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *parent_rate)
{
	/*
	 * The chip has fractional output dividers, so assume it
	 * can provide the requested rate.
	 *
	 * TODO: figure out the closest rate that chip can support
	 * within a low error threshold and return that rate.
	 */
	return rate;
}

/**
 * renesas24x_recalc_rate - return the frequency being provided by the clock.
 * @hw:			clk_hw struct that identifies the specific output clock.
 * @parent_rate:	(not sure) the rate for a parent signal (e.g., the
 *			VCO feeding the output)
 *
 * This API appears to be used to read the current values from the hardware
 * and report the frequency being provided by the clock. Without this function,
 * the clock will be initialized to 0 by default. The OS appears to be
 * calling this to find out what the current value of the clock is at
 * startup, so it can determine when .set_rate is actually changing the
 * frequency.
 *
 * Return: the frequency of the specified clock.
 */
static unsigned long renesas24x_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct renesas24x_output *output = to_renesas24x_output(hw);

	return output->requested;
}

/*
 * Note that .prepare and .unprepare appear to be used more in Gates.
 * They do not appear to be necessary for this device.
 * Instead, update the device when .set_rate is called.
 */
static const struct clk_ops renesas24x_clk_ops = {
	.recalc_rate = renesas24x_recalc_rate,
	.round_rate = renesas24x_round_rate,
	.set_rate = renesas24x_set_rate,
};

static bool renesas24x_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return false;
}

static bool renesas24x_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config renesas24x_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0xffff,
	.writeable_reg = renesas24x_regmap_is_writeable,
	.volatile_reg = renesas24x_regmap_is_volatile,
};

/**
 * renesas24x_clk_notifier_cb - Clock rate change callback
 * @nb:		Pointer to notifier block
 * @event:	Notification reason
 * @data:	Pointer to notification data object
 *
 * This function is called when the input clock frequency changes.
 * The callback checks whether a valid bus frequency can be generated after the
 * change. If so, the change is acknowledged, otherwise the change is aborted.
 * New dividers are written to the HW in the pre- or post change notification
 * depending on the scaling direction.
 *
 * Return:	NOTIFY_STOP if the rate change should be aborted, NOTIFY_OK
 *		to acknowledge the change, NOTIFY_DONE if the notification is
 *		considered irrelevant.
 */
static int renesas24x_clk_notifier_cb(struct notifier_block *nb,
				      unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct clk_renesas24x_chip *chip = to_clk_renesas24x_from_nb(nb);
	int err = 0;

	dev_info(&chip->i2c_client->dev, "input changed: %lu Hz. event: %lu",
		 ndata->new_rate, event);

	switch (event) {
	case PRE_RATE_CHANGE: {
		dev_dbg(&chip->i2c_client->dev, "PRE_RATE_CHANGE\n");
		return NOTIFY_OK;
	}
	case POST_RATE_CHANGE:
		chip->input_clk_freq = ndata->new_rate;
		/*
		 * Can't call clock API clk_set_rate here; I believe
		 * it will be ignored if the rate is the same as we
		 * set previously. Need to call our internal function.
		 */
		dev_dbg(&chip->i2c_client->dev, "POST_RATE_CHANGE. Calling renesas24x_set_frequency\n");
		err = renesas24x_set_frequency(chip);
		if (err)
			dev_err(&chip->i2c_client->dev, "error setting frequency (%i)\n", err);
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static struct clk_hw *of_clk_renesas24x_get(struct of_phandle_args *clkspec,
					    void *_data)
{
	struct clk_renesas24x_chip *chip = _data;
	unsigned int idx = clkspec->args[0];

	if (idx >= ARRAY_SIZE(chip->clk)) {
		pr_err("invalid index %u\n", idx);
		return ERR_PTR(-EINVAL);
	}

	return &chip->clk[idx].hw;
}

/**
 * renesas24x_probe - main entry point for ccf driver
 * @client:	pointer to i2c_client structure
 * @id:		pointer to i2c_device_id structure
 *
 * Main entry point function that gets called to initialize the driver.
 *
 * Return: 0 for success.
 */
static int renesas24x_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct clk_renesas24x_chip *chip;
	struct clk_init_data init;

	int err = 0, x = 0;
	char buf[6];

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	init.ops = &renesas24x_clk_ops;
	init.flags = 0;
	init.num_parents = 0;
	chip->i2c_client = client;

	chip->min_freq = RENESAS24X_MIN_FREQ;
	chip->max_freq = RENESAS24X_MAX_FREQ;

	for (x = 0; x < NUM_INPUTS + 1; x++) {
		char name[12];

		sprintf(name, x == NUM_INPUTS ? "input-xtal" : "input-clk%i", x);
		dev_dbg(&client->dev, "attempting to get %s", name);
		chip->input_clk = devm_clk_get(&client->dev, name);
		if (IS_ERR(chip->input_clk)) {
			err = PTR_ERR(chip->input_clk);
			/*
			 * TODO: Handle EPROBE_DEFER error, which indicates
			 * that the input_clk isn't available now but may be
			 * later when the appropriate module is loaded.
			 */
		} else {
			err = 0;
			chip->input_clk_num = x;
			break;
		}
	}

	if (err) {
		dev_err(&client->dev, "Unable to get input clock, error %d", err);
		chip->input_clk = NULL;
		return err;
	}

	chip->input_clk_freq = clk_get_rate(chip->input_clk);
	dev_dbg(&client->dev, "Got input-freq from input-clk in device tree: %uHz",
		chip->input_clk_freq);

	chip->input_clk_nb.notifier_call = renesas24x_clk_notifier_cb;
	if (clk_notifier_register(chip->input_clk, &chip->input_clk_nb))
		dev_warn(&client->dev, "Unable to register clock notifier for input_clk.");

	dev_dbg(&client->dev, "about to read settings: %zu", ARRAY_SIZE(chip->settings));

	err = of_property_read_u8_array(client->dev.of_node, "settings", chip->settings,
					ARRAY_SIZE(chip->settings));
	if (!err) {
		dev_dbg(&client->dev, "settings property specified in DT");
		chip->has_settings = true;
	} else {
		if (err == -EOVERFLOW) {
			dev_alert(&client->dev, "EOVERFLOW reading settings. ARRAY_SIZE: %zu",
				  ARRAY_SIZE(chip->settings));
			return err;
		}
		dev_dbg(&client->dev,
			"settings property missing in DT (or an error that can be ignored: %i).",
			err);
	}

	/*
	 * Requested output frequencies cannot be specified in the DT.
	 * Either a consumer needs to use the clock API to request the rate.
	 * Use clock-names in DT to specify the output clock.
	 */

	chip->regmap = devm_regmap_init_i2c(client, &renesas24x_regmap_config);
	if (IS_ERR(chip->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(chip->regmap);
	}

	dev_dbg(&client->dev, "call i2c_set_clientdata");
	i2c_set_clientdata(client, chip);

	if (chip->has_settings) {
		/*
		 * A raw settings array was specified in the DT. Write the
		 * settings to the device immediately.
		 */
		err = __renesas_i2c_write_bulk(chip->i2c_client, chip->regmap, 0, chip->settings,
					       ARRAY_SIZE(chip->settings));
		if (err) {
			dev_err(&client->dev, "error writing all settings to chip (%i)\n", err);
			return err;
		}
		dev_dbg(&client->dev, "successfully wrote full settings array");
	}

	/*
	 * Whether or not settings were written to the device, read all
	 * current values from the hw.
	 */
	dev_dbg(&client->dev, "read from HW");
	err = renesas24x_read_from_hw(chip);
	if (err) {
		dev_err(&client->dev, "failed calling renesas24x_read_from_hw (%i)\n", err);
		return err;
	}

	/* Create all 4 clocks */
	for (x = 0; x < NUM_OUTPUTS; x++) {
		init.name = kasprintf(GFP_KERNEL, "%s.Q%i", client->dev.of_node->name, x);
		chip->clk[x].chip = chip;
		chip->clk[x].hw.init = &init;
		chip->clk[x].index = x;
		err = devm_clk_hw_register(&client->dev, &chip->clk[x].hw);
		kfree(init.name); /* clock framework made a copy of the name */
		if (err) {
			dev_err(&client->dev, "clock registration failed\n");
			return err;
		}
		dev_dbg(&client->dev, "successfully registered Q%i", x);
	}

	if (err) {
		dev_err(&client->dev, "clock registration failed\n");
		return err;
	}

	err = of_clk_add_hw_provider(client->dev.of_node, of_clk_renesas24x_get, chip);
	if (err) {
		dev_err(&client->dev, "unable to add clk provider\n");
		return err;
	}

	if (chip->input_clk_num == NUM_INPUTS)
		sprintf(buf, "XTAL");
	else
		sprintf(buf, "CLK%i", chip->input_clk_num);

	dev_info(&client->dev,
		 "probe success. input freq: %uHz (%s), settings string? %s\n",
		 chip->input_clk_freq, buf,
		 chip->has_settings ? "true" : "false");

	return 0;
}

static int renesas24x_remove(struct i2c_client *client)
{
	struct clk_renesas24x_chip *chip = to_clk_renesas24x_from_client(&client);

	of_clk_del_provider(client->dev.of_node);

	if (!chip->input_clk)
		clk_notifier_unregister(chip->input_clk, &chip->input_clk_nb);
	return 0;
}

static const struct i2c_device_id renesas24x_id[] = {
	 { "8t49n24x", renesas24x },
	 {}
};
MODULE_DEVICE_TABLE(i2c, renesas24x_id);

static const struct of_device_id renesas24x_of_match[] = {
	{ .compatible = "renesas,8t49n241" },
	{},
};
MODULE_DEVICE_TABLE(of, renesas24x_of_match);

static struct i2c_driver renesas24x_driver = {
	.driver = {
		.name = "8t49n24x",
		.of_match_table = renesas24x_of_match,
	},
	.probe = renesas24x_probe,
	.remove = renesas24x_remove,
	.id_table = renesas24x_id,
};

module_i2c_driver(renesas24x_driver);

MODULE_DESCRIPTION("8T49N24x ccf driver");
MODULE_AUTHOR("David Cater <david.cater.jc@renesas.com>");
MODULE_AUTHOR("Alex Helms <alexander.helms.jy@renesas.com>");
MODULE_LICENSE("GPL v2");
