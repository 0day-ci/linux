// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <pthread.h>
#include <stdio.h>
#include <signal.h>

#define DEBUG
#include "kvm_util.h"
#include "test_util.h"
#include "processor.h"

#define H_PUT_TERM_CHAR 0x58
#define TEST_VAL 0x8badf00d
#define PASS_VAL 0xdeadbeef
#define FAIL_VAL 0x2badd00d


struct kvm_vm *vm;

/*
 * Call the hypervisor to write a character to the console. KVM does
 * not handle this hypercall so it goes out to userspace. Which in
 * this case is the vcpu_worker() below.
 */
static inline void put_char(char c)
{
	hcall(H_PUT_TERM_CHAR, 0, 1, cpu_to_be64(c));
}

static void guest_code(uint64_t *ptr, uint64_t val)
{
	/*
	 * Test making a hypercall and give a visual indication that
	 * the guest code is running.
	 */
	put_char('.');

	/* Make sure we can receive values */
	GUEST_ASSERT(ptr);
	GUEST_ASSERT(val == TEST_VAL);

	put_char('.');

	/* Read/write to memory */
	if (*ptr == val)
		*ptr = PASS_VAL;
	else
		*ptr = FAIL_VAL;

	put_char('.');

	/* Signal we're done */
	GUEST_DONE();
}

static bool guest_done(struct kvm_vm *vm)
{
	struct ucall uc;
	bool done;

	switch (get_ucall(vm, 0, &uc)) {
	case UCALL_ABORT:
		TEST_FAIL("%s at %s:%ld", (const char *)uc.args[0],
			  __FILE__, uc.args[1]);
		/* not reached */
	case UCALL_DONE:
		done = true;
		break;
	default:
		done = false;
		break;
	}

	return done;
}

static void *vcpu_worker(void *data)
{
	struct kvm_vm *vm = data;
	struct kvm_run *run;
	uint64_t *hva;
	static uint64_t test_buf = TEST_VAL;

	/* Pass arguments to the guest code */
	vcpu_args_set(vm, 0, 2, &test_buf, TEST_VAL);

	run = vcpu_state(vm, 0);
	while (1) {
		vcpu_run(vm, 0);

		if (guest_done(vm))
			break;

		switch (run->exit_reason) {
		case KVM_EXIT_PAPR_HCALL:
			if (run->papr_hcall.nr == H_PUT_TERM_CHAR) {
				char c = be64_to_cpu(run->papr_hcall.args[2]);

				pr_debug("%c", c);
			}
			break;
		default:
			printf("exit reason: %s\n", exit_reason_str(run->exit_reason));
			break;
		}
	}

	hva = addr_gva2hva(vm, (vm_vaddr_t)&test_buf);
	TEST_ASSERT(*hva != FAIL_VAL,
		    "Guest failed to read test value at gva %p", &test_buf);
	TEST_ASSERT(*hva == PASS_VAL,
		    "Guest failed to write test value to gva %p", &test_buf);

	pr_debug("PASS\n");

	return NULL;
}

void dump_vm(int sig)
{
	vm_dump(stderr, vm, 2);
	exit(1);
}

int main(int argc, char *argv[])
{
	pthread_t vcpu_thread;

	signal(SIGINT, dump_vm);

	/*
	 * Do not buffer stdout so we can implement put_char without
	 * flushing.
	 */
	setbuf(stdout, NULL);

	vm = vm_create_default(0, 0, guest_code);
	pthread_create(&vcpu_thread, NULL, vcpu_worker, vm);

	pthread_join(vcpu_thread, NULL);
	kvm_vm_free(vm);

	return 0;
}
