// SPDX-License-Identifier: GPL-2.0
/*
 * counter_emulation_benchmark.c -- test to measure the effects of counter
 * emulation on guest reads of the physical counter.
 *
 * Copyright (c) 2021, Google LLC.
 */

#define _GNU_SOURCE
#include <asm/kvm.h>
#include <linux/kvm.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

#define VCPU_ID 0

static struct counter_values {
	uint64_t cntvct_start;
	uint64_t cntpct;
	uint64_t cntvct_end;
} counter_values;

static uint64_t nr_iterations = 1000;

static void do_test(void)
{
	/*
	 * Open-coded approach instead of using helper methods to keep a tight
	 * interval around the physical counter read.
	 */
	asm volatile("isb\n\t"
		     "mrs %[cntvct_start], cntvct_el0\n\t"
		     "isb\n\t"
		     "mrs %[cntpct], cntpct_el0\n\t"
		     "isb\n\t"
		     "mrs %[cntvct_end], cntvct_el0\n\t"
		     "isb\n\t"
		     : [cntvct_start] "=r"(counter_values.cntvct_start),
		     [cntpct] "=r"(counter_values.cntpct),
		     [cntvct_end] "=r"(counter_values.cntvct_end));
}

static void guest_main(void)
{
	int i;

	for (i = 0; i < nr_iterations; i++) {
		do_test();
		GUEST_SYNC(i);
	}

	for (i = 0; i < nr_iterations; i++) {
		do_test();
		GUEST_SYNC(i);
	}
}

static void enter_guest(struct kvm_vm *vm)
{
	struct ucall uc;

	vcpu_ioctl(vm, VCPU_ID, KVM_RUN, NULL);

	switch (get_ucall(vm, VCPU_ID, &uc)) {
	case UCALL_SYNC:
		break;
	case UCALL_ABORT:
		TEST_ASSERT(false, "%s at %s:%ld", (const char *)uc.args[0],
			    __FILE__, uc.args[1]);
		break;
	default:
		TEST_ASSERT(false, "unexpected exit: %s",
			    exit_reason_str(vcpu_state(vm, VCPU_ID)->exit_reason));
		break;
	}
}

static double counter_frequency(void)
{
	uint32_t freq;

	asm volatile("mrs %0, cntfrq_el0"
		     : "=r" (freq));

	return freq / 1000000.0;
}

static void log_csv(FILE *csv, bool trapped)
{
	double freq = counter_frequency();

	fprintf(csv, "%s,%.02f,%lu,%lu,%lu\n",
		trapped ? "true" : "false", freq,
		counter_values.cntvct_start,
		counter_values.cntpct,
		counter_values.cntvct_end);
}

static double run_loop(struct kvm_vm *vm, FILE *csv, bool trapped)
{
	double avg = 0;
	int i;

	for (i = 0; i < nr_iterations; i++) {
		uint64_t delta;

		enter_guest(vm);
		sync_global_from_guest(vm, counter_values);

		if (csv)
			log_csv(csv, trapped);

		delta = counter_values.cntvct_end - counter_values.cntvct_start;
		avg = ((avg * i) + delta) / (i + 1);
	}

	return avg;
}

static void setup_counter(struct kvm_vm *vm, uint64_t offset)
{
	vcpu_access_device_attr(vm, VCPU_ID, KVM_ARM_VCPU_TIMER_CTRL,
				KVM_ARM_VCPU_TIMER_PHYS_OFFSET, &offset,
				true);
}

static void run_tests(struct kvm_vm *vm, FILE *csv)
{
	double avg_trapped, avg_native, freq;

	freq = counter_frequency();

	if (csv)
		fputs("trapped,freq_mhz,cntvct_start,cntpct,cntvct_end\n", csv);

	/* no physical offsetting; kvm allows reads of cntpct_el0 */
	setup_counter(vm, 0);
	avg_native = run_loop(vm, csv, false);

	/* force emulation of the physical counter */
	setup_counter(vm, 1);
	avg_trapped = run_loop(vm, csv, true);

	pr_info("%lu iterations: average cycles (@%.02fMHz) native: %.02f, trapped: %.02f\n",
		nr_iterations, freq, avg_native, avg_trapped);
}

static void usage(const char *program_name)
{
	fprintf(stderr,
		"Usage: %s [-h] [-o csv_file] [-n iterations]\n"
		"  -h prints this message\n"
		"  -n number of test iterations (default: %lu)\n"
		"  -o csv file to write data\n",
		program_name, nr_iterations);
}

int main(int argc, char **argv)
{
	struct kvm_vm *vm;
	FILE *csv = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "hn:o:")) != -1) {
		switch (opt) {
		case 'o':
			csv = fopen(optarg, "w");
			if (!csv) {
				fprintf(stderr, "failed to open file '%s': %d\n",
					optarg, errno);
				exit(1);
			}
			break;
		case 'n':
			nr_iterations = strtoul(optarg, NULL, 0);
			break;
		default:
			fprintf(stderr, "unrecognized option: '-%c'\n", opt);
			/* fallthrough */
		case 'h':
			usage(argv[0]);
			exit(1);
		}
	}

	vm = vm_create_default(VCPU_ID, 0, guest_main);
	sync_global_to_guest(vm, nr_iterations);
	ucall_init(vm, NULL);

	if (_vcpu_has_device_attr(vm, VCPU_ID, KVM_ARM_VCPU_TIMER_CTRL,
				  KVM_ARM_VCPU_TIMER_PHYS_OFFSET)) {
		print_skip("KVM_ARM_VCPU_TIMER_PHYS_OFFSET not supported.");
		exit(KSFT_SKIP);
	}

	run_tests(vm, csv);
	kvm_vm_free(vm);

	if (csv)
		fclose(csv);
}
