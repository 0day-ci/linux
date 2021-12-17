// SPDX-License-Identifier: GPL-2.0
/*
 * A power allocator to manage temperature
 *
 * Copyright (C) 2014 ARM Ltd.
 *
 */

#define pr_fmt(fmt) "Power allocator: " fmt

#include <linux/rculist.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define CREATE_TRACE_POINTS
#include <trace/events/thermal_power_allocator.h>

#include "thermal_core.h"

#define INVALID_TRIP -1

#define FRAC_BITS 10
#define int_to_frac(x) ((x) << FRAC_BITS)
#define frac_to_int(x) ((x) >> FRAC_BITS)

/**
 * mul_frac() - multiply two fixed-point numbers
 * @x:	first multiplicand
 * @y:	second multiplicand
 *
 * Return: the result of multiplying two fixed-point numbers.  The
 * result is also a fixed-point number.
 */
static inline s64 mul_frac(s64 x, s64 y)
{
	return (x * y) >> FRAC_BITS;
}

/**
 * div_frac() - divide two fixed-point numbers
 * @x:	the dividend
 * @y:	the divisor
 *
 * Return: the result of dividing two fixed-point numbers.  The
 * result is also a fixed-point number.
 */
static inline s64 div_frac(s64 x, s64 y)
{
	return div_s64(x << FRAC_BITS, y);
}

/**
 * enum pivot_type - Values representing what type of pivot the current error
 *                   value is
 * @PEAK:       The current error is a peak
 * @TROUGH:     The current error is a trough
 * @MIDPOINT:   The current error is neither a peak or trough and is some midpoint
 *             in between
 */
enum pivot_type { PEAK = 1, TROUGH = -1, MIDPOINT = 0 };

/**
 * enum ZN_VALUES - Values which the Ziegler-Nichols variable can take. This
 *                  determines which set of PID Coefficients to use
 * @ZN_ORIGINAL: Use the Original PID Coefficients when the thermal zone was
 *               initially bound
 * @ZN_OFF:      Use the current set of PID Coefficients
 * @ZN_ON:       Use Ziegler-Nichols to determine the best set of PID Coefficients
 * @ZN_RESET:    Reset the Ziegler-Nichols set of PID Coefficients so they can be
 *               found again
 */
enum ZN_VALUES { ZN_ORIGINAL = -1, ZN_OFF = 0, ZN_ON = 1, ZN_RESET = 2 };

/**
 * struct zn_coefficients - values used by the Ziegler-Nichols Heuristic to
 *                          determine what the optimal PID coefficients are
 * @zn_found:   Determine whether we have found or are still searching for
 *              optimal PID coefficients
 * @prev_err: Previous err logged
 * @curr_err: Current err being processed
 * @t_prev_peak: Timestamp for the previous "Peak"
 * @period: Period of osciallation
 * @k_ultimate: Value of k_P which produces stable oscillations
 * @base_peak: Err value of the current peak
 * @base_trough: Err value fo the current trough
 * @oscillation_count: Number of stable oscillations we have observed
 * @prev_pivot: Whether the previous pivot was a peak or trough
 * @zn_state: Current Ziegler-Nichols state
 *
 */
struct zn_coefficients {
	bool zn_found;
	s32 prev_err;
	s32 curr_err;
	u32 t_prev_peak;
	u32 period;
	u32 k_ultimate;

	s32 base_peak;
	s32 base_trough;
	s32 oscillation_count;
	enum pivot_type prev_pivot;

	int zn_state;
};

/**
 * struct power_allocator_params - parameters for the power allocator governor
 * @allocated_tzp:	whether we have allocated tzp for this thermal zone and
 *			it needs to be freed on unbind
 * @err_integral:	accumulated error in the PID controller.
 * @prev_err:	error in the previous iteration of the PID controller.
 *		Used to calculate the derivative term.
 * @trip_switch_on:	first passive trip point of the thermal zone.  The
 *			governor switches on when this trip point is crossed.
 *			If the thermal zone only has one passive trip point,
 *			@trip_switch_on should be INVALID_TRIP.
 * @trip_max_desired_temperature:	last passive trip point of the thermal
 *					zone.  The temperature we are
 *					controlling for.
 * @sustainable_power:	Sustainable power (heat) that this thermal zone can
 *			dissipate
 * @zn_coeffs:  Structure to hold information used by the Ziegler-Nichols
 *              heuristic
 */
struct power_allocator_params {
	bool allocated_tzp;
	s64 err_integral;
	s32 prev_err;
	int trip_switch_on;
	int trip_max_desired_temperature;
	u32 sustainable_power;
	struct zn_coefficients *zn_coeffs;
};

/**
 * estimate_sustainable_power() - Estimate the sustainable power of a thermal zone
 * @tz: thermal zone we are operating in
 *
 * For thermal zones that don't provide a sustainable_power in their
 * thermal_zone_params, estimate one.  Calculate it using the minimum
 * power of all the cooling devices as that gives a valid value that
 * can give some degree of functionality.  For optimal performance of
 * this governor, provide a sustainable_power in the thermal zone's
 * thermal_zone_params.
 *
 * Return: the sustainable power for this thermal_zone
 */
static u32 estimate_sustainable_power(struct thermal_zone_device *tz)
{
	u32 sustainable_power = 0;
	struct thermal_instance *instance;
	struct power_allocator_params *params = tz->governor_data;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		struct thermal_cooling_device *cdev = instance->cdev;
		u32 min_power;

		if (instance->trip != params->trip_max_desired_temperature)
			continue;

		if (!cdev_is_power_actor(cdev))
			continue;

		if (cdev->ops->state2power(cdev, instance->upper, &min_power))
			continue;

		sustainable_power += min_power;
	}

	return sustainable_power;
}

/**
 * estimate_pid_constants() - Estimate the constants for the PID controller
 * @tz:		thermal zone for which to estimate the constants
 * @sustainable_power:	sustainable power for the thermal zone
 * @trip_switch_on:	trip point number for the switch on temperature
 * @control_temp:	target temperature for the power allocator governor
 *
 * This function is used to update the estimation of the PID
 * controller constants in struct thermal_zone_parameters.
 */
static void estimate_pid_constants(struct thermal_zone_device *tz,
				   u32 sustainable_power, int trip_switch_on,
				   int control_temp)
{
	int ret;
	int switch_on_temp;
	u32 temperature_threshold;
	s32 k_i;

	ret = tz->ops->get_trip_temp(tz, trip_switch_on, &switch_on_temp);
	if (ret)
		switch_on_temp = 0;

	temperature_threshold = control_temp - switch_on_temp;
	/*
	 * estimate_pid_constants() tries to find appropriate default
	 * values for thermal zones that don't provide them. If a
	 * system integrator has configured a thermal zone with two
	 * passive trip points at the same temperature, that person
	 * hasn't put any effort to set up the thermal zone properly
	 * so just give up.
	 */
	if (!temperature_threshold)
		return;

	tz->tzp->k_po = int_to_frac(sustainable_power) /
		temperature_threshold;

	tz->tzp->k_pu = int_to_frac(2 * sustainable_power) /
		temperature_threshold;

	k_i = tz->tzp->k_pu / 10;
	tz->tzp->k_i = k_i > 0 ? k_i : 1;

	/*
	 * The default for k_d and integral_cutoff is 0, so we can
	 * leave them as they are.
	 */
}

/**
 * get_sustainable_power() - Get the right sustainable power
 * @tz:		thermal zone for which to estimate the constants
 * @params:	parameters for the power allocator governor
 * @control_temp:	target temperature for the power allocator governor
 *
 * This function is used for getting the proper sustainable power value based
 * on variables which might be updated by the user sysfs interface. If that
 * happen the new value is going to be estimated and updated. It is also used
 * after thermal zone binding, where the initial values where set to 0.
 *
 * Return: The sustainable power for this thermal_zone
 */
static u32 get_sustainable_power(struct thermal_zone_device *tz,
				 struct power_allocator_params *params,
				 int control_temp)
{
	u32 sustainable_power;

	if (!tz->tzp->sustainable_power)
		sustainable_power = estimate_sustainable_power(tz);
	else
		sustainable_power = tz->tzp->sustainable_power;

	/* Check if it's init value 0 or there was update via sysfs */
	if (sustainable_power != params->sustainable_power) {
		estimate_pid_constants(tz, sustainable_power,
				       params->trip_switch_on, control_temp);

		/* Do the estimation only once and make available in sysfs */
		tz->tzp->sustainable_power = sustainable_power;
		params->sustainable_power = sustainable_power;
	}

	return sustainable_power;
}

/**
 * set_original_pid_coefficients() - Reset PID Coefficients in the Thermal Zone
 *                                   to original values
 * @tzp: Thermal Zone Parameters we want to update
 *
 */
static inline void set_original_pid_coefficients(struct thermal_zone_params *tzp)
{
	static bool init = true;
	static s32 k_po, k_pu, k_i, k_d, integral_cutoff;

	if (init) {
		k_po = tzp->k_po;
		k_pu = tzp->k_pu;
		k_i = tzp->k_i;
		k_d = tzp->k_d;
		integral_cutoff = tzp->integral_cutoff;
		init = false;
	} else {
		tzp->k_po = k_po;
		tzp->k_pu = k_pu;
		tzp->k_i = k_i;
		tzp->k_d = k_d;
		tzp->integral_cutoff = integral_cutoff;
	}
}

/**
 * set_zn_pid_coefficients() - Calculate and set PID Coefficients based
 *                             on Ziegler-Nichols Heuristic
 * @tzp: thermal zone params to set
 * @period: time taken for error to cycle 1 period
 * @k_ultimate: the Ultimate Proportional Gain value at which
 *              the error oscillates around the set-point
 *
 * This function sets the PID Coefficients of the thermal device
 */
static inline void set_zn_pid_coefficients(struct thermal_zone_params *tzp,
					   u32 period, s32 k_ultimate)
{
	/* Convert time in ms for 1 cycle to cycles/s */
	s32 freq = 1000 / period;

	/* Make k_pu and k_po identical so it represents k_p */
	tzp->k_pu = k_ultimate * 1 / 10;
	tzp->k_po = tzp->k_pu;

	tzp->k_i = freq / 2;
	/* We want an integral term so if the value is 0, set it to 1 */
	tzp->k_i = tzp->k_i > 0 ? tzp->k_i : 1;

	tzp->k_d = (33 * freq) / 100;
	/* We want an integral term so if the value is 0, set it to 1 */
	tzp->k_d = tzp->k_d > 0 ? tzp->k_d : 1;
}

/**
 * is_error_acceptable() - Check whether the error determined to be a pivot
 *                         point is within the acceptable range
 * @err: error value we are checking
 * @base: the base_line value we are comparing against
 *
 * This function is used to determine whether our current pivot point is within
 * the acceptable limits. The value of base is the first pivot point within
 * this series of oscillations
 *
 * Return: boolean representing whether or not the error was within the acceptable
 *         range
 */
static inline bool is_error_acceptable(s32 err, s32 base)
{
	/* Margin for error in milli-celcius */
	const s32 MARGIN = 500;
	s32 lower = abs(base) - MARGIN;
	s32 upper = abs(base) + MARGIN;

	if (lower < abs(err) && abs(err) < upper)
		return true;
	return false;
}

/**
 * is_error_pivot() - Determine whether an error value is a pivot based on the
 *                    previous and next error values
 * @next_err: the next error in a series
 * @curr_err: the current error value we are checking
 * @prev_err: the previous error in a series
 * @peak_trough: integer value to output what kind of pivot (if any)
 *                    the error value is
 *
 * Determine whether or not the current value of error is a pivot and if it is
 * a pivot, which type of pivot it is (peak or trough).
 *
 * Return: Bool representing whether the current value is a pivot point and
 *         integer set to PEAK, TROUGH or MIDPOINT
 */
static inline bool is_error_pivot(s32 next_err, s32 curr_err, s32 prev_err,
				  enum pivot_type *peak_trough)
{
	/*
	 * Check whether curr_err is at it's highest value compared to its neighbours and that error
	 * value is positive
	 */
	if (prev_err < curr_err && curr_err > next_err && curr_err > 0) {
		*peak_trough = PEAK;
		return true;
	}
	/*
	 * Check whether curr_err is at it's lowest value compared to its neighbours and that error
	 * value is negative
	 */
	if (prev_err > curr_err && curr_err < next_err && curr_err < 0) {
		*peak_trough = TROUGH;
		return true;
	}
	/* If the error is not a pivot then it must be somewhere between pivots */
	*peak_trough = MIDPOINT;
	return false;
}

/**
 * update_oscillation_count() - Update the Oscillation Count for this set of pivots
 * @curr_err: the current error value we are checking
 * @base_pivot: the amplitude we are comparing against
 * @peak_trough: the type of pivot we are currently processing
 * @zn_coeffs: the data structure holding information used by the Ziegler-Nichols Hueristic
 *
 * Update the number of times we have oscillated based on our current error value being within the
 * accepted range from the amplitude of previous pivots in this oscillation series.
 *
 * Return: Integer count of the number of oscillations
 */
static inline s32 update_oscillation_count(s32 curr_err, s32 *base_pivot,
					   enum pivot_type peak_trough,
					   struct zn_coefficients *zn_coeffs)
{
	if (is_error_acceptable(curr_err, *base_pivot) &&
	    zn_coeffs->prev_pivot == -peak_trough) {
		zn_coeffs->oscillation_count++;
	} else {
		zn_coeffs->oscillation_count = 0;
		*base_pivot = curr_err;
	}
	zn_coeffs->prev_pivot = peak_trough;
	return zn_coeffs->oscillation_count;
}

/**
 * get_oscillation_count() - Update and get the number of times we have oscillated
 * @curr_err: the current error value we are checking
 * @peak_trough: the type of pivot we are currently processing
 * @zn_coeffs: the data structure holding information used by the
 *                    Ziegler-Nichols Hueristic
 *
 * Return: The number of times we have oscillated for this k_ultimate
 */
static inline s32 get_oscillation_count(s32 curr_err,
					enum pivot_type peak_trough,
					struct zn_coefficients *zn_coeffs)
{
	s32 *base_pivot = 0;

	if (peak_trough == PEAK)
		base_pivot = &zn_coeffs->base_peak;
	else if (peak_trough == TROUGH)
		base_pivot = &zn_coeffs->base_trough;

	return update_oscillation_count(curr_err, base_pivot, peak_trough,
					zn_coeffs);
}

/**
 * get_zn_state() - Update and get the current Ziegler-Nichols State
 * @tzp: The thermal zone params to check to determine the current state
 * @zn_state: The current state which should be returned if no changes are
 *            made
 *
 * Return: The next zieger-nichols state for this pass of the PID controller
 */
static inline int get_zn_state(struct thermal_zone_params *tzp, int zn_state)
{
	if (tzp->k_po == ZN_RESET && tzp->k_pu == ZN_RESET)
		return ZN_RESET;

	if (tzp->k_po == ZN_ORIGINAL && tzp->k_pu == ZN_ORIGINAL)
		return ZN_ORIGINAL;

	if (tzp->k_po == ZN_ON && tzp->k_pu == ZN_ON)
		return ZN_ON;

	return zn_state;
}

/**
 * is_temperature_safe() - Check if the current temperature is within 10% of
 *                         the target
 *
 * @current_temperature: Current reported temperature
 * @control_temp:        Control Temperature we are targeting
 *
 * Return: True if current temperature is within 10% of the target, False otherwise
 */
static inline bool is_temperature_safe(int current_temperature,
				       int control_temp)
{
	return (current_temperature - control_temp) < (control_temp / 10) ?
		       true :
		       false;
}

/**
 * reset_ziegler_nichols() - Reset the Values used to Track Ziegler-Nichols
 *
 * @zn_coeffs: the data structure holding information used by the Ziegler-Nichols Hueristic
 *
 */
static inline void reset_ziegler_nichols(struct zn_coefficients *zn_coeffs)
{
	zn_coeffs->zn_found = false;
	zn_coeffs->k_ultimate = 10;
	zn_coeffs->prev_err = 0;
	zn_coeffs->curr_err = 0;
	zn_coeffs->t_prev_peak = 0;
	zn_coeffs->period = 0;
	/* Manually input INT_MAX as a previous value so the system cannot use it accidentally */
	zn_coeffs->oscillation_count = update_oscillation_count(
		INT_MAX, &zn_coeffs->curr_err, PEAK, zn_coeffs);
}

/**
 * ziegler_nichols() - Calculate the k_ultimate and period for the thermal device
 *                      and use these values to calculate and set the PID coefficients based on
 *                      the Ziegler-Nichols Heuristic
 * @tz: The thermal device we are operating on
 * @next_err: The next error value to be used for calculations
 * @control_temp: The temperature we are trying to target
 *
 * The Ziegler-Nichols PID Coefficient Tuning Method works by determining a K_Ultimate value. This
 * is the largest K_P which yields a stable set of oscillations in error. By using historic and
 * current values of error, this function attempts to determine whether or not it is oscillating,
 * and increment the value of K_Ultimate accordingly.  Once it has determined that the system is
 * oscillating, it calculates the time between "peaks" to determine its period
 *
 */
static inline void ziegler_nichols(struct thermal_zone_device *tz, s32 next_err,
				   int control_temp)
{
	struct power_allocator_params *params = tz->governor_data;
	struct zn_coefficients *zn_coeffs = params->zn_coeffs;
	const int NUMBER_OF_OSCILLATIONS = 10;

	u32 t_now = (u32)div_frac(ktime_get_real_ns(), 1000000);
	enum pivot_type peak_trough = MIDPOINT;
	s32 oscillation_count = 0;
	bool is_pivot;
	bool is_safe =
		is_temperature_safe((control_temp - next_err), control_temp);

	zn_coeffs->zn_state = get_zn_state(tz->tzp, zn_coeffs->zn_state);
	switch (zn_coeffs->zn_state) {
	case ZN_ORIGINAL: {
		set_original_pid_coefficients(tz->tzp);
		zn_coeffs->zn_state = ZN_OFF;
		return;
	}
	case ZN_RESET: {
		reset_ziegler_nichols(zn_coeffs);
		zn_coeffs->zn_state = ZN_ON;
		break;
	}

	case ZN_OFF:
		return;
	}

	/* Override default PID Coefficients. These will be updated later according to the
	 * Heuristic
	 */
	tz->tzp->k_po = zn_coeffs->k_ultimate;
	tz->tzp->k_pu = zn_coeffs->k_ultimate;
	tz->tzp->k_i = 0;
	tz->tzp->k_d = 0;

	if (!zn_coeffs->zn_found) {
		/* Make sure that the previous errors have been logged and this isn't executed on
		 * first pass
		 */
		if (zn_coeffs->curr_err != zn_coeffs->prev_err &&
		    zn_coeffs->prev_err != 0) {
			if (!is_safe)
				goto set_zn;
			is_pivot = is_error_pivot(next_err, zn_coeffs->curr_err,
						  zn_coeffs->prev_err,
						  &peak_trough);
			if (is_pivot) {
				oscillation_count = get_oscillation_count(
					zn_coeffs->curr_err, peak_trough,
					zn_coeffs);
				if (oscillation_count >=
				    NUMBER_OF_OSCILLATIONS) {
					goto set_zn;
				}
				if (peak_trough == PEAK)
					zn_coeffs->t_prev_peak = t_now;
			}
			if (!is_pivot || !oscillation_count)
				zn_coeffs->k_ultimate += 10;
		}
		goto update_errors;
	} else {
		set_zn_pid_coefficients(tz->tzp, zn_coeffs->period,
					zn_coeffs->k_ultimate);
		zn_coeffs->zn_state = ZN_OFF;
	}
	return;

update_errors:
	zn_coeffs->prev_err = zn_coeffs->curr_err;
	zn_coeffs->curr_err = next_err;
	return;

set_zn:
	if (zn_coeffs->t_prev_peak) {
		zn_coeffs->zn_found = true;
		zn_coeffs->period = abs(t_now - zn_coeffs->t_prev_peak);
		set_zn_pid_coefficients(tz->tzp, zn_coeffs->period,
					zn_coeffs->k_ultimate);
		((struct power_allocator_params *)tz->governor_data)
			->err_integral = 0;
		zn_coeffs->zn_state = ZN_OFF;
	} else {
		if (peak_trough == PEAK)
			zn_coeffs->t_prev_peak = t_now;
	}
}

/**
 * pid_controller() - PID controller
 * @tz:	thermal zone we are operating in
 * @control_temp:	the target temperature in millicelsius
 * @max_allocatable_power:	maximum allocatable power for this thermal zone
 *
 * This PID controller increases the available power budget so that the
 * temperature of the thermal zone gets as close as possible to
 * @control_temp and limits the power if it exceeds it.  k_po is the
 * proportional term when we are overshooting, k_pu is the
 * proportional term when we are undershooting.  integral_cutoff is a
 * threshold below which we stop accumulating the error.  The
 * accumulated error is only valid if the requested power will make
 * the system warmer.  If the system is mostly idle, there's no point
 * in accumulating positive error.
 *
 * Return: The power budget for the next period.
 */
static u32 pid_controller(struct thermal_zone_device *tz,
			  int control_temp,
			  u32 max_allocatable_power)
{
	s64 p, i, d, power_range;
	s32 err, max_power_frac;
	u32 sustainable_power;
	struct power_allocator_params *params = tz->governor_data;

	max_power_frac = int_to_frac(max_allocatable_power);

	sustainable_power = get_sustainable_power(tz, params, control_temp);

	err = control_temp - tz->temperature;

	ziegler_nichols(tz, err, control_temp);

	err = int_to_frac(err);

	/* Calculate the proportional term */
	p = mul_frac(err < 0 ? tz->tzp->k_po : tz->tzp->k_pu, err);

	/*
	 * Calculate the integral term
	 *
	 * if the error is less than cut off allow integration (but
	 * the integral is limited to max power)
	 */
	i = mul_frac(tz->tzp->k_i, params->err_integral);

	if (err < int_to_frac(tz->tzp->integral_cutoff)) {
		s64 i_next = i + mul_frac(tz->tzp->k_i, err);

		if (abs(i_next) < max_power_frac) {
			i = i_next;
			params->err_integral += err;
		}
	}

	/*
	 * Calculate the derivative term
	 *
	 * We do err - prev_err, so with a positive k_d, a decreasing
	 * error (i.e. driving closer to the line) results in less
	 * power being applied, slowing down the controller)
	 */
	d = mul_frac(tz->tzp->k_d, err - params->prev_err);
	d = div_frac(d, jiffies_to_msecs(tz->passive_delay_jiffies));
	params->prev_err = err;

	power_range = p + i + d;

	/* feed-forward the known sustainable dissipatable power */
	power_range = sustainable_power + frac_to_int(power_range);

	power_range = clamp(power_range, (s64)0, (s64)max_allocatable_power);

	trace_thermal_power_allocator_pid(tz, frac_to_int(err),
					  frac_to_int(params->err_integral),
					  frac_to_int(p), frac_to_int(i),
					  frac_to_int(d), power_range);

	return power_range;
}

/**
 * power_actor_set_power() - limit the maximum power a cooling device consumes
 * @cdev:	pointer to &thermal_cooling_device
 * @instance:	thermal instance to update
 * @power:	the power in milliwatts
 *
 * Set the cooling device to consume at most @power milliwatts. The limit is
 * expected to be a cap at the maximum power consumption.
 *
 * Return: 0 on success, -EINVAL if the cooling device does not
 * implement the power actor API or -E* for other failures.
 */
static int
power_actor_set_power(struct thermal_cooling_device *cdev,
		      struct thermal_instance *instance, u32 power)
{
	unsigned long state;
	int ret;

	ret = cdev->ops->power2state(cdev, power, &state);
	if (ret)
		return ret;

	instance->target = clamp_val(state, instance->lower, instance->upper);
	mutex_lock(&cdev->lock);
	__thermal_cdev_update(cdev);
	mutex_unlock(&cdev->lock);

	return 0;
}

/**
 * divvy_up_power() - divvy the allocated power between the actors
 * @req_power:	each actor's requested power
 * @max_power:	each actor's maximum available power
 * @num_actors:	size of the @req_power, @max_power and @granted_power's array
 * @total_req_power: sum of @req_power
 * @power_range:	total allocated power
 * @granted_power:	output array: each actor's granted power
 * @extra_actor_power:	an appropriately sized array to be used in the
 *			function as temporary storage of the extra power given
 *			to the actors
 *
 * This function divides the total allocated power (@power_range)
 * fairly between the actors.  It first tries to give each actor a
 * share of the @power_range according to how much power it requested
 * compared to the rest of the actors.  For example, if only one actor
 * requests power, then it receives all the @power_range.  If
 * three actors each requests 1mW, each receives a third of the
 * @power_range.
 *
 * If any actor received more than their maximum power, then that
 * surplus is re-divvied among the actors based on how far they are
 * from their respective maximums.
 *
 * Granted power for each actor is written to @granted_power, which
 * should've been allocated by the calling function.
 */
static void divvy_up_power(u32 *req_power, u32 *max_power, int num_actors,
			   u32 total_req_power, u32 power_range,
			   u32 *granted_power, u32 *extra_actor_power)
{
	u32 extra_power, capped_extra_power;
	int i;

	/*
	 * Prevent division by 0 if none of the actors request power.
	 */
	if (!total_req_power)
		total_req_power = 1;

	capped_extra_power = 0;
	extra_power = 0;
	for (i = 0; i < num_actors; i++) {
		u64 req_range = (u64)req_power[i] * power_range;

		granted_power[i] = DIV_ROUND_CLOSEST_ULL(req_range,
							 total_req_power);

		if (granted_power[i] > max_power[i]) {
			extra_power += granted_power[i] - max_power[i];
			granted_power[i] = max_power[i];
		}

		extra_actor_power[i] = max_power[i] - granted_power[i];
		capped_extra_power += extra_actor_power[i];
	}

	if (!extra_power)
		return;

	/*
	 * Re-divvy the reclaimed extra among actors based on
	 * how far they are from the max
	 */
	extra_power = min(extra_power, capped_extra_power);
	if (capped_extra_power > 0)
		for (i = 0; i < num_actors; i++) {
			u64 extra_range = (u64)extra_actor_power[i] * extra_power;

			granted_power[i] += DIV_ROUND_CLOSEST_ULL(extra_range,
							 capped_extra_power);
		}
}

static int allocate_power(struct thermal_zone_device *tz,
			  int control_temp)
{
	struct thermal_instance *instance;
	struct power_allocator_params *params = tz->governor_data;
	u32 *req_power, *max_power, *granted_power, *extra_actor_power;
	u32 *weighted_req_power;
	u32 total_req_power, max_allocatable_power, total_weighted_req_power;
	u32 total_granted_power, power_range;
	int i, num_actors, total_weight, ret = 0;
	int trip_max_desired_temperature = params->trip_max_desired_temperature;

	mutex_lock(&tz->lock);

	num_actors = 0;
	total_weight = 0;
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if ((instance->trip == trip_max_desired_temperature) &&
		    cdev_is_power_actor(instance->cdev)) {
			num_actors++;
			total_weight += instance->weight;
		}
	}

	if (!num_actors) {
		ret = -ENODEV;
		goto unlock;
	}

	/*
	 * We need to allocate five arrays of the same size:
	 * req_power, max_power, granted_power, extra_actor_power and
	 * weighted_req_power.  They are going to be needed until this
	 * function returns.  Allocate them all in one go to simplify
	 * the allocation and deallocation logic.
	 */
	BUILD_BUG_ON(sizeof(*req_power) != sizeof(*max_power));
	BUILD_BUG_ON(sizeof(*req_power) != sizeof(*granted_power));
	BUILD_BUG_ON(sizeof(*req_power) != sizeof(*extra_actor_power));
	BUILD_BUG_ON(sizeof(*req_power) != sizeof(*weighted_req_power));
	req_power = kcalloc(num_actors * 5, sizeof(*req_power), GFP_KERNEL);
	if (!req_power) {
		ret = -ENOMEM;
		goto unlock;
	}

	max_power = &req_power[num_actors];
	granted_power = &req_power[2 * num_actors];
	extra_actor_power = &req_power[3 * num_actors];
	weighted_req_power = &req_power[4 * num_actors];

	i = 0;
	total_weighted_req_power = 0;
	total_req_power = 0;
	max_allocatable_power = 0;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		int weight;
		struct thermal_cooling_device *cdev = instance->cdev;

		if (instance->trip != trip_max_desired_temperature)
			continue;

		if (!cdev_is_power_actor(cdev))
			continue;

		if (cdev->ops->get_requested_power(cdev, &req_power[i]))
			continue;

		if (!total_weight)
			weight = 1 << FRAC_BITS;
		else
			weight = instance->weight;

		weighted_req_power[i] = frac_to_int(weight * req_power[i]);

		if (cdev->ops->state2power(cdev, instance->lower,
					   &max_power[i]))
			continue;

		total_req_power += req_power[i];
		max_allocatable_power += max_power[i];
		total_weighted_req_power += weighted_req_power[i];

		i++;
	}

	power_range = pid_controller(tz, control_temp, max_allocatable_power);

	divvy_up_power(weighted_req_power, max_power, num_actors,
		       total_weighted_req_power, power_range, granted_power,
		       extra_actor_power);

	total_granted_power = 0;
	i = 0;
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->trip != trip_max_desired_temperature)
			continue;

		if (!cdev_is_power_actor(instance->cdev))
			continue;

		power_actor_set_power(instance->cdev, instance,
				      granted_power[i]);
		total_granted_power += granted_power[i];

		i++;
	}

	trace_thermal_power_allocator(tz, req_power, total_req_power,
				      granted_power, total_granted_power,
				      num_actors, power_range,
				      max_allocatable_power, tz->temperature,
				      control_temp - tz->temperature);

	kfree(req_power);
unlock:
	mutex_unlock(&tz->lock);

	return ret;
}

/**
 * get_governor_trips() - get the number of the two trip points that are key for this governor
 * @tz:	thermal zone to operate on
 * @params:	pointer to private data for this governor
 *
 * The power allocator governor works optimally with two trips points:
 * a "switch on" trip point and a "maximum desired temperature".  These
 * are defined as the first and last passive trip points.
 *
 * If there is only one trip point, then that's considered to be the
 * "maximum desired temperature" trip point and the governor is always
 * on.  If there are no passive or active trip points, then the
 * governor won't do anything.  In fact, its throttle function
 * won't be called at all.
 */
static void get_governor_trips(struct thermal_zone_device *tz,
			       struct power_allocator_params *params)
{
	int i, last_active, last_passive;
	bool found_first_passive;

	found_first_passive = false;
	last_active = INVALID_TRIP;
	last_passive = INVALID_TRIP;

	for (i = 0; i < tz->trips; i++) {
		enum thermal_trip_type type;
		int ret;

		ret = tz->ops->get_trip_type(tz, i, &type);
		if (ret) {
			dev_warn(&tz->device,
				 "Failed to get trip point %d type: %d\n", i,
				 ret);
			continue;
		}

		if (type == THERMAL_TRIP_PASSIVE) {
			if (!found_first_passive) {
				params->trip_switch_on = i;
				found_first_passive = true;
			} else  {
				last_passive = i;
			}
		} else if (type == THERMAL_TRIP_ACTIVE) {
			last_active = i;
		} else {
			break;
		}
	}

	if (last_passive != INVALID_TRIP) {
		params->trip_max_desired_temperature = last_passive;
	} else if (found_first_passive) {
		params->trip_max_desired_temperature = params->trip_switch_on;
		params->trip_switch_on = INVALID_TRIP;
	} else {
		params->trip_switch_on = INVALID_TRIP;
		params->trip_max_desired_temperature = last_active;
	}
}

static void reset_pid_controller(struct power_allocator_params *params)
{
	params->err_integral = 0;
	params->prev_err = 0;
}

static void allow_maximum_power(struct thermal_zone_device *tz, bool update)
{
	struct thermal_instance *instance;
	struct power_allocator_params *params = tz->governor_data;
	u32 req_power;

	mutex_lock(&tz->lock);
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		struct thermal_cooling_device *cdev = instance->cdev;

		if ((instance->trip != params->trip_max_desired_temperature) ||
		    (!cdev_is_power_actor(instance->cdev)))
			continue;

		instance->target = 0;
		mutex_lock(&instance->cdev->lock);
		/*
		 * Call for updating the cooling devices local stats and avoid
		 * periods of dozen of seconds when those have not been
		 * maintained.
		 */
		cdev->ops->get_requested_power(cdev, &req_power);

		if (update)
			__thermal_cdev_update(instance->cdev);

		mutex_unlock(&instance->cdev->lock);
	}
	mutex_unlock(&tz->lock);
}

/**
 * check_power_actors() - Check all cooling devices and warn when they are
 *			not power actors
 * @tz:		thermal zone to operate on
 *
 * Check all cooling devices in the @tz and warn every time they are missing
 * power actor API. The warning should help to investigate the issue, which
 * could be e.g. lack of Energy Model for a given device.
 *
 * Return: 0 on success, -EINVAL if any cooling device does not implement
 * the power actor API.
 */
static int check_power_actors(struct thermal_zone_device *tz)
{
	struct thermal_instance *instance;
	int ret = 0;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (!cdev_is_power_actor(instance->cdev)) {
			dev_warn(&tz->device, "power_allocator: %s is not a power actor\n",
				 instance->cdev->type);
			ret = -EINVAL;
		}
	}

	return ret;
}

/**
 * power_allocator_bind() - bind the power_allocator governor to a thermal zone
 * @tz:	thermal zone to bind it to
 *
 * Initialize the PID controller parameters and bind it to the thermal
 * zone.
 *
 * Return: 0 on success, or -ENOMEM if we ran out of memory, or -EINVAL
 * when there are unsupported cooling devices in the @tz.
 */
static int power_allocator_bind(struct thermal_zone_device *tz)
{
	int ret;
	struct power_allocator_params *params;
	int control_temp;
	struct zn_coefficients *zn_coeffs;

	ret = check_power_actors(tz);
	if (ret)
		return ret;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	zn_coeffs = kzalloc(sizeof(*zn_coeffs), GFP_KERNEL);
	if (!zn_coeffs)
		return -ENOMEM;

	params->zn_coeffs = zn_coeffs;
	zn_coeffs->zn_state = ZN_ON;

	if (!tz->tzp) {
		tz->tzp = kzalloc(sizeof(*tz->tzp), GFP_KERNEL);
		if (!tz->tzp) {
			ret = -ENOMEM;
			goto free_params;
		}

		params->allocated_tzp = true;
	}

	if (!tz->tzp->sustainable_power)
		dev_warn(&tz->device, "power_allocator: sustainable_power will be estimated\n");

	get_governor_trips(tz, params);

	if (tz->trips > 0) {
		ret = tz->ops->get_trip_temp(tz,
					params->trip_max_desired_temperature,
					&control_temp);
		if (!ret)
			estimate_pid_constants(tz, tz->tzp->sustainable_power,
					       params->trip_switch_on,
					       control_temp);
		/* Store the original PID coefficient values */
		set_original_pid_coefficients(tz->tzp);
	}

	reset_pid_controller(params);

	tz->governor_data = params;

	return 0;

free_params:
	kfree(params);

	return ret;
}

static void power_allocator_unbind(struct thermal_zone_device *tz)
{
	struct power_allocator_params *params = tz->governor_data;

	dev_dbg(&tz->device, "Unbinding from thermal zone %d\n", tz->id);

	kfree(params->zn_coeffs);
	params->zn_coeffs = NULL;

	if (params->allocated_tzp) {
		kfree(tz->tzp);
		tz->tzp = NULL;
	}

	kfree(tz->governor_data);
	tz->governor_data = NULL;
}

static int power_allocator_throttle(struct thermal_zone_device *tz, int trip)
{
	int ret;
	int switch_on_temp, control_temp;
	struct power_allocator_params *params = tz->governor_data;
	bool update;

	/*
	 * We get called for every trip point but we only need to do
	 * our calculations once
	 */
	if (trip != params->trip_max_desired_temperature)
		return 0;

	ret = tz->ops->get_trip_temp(tz, params->trip_switch_on,
				     &switch_on_temp);
	if (!ret && (tz->temperature < switch_on_temp)) {
		update = (tz->last_temperature >= switch_on_temp);
		tz->passive = 0;
		reset_pid_controller(params);
		allow_maximum_power(tz, update);
		return 0;
	}

	tz->passive = 1;

	ret = tz->ops->get_trip_temp(tz, params->trip_max_desired_temperature,
				&control_temp);
	if (ret) {
		dev_warn(&tz->device,
			 "Failed to get the maximum desired temperature: %d\n",
			 ret);
		return ret;
	}

	return allocate_power(tz, control_temp);
}

static struct thermal_governor thermal_gov_power_allocator = {
	.name		= "power_allocator",
	.bind_to_tz	= power_allocator_bind,
	.unbind_from_tz	= power_allocator_unbind,
	.throttle	= power_allocator_throttle,
};

THERMAL_GOVERNOR_DECLARE(thermal_gov_power_allocator);
