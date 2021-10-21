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
#define R8T49N24X_MIN_FREQ		1000000U
#define R8T49N24X_MAX_FREQ		300000000U

enum clk_r8t49n24x_variant { renesas24x };

static unsigned int __mask_and_shift(unsigned int value, unsigned int mask)
{
	value &= mask;
	return value >> __renesas_bits_to_shift(mask);
}

/**
 * r8t49n24x_set_output_mode - Set the mode for a particular clock
 * output in the register.
 * @reg:	The current register value before setting the mode.
 * @mask:	The bitmask identifying where in the register the
 *		output mode is stored.
 * @mode:	The mode to set.
 *
 * Return: the new register value with the specified mode bits set.
 */
static int r8t49n24x_set_output_mode(u8 reg, u8 mask, u8 mode)
{
	if (((reg & mask) >> __renesas_bits_to_shift(mask)) == OUTPUTMODE_HIGHZ) {
		reg = reg & ~mask;
		reg |= OUTPUTMODE_LVDS << __renesas_bits_to_shift(mask);
	}
	return reg;
}

/**
 * r8t49n24x_read_from_hw - Get the current values on the hw
 * @chip:	Device data structure
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int r8t49n24x_read_from_hw(struct clk_r8t49n24x_chip *chip)
{
	int err;
	u32 tmp = 0, tmp2;
	unsigned int i;
	struct i2c_client *client = chip->i2c_client;

	err = regmap_read(chip->regmap, R8T49N24X_REG_DSM_INT_8, &chip->reg_dsm_int_8);
	if (err) {
		dev_err(&client->dev, "error reading R8T49N24X_REG_DSM_INT_8: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_dsm_int_8: 0x%x", chip->reg_dsm_int_8);

	err = regmap_read(chip->regmap, R8T49N24X_REG_DSMFRAC_20_16_MASK,
			  &chip->reg_dsm_frac_20_16);
	if (err) {
		dev_err(&client->dev, "error reading R8T49N24X_REG_DSMFRAC_20_16_MASK: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_dsm_frac_20_16: 0x%x", chip->reg_dsm_frac_20_16);

	err = regmap_read(chip->regmap, R8T49N24X_REG_OUTEN, &chip->reg_out_en_x);
	if (err) {
		dev_err(&client->dev, "error reading R8T49N24X_REG_OUTEN: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_out_en_x: 0x%x", chip->reg_out_en_x);

	err = regmap_read(chip->regmap, R8T49N24X_REG_OUTMODE0_1, &tmp);
	if (err) {
		dev_err(&client->dev, "error reading R8T49N24X_REG_OUTMODE0_1: %i", err);
		return err;
	}

	tmp2 = r8t49n24x_set_output_mode(tmp, R8T49N24X_REG_OUTMODE0_MASK, OUTPUTMODE_LVDS);
	tmp2 = r8t49n24x_set_output_mode(tmp2, R8T49N24X_REG_OUTMODE1_MASK, OUTPUTMODE_LVDS);
	dev_dbg(&client->dev,
		"reg_out_mode_0_1 original: 0x%x. After OUT0/1 to LVDS if necessary: 0x%x",
		tmp, tmp2);

	chip->reg_out_mode_0_1 = tmp2;
	err = regmap_read(chip->regmap, R8T49N24X_REG_OUTMODE2_3, &tmp);
	if (err) {
		dev_err(&client->dev, "error reading R8T49N24X_REG_OUTMODE2_3: %i", err);
		return err;
	}

	tmp2 = r8t49n24x_set_output_mode(tmp, R8T49N24X_REG_OUTMODE2_MASK, OUTPUTMODE_LVDS);
	tmp2 = r8t49n24x_set_output_mode(tmp2, R8T49N24X_REG_OUTMODE3_MASK, OUTPUTMODE_LVDS);
	dev_dbg(&client->dev,
		"reg_out_mode_2_3 original: 0x%x. After OUT2/3 to LVDS if necessary: 0x%x",
		tmp, tmp2);

	chip->reg_out_mode_2_3 = tmp2;
	err = regmap_read(chip->regmap, R8T49N24X_REG_Q_DIS, &chip->reg_qx_dis);
	if (err) {
		dev_err(&client->dev, "error reading R8T49N24X_REG_Q_DIS: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_qx_dis: 0x%x", chip->reg_qx_dis);

	err = regmap_read(chip->regmap, R8T49N24X_REG_NS1_Q0, &chip->reg_ns1_q0);
	if (err) {
		dev_err(&client->dev, "error reading R8T49N24X_REG_NS1_Q0: %i", err);
		return err;
	}

	dev_dbg(&client->dev, "reg_ns1_q0: 0x%x", chip->reg_ns1_q0);

	for (i = 1; i <= NUM_OUTPUTS; i++) {
		struct clk_register_offsets offsets;

		r8t49n24x_get_offsets(i, &offsets);

		err = regmap_read(chip->regmap, offsets.n_17_16_offset,
				  &chip->reg_n_qx_17_16[i - 1]);
		if (err) {
			dev_err(&client->dev,
				"error reading n_17_16_offset output %d (offset: 0x%x): %i",
				i, offsets.n_17_16_offset, err);
			return err;
		}

		dev_dbg(&client->dev, "reg_n_qx_17_16[Q%u]: 0x%x", i,
			chip->reg_n_qx_17_16[i - 1]);

		err = regmap_read(chip->regmap, offsets.nfrac_27_24_offset,
				  &chip->reg_nfrac_qx_27_24[i - 1]);
		if (err) {
			dev_err(&client->dev,
				"error reading nfrac_27_24_offset output %d (offset: 0x%x): %i",
				i, offsets.nfrac_27_24_offset,
				err);
			return err;
		}

		dev_dbg(&client->dev, "reg_nfrac_qx_27_24[Q%u]: 0x%x", i,
			chip->reg_nfrac_qx_27_24[i - 1]);
	}

	dev_dbg(&client->dev, "initial values read from chip successfully");

	/* Also read DBL_DIS to determine whether the doubler is disabled. */
	err = regmap_read(chip->regmap, R8T49N24X_REG_DBL_DIS, &tmp);
	if (err) {
		dev_err(&client->dev, "error reading R8T49N24X_REG_DBL_DIS: %i", err);
		return err;
	}

	chip->doubler_disabled = __mask_and_shift(tmp, R8T49N24X_REG_DBL_DIS_MASK);
	dev_dbg(&client->dev, "doubler_disabled: %d", chip->doubler_disabled);

	return 0;
}

/**
 * r8t49n24x_set_rate - Sets the specified output clock to the specified rate.
 * @hw:		clk_hw struct that identifies the specific output clock.
 * @rate:	the rate (in Hz) for the specified clock.
 * @parent_rate:(not sure) the rate for a parent signal (e.g.,
 *		the VCO feeding the output)
 *
 * This function will call r8t49n24x_set_frequency, which means it will
 * calculate divider for all requested outputs and update the attached
 * device (issue I2C commands to update the registers).
 *
 * Return: 0 on success.
 */
static int r8t49n24x_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	int err;

	/*
	 * hw->clk is the pointer to the specific output clock the user is
	 * requesting. We use hw to get back to the output structure for
	 * the output clock. Set the requested rate in the output structure.
	 * Note that container_of cannot be used to find the device structure
	 * (clk_r8t49n24x_chip) from clk_hw, because clk_r8t49n24x_chip has an array
	 * of r8t49n24x_output structs. That is why it is necessary to use
	 * output->chip to access the device structure.
	 */
	struct r8t49n24x_output *output = to_r8t49n24x_output(hw);
	struct i2c_client *client = output->chip->i2c_client;

	if (rate < output->chip->min_freq || rate > output->chip->max_freq) {
		dev_err(&client->dev,
			"requested frequency (%luHz) is out of range\n", rate);
		return -EINVAL;
	}

	/*
	 * Set the requested frequency in the output data structure, and then
	 * call r8t49n24x_set_frequency. r8t49n24x_set_frequency considers all
	 * requested frequencies when deciding on a vco frequency and
	 * calculating dividers.
	 */
	output->requested = rate;

	dev_dbg(&client->dev, "calling r8t49n24x_set_frequency for Q%u. rate: %lu",
		output->index, rate);
	err = r8t49n24x_set_frequency(output->chip);
	if (err)
		dev_dbg(&client->dev, "error calling set_frequency: %d", err);

	return err;
}

/**
 * r8t49n24x_determine_rate - get valid rate that is closest to the requested rate
 * @hw:		clk_hw struct that identifies the specific output clock.
 * @req:	the clk rate request struct.
 *
 * Returns the closest rate to the requested rate actually supported by the
 * chip.
 *
 * Return: adjusted rate
 */
static int r8t49n24x_determine_rate(struct clk_hw *hw,
				    struct clk_rate_request *req)
{
	/*
	 * The chip has fractional output dividers, so assume it
	 * can provide the requested rate.
	 *
	 * TODO: figure out the closest rate that chip can support
	 * within a low error threshold and return that rate.
	 */
	return req->rate;
}

/**
 * r8t49n24x_unprepare - disable an output clock.
 * @hw:		clk_hw struct that identifies the specific output clock.
 */
static void r8t49n24x_unprepare(struct clk_hw *hw)
{
	struct r8t49n24x_output *output = to_r8t49n24x_output(hw);

	r8t49n24x_enable_output(output->chip, output->index, false);
}

/**
 * r8t49n24x_prepare - enable an output clock.
 * @hw:		clk_hw struct that identifies the specific output clock.
 *
 * Return: 0 for success.
 */
static int r8t49n24x_prepare(struct clk_hw *hw)
{
	struct r8t49n24x_output *output = to_r8t49n24x_output(hw);

	return r8t49n24x_enable_output(output->chip, output->index, true);
}

/**
 * r8t49n24x_recalc_rate - return the frequency being provided by the clock.
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
static unsigned long r8t49n24x_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct r8t49n24x_output *output = to_r8t49n24x_output(hw);

	return output->requested;
}

/*
 * Note that .prepare and .unprepare appear to be used more in Gates.
 * They do not appear to be necessary for this device.
 * Instead, update the device when .set_rate is called.
 */
static const struct clk_ops r8t49n24x_clk_ops = {
	.recalc_rate = r8t49n24x_recalc_rate,
	.determine_rate = r8t49n24x_determine_rate,
	.set_rate = r8t49n24x_set_rate,
	.prepare = r8t49n24x_prepare,
	.unprepare = r8t49n24x_unprepare,
};

static bool r8t49n24x_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return false;
}

static const struct regmap_config r8t49n24x_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0xffff,
	.volatile_reg = r8t49n24x_regmap_is_volatile,
};

/**
 * r8t49n24x_clk_notifier_cb - Clock rate change callback
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
static int r8t49n24x_clk_notifier_cb(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct clk_r8t49n24x_chip *chip = to_clk_r8t49n24x_from_nb(nb);
	int err;

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
		dev_dbg(&chip->i2c_client->dev, "POST_RATE_CHANGE. Calling r8t49n24x_set_frequency\n");
		err = r8t49n24x_set_frequency(chip);
		if (err)
			dev_dbg(&chip->i2c_client->dev, "error setting frequency (%i)\n", err);
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static struct clk_hw *of_clk_r8t49n24x_get(struct of_phandle_args *clkspec,
					   void *_data)
{
	struct clk_r8t49n24x_chip *chip = _data;
	unsigned int idx = clkspec->args[0];

	if (idx >= ARRAY_SIZE(chip->clk)) {
		pr_err("invalid clock index %u for provider %pOF\n", idx, clkspec->np);
		return ERR_PTR(-EINVAL);
	}

	return &chip->clk[idx].hw;
}

/**
 * r8t49n24x_probe - main entry point for ccf driver
 * @client:	pointer to i2c_client structure
 * @id:		pointer to i2c_device_id structure
 *
 * Main entry point function that gets called to initialize the driver.
 *
 * Return: 0 for success.
 */
static int r8t49n24x_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct clk_r8t49n24x_chip *chip;
	struct clk_init_data init;

	int err, i;
	char buf[6];

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	init.ops = &r8t49n24x_clk_ops;
	init.flags = 0;
	init.num_parents = 0;
	chip->i2c_client = client;

	chip->min_freq = R8T49N24X_MIN_FREQ;
	chip->max_freq = R8T49N24X_MAX_FREQ;

	for (i = 0; i <= NUM_INPUTS; i++) {
		char name[12];

		sprintf(name, i == NUM_INPUTS ? "xtal" : "clk%i", i);
		dev_dbg(&client->dev, "attempting to get %s", name);
		chip->input_clk = devm_clk_get_optional(&client->dev, name);
		if (chip->input_clk) {
			err = 0;
			chip->input_clk_num = i;
			break;
		}
	}

	if (IS_ERR(chip->input_clk)) {
		return dev_err_probe(&client->dev, PTR_ERR(chip->input_clk),
				     "can't get input clock/xtal\n");
	}

	chip->input_clk_freq = clk_get_rate(chip->input_clk);
	dev_dbg(&client->dev, "Frequency from clk in device tree: %uHz", chip->input_clk_freq);

	chip->input_clk_nb.notifier_call = r8t49n24x_clk_notifier_cb;
	if (clk_notifier_register(chip->input_clk, &chip->input_clk_nb))
		dev_warn(&client->dev, "Unable to register clock notifier for input_clk.");

	dev_dbg(&client->dev, "about to read settings: %zu", ARRAY_SIZE(chip->settings));

	err = of_property_read_u8_array(client->dev.of_node, "renesas,settings", chip->settings,
					ARRAY_SIZE(chip->settings));
	if (!err) {
		dev_dbg(&client->dev, "settings property specified in DT");
		chip->has_settings = true;
	} else if (err == -EOVERFLOW) {
		dev_dbg(&client->dev, "EOVERFLOW reading settings. ARRAY_SIZE: %zu",
			ARRAY_SIZE(chip->settings));
			return err;
	} else {
		dev_dbg(&client->dev,
			"settings property missing in DT (or an error that can be ignored: %i).",
			err);
	}

	/*
	 * Requested output frequencies cannot be specified in the DT.
	 * Either a consumer needs to use the clock API to request the rate.
	 * Use clock-names in DT to specify the output clock.
	 */

	chip->regmap = devm_regmap_init_i2c(client, &r8t49n24x_regmap_config);
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
	err = r8t49n24x_read_from_hw(chip);
	if (err)
		return err;

	/* Create all 4 clocks */
	for (i = 0; i < NUM_OUTPUTS; i++) {
		init.name = kasprintf(GFP_KERNEL, "%s.Q%i", client->dev.of_node->name, i);
		chip->clk[i].chip = chip;
		chip->clk[i].hw.init = &init;
		chip->clk[i].index = i;
		err = devm_clk_hw_register(&client->dev, &chip->clk[i].hw);
		kfree(init.name); /* clock framework made a copy of the name */
		if (err) {
			dev_err(&client->dev, "clock registration failed\n");
			return err;
		}
		dev_dbg(&client->dev, "successfully registered Q%i", i);
	}

	err = of_clk_add_hw_provider(client->dev.of_node, of_clk_r8t49n24x_get, chip);
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

static int r8t49n24x_remove(struct i2c_client *client)
{
	struct clk_r8t49n24x_chip *chip = to_clk_r8t49n24x_from_client(&client);

	of_clk_del_provider(client->dev.of_node);

	if (!chip->input_clk)
		clk_notifier_unregister(chip->input_clk, &chip->input_clk_nb);
	return 0;
}

static const struct i2c_device_id r8t49n24x_id[] = {
	 { "8t49n24x", renesas24x },
	 {}
};
MODULE_DEVICE_TABLE(i2c, r8t49n24x_id);

static const struct of_device_id r8t49n24x_of_match[] = {
	{ .compatible = "renesas,8t49n241" },
	{},
};
MODULE_DEVICE_TABLE(of, r8t49n24x_of_match);

static struct i2c_driver r8t49n24x_driver = {
	.driver = {
		.name = "8t49n24x",
		.of_match_table = r8t49n24x_of_match,
	},
	.probe = r8t49n24x_probe,
	.remove = r8t49n24x_remove,
	.id_table = r8t49n24x_id,
};

module_i2c_driver(r8t49n24x_driver);

MODULE_DESCRIPTION("8T49N24x ccf driver");
MODULE_AUTHOR("David Cater <david.cater.jc@renesas.com>");
MODULE_AUTHOR("Alex Helms <alexander.helms.jy@renesas.com>");
MODULE_LICENSE("GPL v2");
