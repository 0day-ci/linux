// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kvm.h>
#include <linux/psp-sev.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "kselftest.h"
#include "../lib/kvm_util_internal.h"

#define SEV_DEV_PATH "/dev/sev"

#define MIGRATE_TEST_NUM_VCPUS 4
#define MIGRATE_TEST_VMS 3
#define LOCK_TESTING_THREADS 3
#define LOCK_TESTING_ITERATIONS 10000

/*
 * Open SEV_DEV_PATH if available, otherwise exit the entire program.
 *
 * Input Args:
 *   flags - The flags to pass when opening SEV_DEV_PATH.
 *
 * Return:
 *   The opened file descriptor of /dev/sev.
 */
static int open_sev_dev_path_or_exit(int flags)
{
	static int fd;

	if (fd != 0)
		return fd;

	fd = open(SEV_DEV_PATH, flags);
	if (fd < 0) {
		print_skip("%s not available, is SEV not enabled? (errno: %d)",
			   SEV_DEV_PATH, errno);
		exit(KSFT_SKIP);
	}

	return fd;
}

static void sev_ioctl(int vm_fd, int cmd_id, void *data)
{
	struct kvm_sev_cmd cmd = {
		.id = cmd_id,
		.data = (uint64_t)data,
		.sev_fd = open_sev_dev_path_or_exit(0),
	};
	int ret;

	TEST_ASSERT(cmd_id < KVM_SEV_NR_MAX && cmd_id >= 0,
		    "Unknown SEV CMD : %d\n", cmd_id);

	ret = ioctl(vm_fd, KVM_MEMORY_ENCRYPT_OP, &cmd);
	TEST_ASSERT((ret == 0 || cmd.error == SEV_RET_SUCCESS),
		    "%d failed: return code: %d, errno: %d, fw error: %d",
		    cmd_id, ret, errno, cmd.error);
}

static struct kvm_vm *sev_vm_create(bool es)
{
	struct kvm_vm *vm;
	struct kvm_sev_launch_start start = { 0 };
	int i;

	vm = vm_create(VM_MODE_DEFAULT, 0, O_RDWR);
	sev_ioctl(vm->fd, es ? KVM_SEV_ES_INIT : KVM_SEV_INIT, NULL);
	for (i = 0; i < MIGRATE_TEST_NUM_VCPUS; ++i)
		vm_vcpu_add(vm, i);
	start.policy |= (es) << 2;
	sev_ioctl(vm->fd, KVM_SEV_LAUNCH_START, &start);
	if (es)
		sev_ioctl(vm->fd, KVM_SEV_LAUNCH_UPDATE_VMSA, NULL);
	return vm;
}

static void test_sev_migrate_from(bool es)
{
	struct kvm_vm *vms[MIGRATE_TEST_VMS];
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_VM_MIGRATE_ENC_CONTEXT_FROM
	};
	int i;

	for (i = 0; i < MIGRATE_TEST_VMS; ++i) {
		vms[i] = sev_vm_create(es);
		if (i > 0) {
			cap.args[0] = vms[i - 1]->fd;
			vm_enable_cap(vms[i], &cap);
		}
	}
}

struct locking_thread_input {
	struct kvm_vm *vm;
	int source_fds[LOCK_TESTING_THREADS];
};

static void *locking_test_thread(void *arg)
{
	/*
	 * This test case runs a number of threads all trying to use the intra
	 * host migration ioctls. This tries to detect if a deadlock exists.
	 */
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_VM_MIGRATE_ENC_CONTEXT_FROM
	};
	int i, j;
	struct locking_thread_input *input = (struct locking_test_thread *)arg;

	for (i = 0; i < LOCK_TESTING_ITERATIONS; ++i) {
		j = input->source_fds[i % LOCK_TESTING_THREADS];
		cap.args[0] = input->source_fds[j];
		/*
		 * Call IOCTL directly without checking return code or
		 * asserting. We are * simply trying to confirm there is no
		 * deadlock from userspace * not check correctness of
		 * migration here.
		 */
		ioctl(input->vm->fd, KVM_ENABLE_CAP, &cap);
	}
}

static void test_sev_migrate_locking(void)
{
	struct locking_thread_input input[LOCK_TESTING_THREADS];
	pthread_t pt[LOCK_TESTING_THREADS];
	int i;

	for (i = 0; i < LOCK_TESTING_THREADS; ++i) {
		input[i].vm = sev_vm_create(/* es= */ false);
		input[0].source_fds[i] = input[i].vm->fd;
	}
	for (i = 1; i < LOCK_TESTING_THREADS; ++i)
		memcpy(input[i].source_fds, input[0].source_fds,
		       sizeof(input[i].source_fds));

	for (i = 0; i < LOCK_TESTING_THREADS; ++i)
		pthread_create(&pt[i], NULL, locking_test_thread, &input[i]);

	for (i = 0; i < LOCK_TESTING_THREADS; ++i)
		pthread_join(pt[i], NULL);
}

int main(int argc, char *argv[])
{
	test_sev_migrate_from(/* es= */ false);
	test_sev_migrate_from(/* es= */ true);
	test_sev_migrate_locking();
	return 0;
}
