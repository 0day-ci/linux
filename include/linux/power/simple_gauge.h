/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 ROHM Semiconductors */

#ifndef POWER_SW_GAUGE_H
#define POWER_SW_GAUGE_H

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define SW_GAUGE_FULL BIT(0)
#define SW_GAUGE_RELAX BIT(1)
#define SW_GAUGE_MAY_BE_LOW BIT(2)
#define SW_GAUGE_CLAMP_SOC BIT(3)

/* Power supply properties handled by simple_gauge */
static const enum power_supply_property SIMPLE_GAUGE_PROPS[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_TEMP
};
#define SIMPLE_GAUGE_PROP_SIZE (sizeof(SIMPLE_GAUGE_PROPS))
#define NUM_SIMPLE_GAUGE_PROPS (ARRAY_SIZE(SIMPLE_GAUGE_PROPS))

struct simple_gauge;

/**
 * struct simple_gauge_ops - fuel-gauge operations
 *
 * @is_relaxed:		return true if battery is at relaxed state. Update
 *			rex_volt to contain measured relaxed battery voltage.
 * @get_temp:		return the battery temperature in tenths of a degree C.
 * @get_uah_from_full:	some chargers can provide CC value change since battery
 *			was last charged full. This value can be used by
 *			sw-gauge when correcting CC based on battery full
 *			status. This function should return charge lost since
 *			battery was last load full. Units in uAh.
 * @get_uah:		return current charge as measured by coulomb counter in
 *			uAh
 * @update_cc_uah:	Update CC by given charge in uAh
 * @get_cycle:		get battery cycle for age compensation
 * @set_cycle:		some batteries/chargers rely on user-space to store the
 *			cycle infomration over reset. Those drivers can
 *			implement the set_cycle callback which user-space can
 *			use to set the stored battery cycle after reset.
 * @get_sys:		get the current system voltage in uV. Used for
 *			IC specific low-voltage SOC correction.
 * @get_soc_by_ocv:	setups which do not store the OCV/SOC information in
 *			standard battery_info can implement this function to
 *			compute SOC based on OCV. SOC should be returned as
 *			units of 0.1%
 * @get_ocv_by_soc:	setups which do not store the OCV/SOC information in
 *			standard battery_info can implement this function to
 *			compute OCV based on SOC. NOTE: Soc is provided to
 *			the function in units of 0.1% to improve accuracy.
 * @age_correct_cap:	batteries/devices with more complicated aging
 *			correction than constant uAh times battery cycles
 *			can implement this to adjust capacity based on battery
 *			cycles. For constant aging use degrade_cycle_uah
 *			in desc.
 * @temp_correct_cap:	batteries/devices with more complicated temperature
 *			correction than ranges of temperatures with constant
 *			change uah/degree C can implement this to adjust
 *			capacity based on battery temperature.
 *			For temperature ranges with constant change uAh/degree
 *			use temp_dgr and amount_of_temp_dgr at desc.
 * @calibrate:		many devices implement coulomb counter calibration
 *			(for example by measuring ADC offset pins shorted).
 *			Such devices can implement this function for periodical
 *			calibration.
 * @suspend_calibrate:	Many small capacity battery devices or devices which
 *			spend long time MCU suspended can benefit from
 *			starting the calibration when entering to suspend. Such
 *			devices can implement this callback to initiaite
 *			calibration when entering to suspend
 * @zero_cap_adjust: IC specific SOC estimation adjustment to be performed
 *			when battery is approaching empty.
 */
struct simple_gauge_ops {
	/*
	 * Get battery relax - could probably also use PSY class state if
	 * it was extended with some properties like BATTERY_RELAXED to know
	 * if OCV can be used.
	 *
	 * Currently meaningfull states are charging/discharging/full/relaxed
	 * Full so we can correct battery capacity and/or CC
	 * Relax so we know we can use OCV
	 */
	bool (*is_relaxed)(struct simple_gauge *gauge, int *rex_volt);
	int (*get_temp)(struct simple_gauge *gauge, int *temp);
	int (*get_uah_from_full)(struct simple_gauge *gauge, int *uah);
	int (*get_uah)(struct simple_gauge *gauge, int *uah);
	int (*update_cc_uah)(struct simple_gauge *gauge, int bcap);
	int (*get_cycle)(struct simple_gauge *gauge, int *cycle);
	int (*set_cycle)(struct simple_gauge *gauge, int old, int *new_cycle);
	int (*get_vsys)(struct simple_gauge *gauge, int *uv);
	int (*get_soc_by_ocv)(struct simple_gauge *sw, int ocv, int temp, int *soc);
	int (*get_ocv_by_soc)(struct simple_gauge *sw, int soc, int temp, int *ocv);
	int (*age_correct_cap)(struct simple_gauge *gauge, int cycle, int *cap);
	int (*temp_correct_cap)(struct simple_gauge *gauge, int *cap, int temp);
	int (*calibrate)(struct simple_gauge *sw);
	int (*suspend_calibrate)(struct simple_gauge *sw, bool start);
	int (*zero_cap_adjust)(struct simple_gauge *sw, int *effective_cap,
				  int cc_uah, int vbat, int temp);
};

/**
 * struct simple_gauge_desc - fuel gauge description
 *
 * The fuel gauges which benefit from generic computations (typically devices
 * with coulomb counter. OCV - SOC table and iterative polling / error
 * correction) provided by the simple_gauge framework must be described by the
 * simple_gauge_desc prior registration to the simple_gauge framework.
 *
 * @name:		Identifying name for gauge (Is this needed?)
 * @degrade_cycle_uah:	constant lost capacity / battery cycle in uAh.
 * @amount_of_temp_dgr:	amount of temperature ranges provided in temp_dgr
 * @temp_dgr:		ranges of constant lost capacity / temperature degree
 *			in uAh. Ranges should be sorted in asecnding order by
 *			temperature_floor.
 * @poll_interval:	time interval in mS at which this fuel gauge iteration
 *			loop for volage polling and coulomb counter corrections
 *			should be ran.
 * @calibrate_interval:	time interval in mS at which this IC should be
 *			calibrated.
 * @designed_cap:	designed battery capacity in uAh. Can be given here if
 *			not available via batinfo.
 * @allow_set_cycle:	Allow userspace to set cached battery cycle. If no HW
 *			access is required when new battery cycle value is set
 *			the driver can omit set_cycle callback and just set
 *			this to true.
 * @clamp_soc:		Set true tonot allow computed SOC to increase if state
 *			is discharging.
 * @cap_adjust_volt_threshold: some systems want to apply extra computation
 *			to estimate battery capacity when voltage gets close
 *			to system limit in order to avoid shut-down for as long
 *			as possible. Such ICs can set this limit and optionally
 *			implement zero_cap_adjust callback.
 * @system_min_voltage:	ICs using the cap_adjust_volt_threshold and no
 *			zero_cap_adjust call-back should set this voltage
 *			to Vsys which correspond empty battery situation.
 */
struct simple_gauge_desc {
	int degrade_cycle_uah;
	int amount_of_temp_dgr;
	struct power_supply_temp_degr *temp_dgr;
	int poll_interval;
	int calibrate_interval;
	int designed_cap; /* This is also looked from batinfo (DT node) */
	int cap_adjust_volt_threshold;
	int system_min_voltage;
	bool allow_set_cycle;
	bool clamp_soc;
	void *drv_data;
};

/**
 * struct simple_gauge_psy - power supply configuration
 *
 * configuration being further passed to power-supply registration.
 */
struct simple_gauge_psy {
	const char *psy_name;
	struct device_node *of_node;
	/* Device specific sysfs attributes, delivered to power_supply */
	const struct attribute_group **attr_grp;

	enum power_supply_property *additional_props;
	int num_additional_props;

	int (*is_writable)(struct simple_gauge *gauge,
			   enum power_supply_property psp);
	int (*get_custom_property)(struct simple_gauge *gauge,
				   enum power_supply_property psp,
				   union power_supply_propval *val);
	int (*set_custom_property)(struct simple_gauge *gauge,
				   enum power_supply_property psp,
				   const union power_supply_propval *val);
};

/**
 * struct simple_gauge - simple_gauge runtime data
 *
 * Internal to sw-gauge. Should not be directly accessed/modified by drivers
 */
struct simple_gauge {
	struct device *dev;
	int designed_cap;	/* This should be available for drivers */
	struct simple_gauge_desc desc;
	int cycle;
	u64 next_iter; /* Time of next iteration in jiffies64 */
	u64 next_cal; /* Time of next calibration in jiffies64 */
	int force_run;
	int refcount;
	struct power_supply *psy;
	enum power_supply_property *properties;

	int (*get_custom_property)(struct simple_gauge *gauge,
				  enum power_supply_property psp,
				  union power_supply_propval *val);
	int (*set_custom_property)(struct simple_gauge *gauge,
				 enum power_supply_property psp,
				 const union power_supply_propval *val);
	int (*custom_is_writable)(struct simple_gauge *gauge,
				  enum power_supply_property psp);
	struct power_supply_battery_info info;
	struct simple_gauge_ops ops;
	struct list_head node;
	int amount_of_temp_dgr;
	struct power_supply_temp_degr *temp_dgr;
	spinlock_t lock;
	bool batinfo_got;
	wait_queue_head_t wq;
	int soc_rounding;
	int clamped_soc;
	/* Cached values from prev iteration */
	int soc;		/* SOC computed at previous iteration */
	int capacity_uah;	/* CAP computed at previous iteration (uAh) */
	int cc_uah;		/* uAh reported by CC at previous iteration */
	int temp;		/* Temperature at previous iteration */
};

struct simple_gauge *__must_check psy_register_simple_gauge(struct device *parent,
						    struct simple_gauge_psy *psycfg,
						    struct simple_gauge_ops *ops,
						    struct simple_gauge_desc *desc);
void psy_remove_simple_gauge(struct simple_gauge *sw);

struct simple_gauge *__must_check
devm_psy_register_simple_gauge(struct device *parent, struct simple_gauge_psy *psycfg,
			   struct simple_gauge_ops *ops,
			   struct simple_gauge_desc *desc);
void simple_gauge_run(struct simple_gauge *sw);
int simple_gauge_run_blocking_timeout(struct simple_gauge *sg,
				      unsigned int timeout_ms);
int simple_gauge_run_blocking(struct simple_gauge *sg);
void *simple_gauge_get_drvdata(struct simple_gauge *sg);

#endif /* POWER_SW_GAUGE_H */
