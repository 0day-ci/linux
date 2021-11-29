// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "osnoise.h"
#include "utils.h"

/*
 * osnoise_get_cpus - return the original "osnoise/cpus" content
 *
 * It also saves the value to be restored.
 */
char *osnoise_get_cpus(struct osnoise_context *context)
{
	char buffer[1024];
	char *cpus_path;
	int retval;

	if (context->curr_cpus)
		return context->curr_cpus;

	if (context->orig_cpus)
		return context->orig_cpus;

	cpus_path = tracefs_get_tracing_file("osnoise/cpus");

	context->cpus_fd = open(cpus_path, O_RDWR);
	if (context->cpus_fd < 0)
		goto out_err;

	retval = read(context->cpus_fd, &buffer, sizeof(buffer));
	if (retval <= 0)
		goto out_close;

	context->orig_cpus = strdup(buffer);
	if (!context->orig_cpus)
		goto out_close;

	tracefs_put_tracing_file(cpus_path);

	return context->orig_cpus;

out_close:
	close(context->cpus_fd);
out_err:
	tracefs_put_tracing_file(cpus_path);
	return NULL;
}

/*
 * osnoise_set_cpus - configure osnoise to run on *cpus
 *
 * "osnoise/cpus" file is used to set the cpus in which osnoise/timerlat
 * will run. This function opens this file, saves the current value,
 * and set the cpus passed as argument.
 */
int osnoise_set_cpus(struct osnoise_context *context, char *cpus)
{
	char *orig_cpus = osnoise_get_cpus(context);
	int retval;

	if (!orig_cpus)
		return -1;

	context->curr_cpus = strdup(cpus);
	if (!context->curr_cpus)
		return -1;

	retval = write(context->cpus_fd, cpus, strlen(cpus) + 1);
	if (retval < (strlen(cpus) + 1)) {
		free(context->curr_cpus);
		context->curr_cpus = NULL;
		return -1;
	}

	return 0;
}

/*
 * osnoise_restore_cpus - restore the original "osnoise/cpus"
 *
 * osnoise_set_cpus() saves the original data for the "osnoise/cpus"
 * file. This function restore the original config it was previously
 * modified.
 */
void osnoise_restore_cpus(struct osnoise_context *context)
{
	int retval;

	if (!context->orig_cpus)
		return;

	if (!context->curr_cpus)
		return;

	/* nothing to do? */
	if (!strcmp(context->orig_cpus, context->curr_cpus))
		goto out_done;

	retval = write(context->cpus_fd, context->orig_cpus, strlen(context->orig_cpus));
	if (retval < strlen(context->orig_cpus))
		err_msg("could not restore original osnoise cpus\n");

out_done:
	free(context->curr_cpus);
	context->curr_cpus = NULL;
}

/*
 * osnoise_put_cpus - restore cpus config and cleanup data
 */
void osnoise_put_cpus(struct osnoise_context *context)
{
	osnoise_restore_cpus(context);

	if (!context->orig_cpus)
		return;

	free(context->orig_cpus);
	context->orig_cpus = NULL;
	close(context->cpus_fd);
	context->cpus_fd = CLOSED_FD;
}

/*
 * osnoise_get_runtime - return the original "osnoise/runtime_us" value
 *
 * It also saves the value to be restored.
 */
unsigned long long osnoise_get_runtime(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	long long runtime_us;
	char *runtime_path;
	int retval;

	if (context->runtime_us != OSNOISE_TIME_INIT_VAL)
		return context->runtime_us;

	if (context->orig_runtime_us != OSNOISE_TIME_INIT_VAL)
		return context->orig_runtime_us;

	runtime_path = tracefs_get_tracing_file("osnoise/runtime_us");

	context->runtime_fd = open(runtime_path, O_RDWR);
	if (context->runtime_fd < 0)
		goto out_err;

	retval = read(context->runtime_fd, &buffer, sizeof(buffer));
	if (retval <= 0)
		goto out_close;

	runtime_us = get_llong_from_str(buffer);
	if (runtime_us < 0)
		goto out_close;

	tracefs_put_tracing_file(runtime_path);

	context->orig_runtime_us = runtime_us;
	return runtime_us;

out_close:
	close(context->runtime_fd);
	context->runtime_fd = CLOSED_FD;
out_err:
	tracefs_put_tracing_file(runtime_path);
	return 0;
}

/*
 * osnoise_get_period - return the original "osnoise/period_us" value
 *
 * It also saves the value to be restored.
 */
unsigned long long osnoise_get_period(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	char *period_path;
	long long period_us;
	int retval;

	if (context->period_us != OSNOISE_TIME_INIT_VAL)
		return context->period_us;

	if (context->orig_period_us != OSNOISE_TIME_INIT_VAL)
		return context->orig_period_us;

	period_path = tracefs_get_tracing_file("osnoise/period_us");

	context->period_fd = open(period_path, O_RDWR);
	if (context->period_fd < 0)
		goto out_err;

	retval = read(context->period_fd, &buffer, sizeof(buffer));
	if (retval <= 0)
		goto out_close;

	period_us = get_llong_from_str(buffer);
	if (period_us < 0)
		goto out_close;

	tracefs_put_tracing_file(period_path);

	context->orig_period_us = period_us;
	return period_us;

out_close:
	close(context->period_fd);
	context->period_fd = CLOSED_FD;
out_err:
	tracefs_put_tracing_file(period_path);
	return 0;
}

static int __osnoise_write_runtime(struct osnoise_context *context,
				   unsigned long long runtime)
{
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (context->orig_runtime_us == OSNOISE_TIME_INIT_VAL)
		return -1;

	snprintf(buffer, sizeof(buffer), "%llu\n", runtime);

	retval = write(context->runtime_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		return -1;

	context->runtime_us = runtime;
	return 0;
}

static int __osnoise_write_period(struct osnoise_context *context,
				  unsigned long long period)
{
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (context->orig_period_us == OSNOISE_TIME_INIT_VAL)
		return -1;

	snprintf(buffer, sizeof(buffer), "%llu\n", period);

	retval = write(context->period_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		return -1;

	context->period_us = period;
	return 0;
}

/*
 * osnoise_set_runtime_period - set osnoise runtime and period
 *
 * Osnoise's runtime and period are related as runtime <= period.
 * Thus, this function saves the original values, and then tries
 * to set the runtime and period if they are != 0.
 */
int osnoise_set_runtime_period(struct osnoise_context *context,
			       unsigned long long runtime,
			       unsigned long long period)
{
	unsigned long long curr_runtime_us;
	unsigned long long curr_period_us;
	int retval;

	if (!period && !runtime)
		return 0;

	curr_runtime_us = osnoise_get_runtime(context);
	curr_period_us = osnoise_get_period(context);

	/* error getting any value? */
	if (curr_period_us == -1 || curr_runtime_us == -1)
		return -1;

	if (!period) {
		if (runtime > curr_period_us)
			return -1;
		return __osnoise_write_runtime(context, runtime);
	} else if (!runtime) {
		if (period < curr_runtime_us)
			return -1;
		return __osnoise_write_period(context, period);
	}

	if (runtime > curr_period_us) {
		retval = __osnoise_write_period(context, period);
		if (retval)
			return -1;
		retval = __osnoise_write_runtime(context, runtime);
		if (retval)
			return -1;
	} else {
		retval = __osnoise_write_runtime(context, runtime);
		if (retval)
			return -1;
		retval = __osnoise_write_period(context, period);
		if (retval)
			return -1;
	}

	return 0;
}

/*
 * osnoise_restore_runtime_period - restore the original runtime and period
 */
void osnoise_restore_runtime_period(struct osnoise_context *context)
{
	unsigned long long orig_runtime = context->orig_runtime_us;
	unsigned long long orig_period = context->orig_period_us;
	unsigned long long curr_runtime = context->runtime_us;
	unsigned long long curr_period = context->period_us;
	int retval;

	if ((orig_runtime == OSNOISE_TIME_INIT_VAL) && (orig_period == OSNOISE_TIME_INIT_VAL))
		return;

	if ((orig_period == curr_period) && (orig_runtime == curr_runtime))
		goto out_done;

	retval = osnoise_set_runtime_period(context, orig_runtime, orig_period);
	if (retval)
		err_msg("Could not restore original osnoise runtime/period\n");

out_done:
	context->runtime_us = OSNOISE_TIME_INIT_VAL;
	context->period_us = OSNOISE_TIME_INIT_VAL;
}

/*
 * osnoise_put_runtime_period - restore original values and cleanup data
 */
void osnoise_put_runtime_period(struct osnoise_context *context)
{
	osnoise_restore_runtime_period(context);

	if (context->orig_runtime_us != OSNOISE_TIME_INIT_VAL) {
		close(context->runtime_fd);
		context->runtime_fd = CLOSED_FD;
		context->orig_runtime_us = OSNOISE_TIME_INIT_VAL;
	}

	if (context->orig_period_us != OSNOISE_TIME_INIT_VAL) {
		close(context->period_fd);
		context->period_fd = CLOSED_FD;
		context->orig_period_us = OSNOISE_TIME_INIT_VAL;
	}
}

/*
 * osnoise_get_timerlat_period_us - read and save the original "timerlat_period_us"
 */
static long long
osnoise_get_timerlat_period_us(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	long long timerlat_period_us;
	char *stop_path;
	int retval;

	if (context->timerlat_period_us != OSNOISE_TIME_INIT_VAL)
		return context->timerlat_period_us;

	if (context->orig_timerlat_period_us != OSNOISE_TIME_INIT_VAL)
		return context->orig_timerlat_period_us;

	stop_path = tracefs_get_tracing_file("osnoise/timerlat_period_us");

	context->timerlat_period_us_fd = open(stop_path, O_RDWR);
	if (context->timerlat_period_us_fd < 0)
		goto out_err;

	retval = read(context->timerlat_period_us_fd, &buffer, sizeof(buffer));
	if (retval <= 0)
		goto out_close;

	timerlat_period_us = get_llong_from_str(buffer);
	if (timerlat_period_us < 0)
		goto out_close;

	tracefs_put_tracing_file(stop_path);

	context->orig_timerlat_period_us = timerlat_period_us;
	return timerlat_period_us;

out_close:
	close(context->timerlat_period_us_fd);
	context->timerlat_period_us_fd = CLOSED_FD;
out_err:
	tracefs_put_tracing_file(stop_path);
	return -1;
}

/*
 * osnoise_set_timerlat_period_us - set "timerlat_period_us"
 */
int osnoise_set_timerlat_period_us(struct osnoise_context *context, long long timerlat_period_us)
{
	long long curr_timerlat_period_us = osnoise_get_timerlat_period_us(context);
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (curr_timerlat_period_us == OSNOISE_TIME_INIT_VAL)
		return -1;

	snprintf(buffer, BUFF_U64_STR_SIZE, "%lld\n", timerlat_period_us);

	retval = write(context->timerlat_period_us_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		return -1;

	context->timerlat_period_us = timerlat_period_us;

	return 0;
}

/*
 * osnoise_restore_timerlat_period_us - restore "timerlat_period_us"
 */
void osnoise_restore_timerlat_period_us(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (context->orig_timerlat_period_us == OSNOISE_TIME_INIT_VAL)
		return;

	if (context->orig_timerlat_period_us == context->timerlat_period_us)
		goto out_done;

	snprintf(buffer, BUFF_U64_STR_SIZE, "%lld\n", context->orig_timerlat_period_us);

	retval = write(context->timerlat_period_us_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		err_msg("Could not restore original osnoise timerlat_period_us\n");

out_done:
	context->timerlat_period_us = OSNOISE_TIME_INIT_VAL;
}

/*
 * osnoise_put_timerlat_period_us - restore original values and cleanup data
 */
void osnoise_put_timerlat_period_us(struct osnoise_context *context)
{
	osnoise_restore_timerlat_period_us(context);

	if (context->orig_timerlat_period_us == OSNOISE_TIME_INIT_VAL)
		return;

	close(context->timerlat_period_us_fd);
	context->timerlat_period_us_fd = CLOSED_FD;
	context->orig_timerlat_period_us = OSNOISE_TIME_INIT_VAL;
}

/*
 * osnoise_get_stop_us - read and save the original "stop_tracing_us"
 */
static long long
osnoise_get_stop_us(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	long long stop_us;
	char *stop_path;
	int retval;

	if (context->stop_us != OSNOISE_OPTION_INIT_VAL)
		return context->stop_us;

	if (context->orig_stop_us != OSNOISE_OPTION_INIT_VAL)
		return context->orig_stop_us;

	stop_path = tracefs_get_tracing_file("osnoise/stop_tracing_us");

	context->stop_us_fd = open(stop_path, O_RDWR);
	if (context->stop_us_fd < 0)
		goto out_err;

	retval = read(context->stop_us_fd, &buffer, sizeof(buffer));
	if (retval <= 0)
		goto out_close;

	stop_us = get_llong_from_str(buffer);
	if (stop_us < 0)
		goto out_close;

	tracefs_put_tracing_file(stop_path);

	context->orig_stop_us = stop_us;
	return stop_us;

out_close:
	close(context->stop_us_fd);
	context->stop_us_fd = CLOSED_FD;
out_err:
	tracefs_put_tracing_file(stop_path);
	return -1;
}

/*
 * osnoise_set_stop_us - set "stop_tracing_us"
 */
int osnoise_set_stop_us(struct osnoise_context *context, long long stop_us)
{
	long long curr_stop_us = osnoise_get_stop_us(context);
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (curr_stop_us == OSNOISE_OPTION_INIT_VAL)
		return -1;

	snprintf(buffer, BUFF_U64_STR_SIZE, "%lld\n", stop_us);

	retval = write(context->stop_us_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		return -1;

	context->stop_us = stop_us;

	return 0;
}

/*
 * osnoise_restore_stop_us - restore the original "stop_tracing_us"
 */
void osnoise_restore_stop_us(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (context->orig_stop_us == OSNOISE_OPTION_INIT_VAL)
		return;

	if (context->orig_stop_us == context->stop_us)
		goto out_done;

	snprintf(buffer, BUFF_U64_STR_SIZE, "%lld\n", context->orig_stop_us);

	retval = write(context->stop_us_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		err_msg("Could not restore original osnoise stop_us\n");

out_done:
	context->stop_us = OSNOISE_OPTION_INIT_VAL;
}

/*
 * osnoise_put_stop_us - restore original values and cleanup data
 */
void osnoise_put_stop_us(struct osnoise_context *context)
{
	osnoise_restore_stop_us(context);

	if (context->orig_stop_us == OSNOISE_OPTION_INIT_VAL)
		return;

	close(context->stop_us_fd);
	context->stop_us_fd = CLOSED_FD;
	context->orig_stop_us = OSNOISE_OPTION_INIT_VAL;
}

/*
 * osnoise_get_stop_total_us - read and save the original "stop_tracing_total_us"
 */
static long long
osnoise_get_stop_total_us(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	long long stop_total_us;
	char *stop_path;
	int retval;

	if (context->stop_total_us != OSNOISE_OPTION_INIT_VAL)
		return context->stop_total_us;

	if (context->orig_stop_total_us != OSNOISE_OPTION_INIT_VAL)
		return context->orig_stop_total_us;

	stop_path = tracefs_get_tracing_file("osnoise/stop_tracing_total_us");

	context->stop_total_us_fd = open(stop_path, O_RDWR);
	if (context->stop_total_us_fd < 0)
		goto out_err;

	retval = read(context->stop_total_us_fd, &buffer, sizeof(buffer));
	if (retval <= 0)
		goto out_close;

	stop_total_us = get_llong_from_str(buffer);
	if (stop_total_us < 0)
		goto out_close;

	tracefs_put_tracing_file(stop_path);

	context->orig_stop_total_us = stop_total_us;
	return stop_total_us;

out_close:
	close(context->stop_total_us_fd);
	context->stop_total_us_fd = CLOSED_FD;
out_err:
	tracefs_put_tracing_file(stop_path);
	return -1;
}

/*
 * osnoise_set_stop_total_us - set "stop_tracing_total_us"
 */
int osnoise_set_stop_total_us(struct osnoise_context *context, long long stop_total_us)
{
	long long curr_stop_total_us = osnoise_get_stop_total_us(context);
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (curr_stop_total_us == OSNOISE_OPTION_INIT_VAL)
		return -1;

	snprintf(buffer, BUFF_U64_STR_SIZE, "%lld\n", stop_total_us);

	retval = write(context->stop_total_us_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		return -1;

	context->stop_total_us = stop_total_us;

	return 0;
}

/*
 * osnoise_restore_stop_total_us - restore the original "stop_tracing_total_us"
 */
void osnoise_restore_stop_total_us(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (context->orig_stop_total_us == OSNOISE_OPTION_INIT_VAL)
		return;

	if (context->orig_stop_total_us == context->stop_total_us)
		goto out_done;

	snprintf(buffer, BUFF_U64_STR_SIZE, "%lld\n", context->orig_stop_total_us);

	retval = write(context->stop_total_us_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		err_msg("Could not restore original osnoise stop_total_us\n");

out_done:
	context->stop_total_us = OSNOISE_OPTION_INIT_VAL;
}

/*
 * osnoise_put_stop_total_us - restore original values and cleanup data
 */
void osnoise_put_stop_total_us(struct osnoise_context *context)
{
	osnoise_restore_stop_total_us(context);

	if (context->orig_stop_total_us == OSNOISE_OPTION_INIT_VAL)
		return;

	close(context->stop_total_us_fd);
	context->stop_total_us_fd = CLOSED_FD;
	context->orig_stop_total_us = OSNOISE_OPTION_INIT_VAL;
}

/*
 * osnoise_get_print_stack - read and save the original "print_stack"
 */
static long long
osnoise_get_print_stack(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	long long print_stack;
	char *stop_path;
	int retval;

	if (context->print_stack != OSNOISE_OPTION_INIT_VAL)
		return context->print_stack;

	if (context->orig_print_stack != OSNOISE_OPTION_INIT_VAL)
		return context->orig_print_stack;

	stop_path = tracefs_get_tracing_file("osnoise/print_stack");

	context->print_stack_fd = open(stop_path, O_RDWR);
	if (context->print_stack_fd < 0)
		goto out_err;

	retval = read(context->print_stack_fd, &buffer, sizeof(buffer));
	if (retval <= 0)
		goto out_close;

	print_stack = get_llong_from_str(buffer);
	if (print_stack < 0)
		goto out_close;

	tracefs_put_tracing_file(stop_path);

	context->orig_print_stack = print_stack;
	return print_stack;

out_close:
	close(context->print_stack_fd);
	context->print_stack_fd = CLOSED_FD;
out_err:
	tracefs_put_tracing_file(stop_path);
	return -1;
}

/*
 * osnoise_set_print_stack - set "print_stack"
 */
int osnoise_set_print_stack(struct osnoise_context *context, long long print_stack)
{
	long long curr_print_stack = osnoise_get_print_stack(context);
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (curr_print_stack == OSNOISE_OPTION_INIT_VAL)
		return -1;

	snprintf(buffer, BUFF_U64_STR_SIZE, "%lld\n", print_stack);

	retval = write(context->print_stack_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		return -1;

	context->print_stack = print_stack;

	return 0;
}

/*
 * osnoise_restore_print_stack - restore the original "print_stack"
 */
void osnoise_restore_print_stack(struct osnoise_context *context)
{
	char buffer[BUFF_U64_STR_SIZE];
	int retval;

	if (context->orig_print_stack == OSNOISE_OPTION_INIT_VAL)
		return;

	if (context->orig_print_stack == context->print_stack)
		goto out_done;

	snprintf(buffer, BUFF_U64_STR_SIZE, "%lld\n", context->orig_print_stack);

	retval = write(context->print_stack_fd, buffer, strlen(buffer) + 1);
	if (retval < (strlen(buffer) + 1))
		err_msg("Could not restore original osnoise print_stack\n");

out_done:
	context->print_stack = OSNOISE_OPTION_INIT_VAL;
}

/*
 * osnoise_put_print_stack - restore original values and cleanup data
 */
void osnoise_put_print_stack(struct osnoise_context *context)
{
	osnoise_restore_print_stack(context);

	if (context->orig_print_stack == OSNOISE_OPTION_INIT_VAL)
		return;

	close(context->print_stack_fd);
	context->print_stack_fd = CLOSED_FD;
	context->orig_print_stack = OSNOISE_OPTION_INIT_VAL;
}

/*
 * enable_osnoise - enable osnoise tracer in the trace_instance
 */
int enable_osnoise(struct trace_instance *trace)
{
	return enable_tracer_by_name(trace->inst, "osnoise");
}

/*
 * enable_timerlat - enable timerlat tracer in the trace_instance
 */
int enable_timerlat(struct trace_instance *trace)
{
	return enable_tracer_by_name(trace->inst, "timerlat");
}

enum {
	FLAG_CONTEXT_NEWLY_CREATED	= (1 << 0),
	FLAG_CONTEXT_DELETED		= (1 << 1),
};

/*
 * osnoise_get_context - increase the usage of a context and return it
 */
int osnoise_get_context(struct osnoise_context *context)
{
	int ret;

	if (context->flags & FLAG_CONTEXT_DELETED) {
		ret = -1;
	} else {
		context->ref++;
		ret = 0;
	}

	return ret;
}

/*
 * osnoise_context_alloc - alloc an osnoise_context
 *
 * The osnoise context contains the information of the "osnoise/" configs.
 * It is used to set and restore the config.
 */
struct osnoise_context *osnoise_context_alloc(void)
{
	struct osnoise_context *context;

	context = calloc(1, sizeof(*context));
	if (!context)
		goto out_err;

	context->cpus_fd 		= CLOSED_FD;
	context->runtime_fd		= CLOSED_FD;
	context->period_fd		= CLOSED_FD;
	context->stop_us_fd		= CLOSED_FD;
	context->stop_total_us_fd	= CLOSED_FD;
	context->timerlat_period_us_fd	= CLOSED_FD;
	context->print_stack_fd		= CLOSED_FD;

	context->orig_stop_us		= OSNOISE_OPTION_INIT_VAL;
	context->stop_us		= OSNOISE_OPTION_INIT_VAL;

	context->orig_stop_total_us	= OSNOISE_OPTION_INIT_VAL;
	context->stop_total_us		= OSNOISE_OPTION_INIT_VAL;

	context->orig_print_stack	= OSNOISE_OPTION_INIT_VAL;
	context->print_stack		= OSNOISE_OPTION_INIT_VAL;

	osnoise_get_context(context);

	return context;
out_err:
	if (context)
		free(context);
	return NULL;
}

/*
 * osnoise_put_context - put the osnoise_put_context
 *
 * If there is no other user for the context, the original data
 * is restored.
 */
void osnoise_put_context(struct osnoise_context *context)
{
	if (--context->ref < 1)
		context->flags |= FLAG_CONTEXT_DELETED;

	if (!(context->flags & FLAG_CONTEXT_DELETED))
		return;

	osnoise_put_cpus(context);
	osnoise_put_runtime_period(context);
	osnoise_put_stop_us(context);
	osnoise_put_stop_total_us(context);
	osnoise_put_timerlat_period_us(context);
	osnoise_put_print_stack(context);

	free(context);
}

/*
 * osnoise_destroy_tool - disable trace, restore configs and free data
 */
void osnoise_destroy_tool(struct osnoise_tool *top)
{
	trace_instance_destroy(&top->trace);

	if (top->context)
		osnoise_put_context(top->context);

	free(top);
}

/*
 * osnoise_init_tool - init an osnoise tool
 *
 * It allocs data, create a context to store data and
 * creates a new trace instance for the tool.
 */
struct osnoise_tool *osnoise_init_tool(char *tool_name)
{
	struct osnoise_tool *top;
	int retval;

	top = calloc(1, sizeof(*top));
	if (!top)
		return NULL;

	top->context = osnoise_context_alloc();
	if (!top->context)
		goto out_err;

	retval = trace_instance_init(&top->trace, tool_name);
	if (retval)
		goto out_err;

	return top;
out_err:
	osnoise_destroy_tool(top);
	return NULL;
}

/*
 * osnoise_init_trace_tool - init a tracer instance to trace osnoise events
 */
struct osnoise_tool *osnoise_init_trace_tool(char *tracer)
{
	struct osnoise_tool *trace;
	int retval;

	trace = osnoise_init_tool("osnoise_trace");
	if (!trace)
		return NULL;

	retval = tracefs_event_enable(trace->trace.inst, "osnoise", NULL);
	if (retval < 0 && !errno) {
		err_msg("Could not find osnoise events\n");
		goto out_err;
	}

	retval = enable_tracer_by_name(trace->trace.inst, tracer);
	if (retval) {
		err_msg("Could not enable osnoiser tracer for tracing\n");
		goto out_err;
	}

	return trace;
out_err:
	osnoise_destroy_tool(trace);
	return NULL;
}

static void osnoise_usage(void)
{
	int i;

	static const char *msg[] = {
		"",
		"osnoise version " VERSION,
		"",
		"  usage: [rtla] osnoise [MODE] ...",
		"",
		"  modes:",
		"     top  - prints the summary from osnoise tracer",
		"",
		"if no MODE is given, the top mode is called, passing the arguments",
		NULL,
	};

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(1);
}

int osnoise_main(int argc, char *argv[])
{
	if (argc == 0)
		goto usage;

	/*
	 * if osnoise was called without any argument, run the
	 * default cmdline.
	 */
	if (argc == 1) {
		osnoise_top_main(argc, argv);
		exit(0);
	}

	if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
		osnoise_usage();
		exit(0);
	} else if (strncmp(argv[1], "-", 1) == 0) {
		/* the user skipped the tool, call the default one */
		osnoise_top_main(argc, argv);
		exit(0);
	} else if (strcmp(argv[1], "top") == 0) {
		osnoise_top_main(argc-1, &argv[1]);
		exit(0);
	}

usage:
	osnoise_usage();
	exit(1);
}
