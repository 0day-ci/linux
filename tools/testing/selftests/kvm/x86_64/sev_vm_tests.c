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
#include "kvm_util.h"
#include "kselftest.h"
#include "../lib/kvm_util_internal.h"

#define SEV_DEV_PATH "/dev/sev"

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

static void sev_ioctl(int fd, int cmd_id, void *data)
{
	struct kvm_sev_cmd cmd = { 0 };
	int ret;

	TEST_ASSERT(cmd_id < KVM_SEV_NR_MAX, "Unknown SEV CMD : %d\n", cmd_id);

	cmd.id = cmd_id;
	cmd.sev_fd = open_sev_dev_path_or_exit(0);
	cmd.data = (uint64_t)data;
	ret = ioctl(fd, KVM_MEMORY_ENCRYPT_OP, &cmd);
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
	for (i = 0; i < 3; ++i)
		vm_vcpu_add(vm, i);
	start.policy |= (es) << 2;
	sev_ioctl(vm->fd, KVM_SEV_LAUNCH_START, &start);
	if (es)
		sev_ioctl(vm->fd, KVM_SEV_LAUNCH_UPDATE_VMSA, NULL);
	return vm;
}

static void test_sev_migrate_from(bool es)
{
	struct kvm_vm *vms[3];
	struct kvm_enable_cap cap = { 0 };
	int i;

	for (i = 0; i < sizeof(vms) / sizeof(struct kvm_vm *); ++i)
		vms[i] = sev_vm_create(es);

	cap.cap = KVM_CAP_VM_MIGRATE_ENC_CONTEXT_FROM;
	for (i = 0; i < sizeof(vms) / sizeof(struct kvm_vm *) - 1; ++i) {
		cap.args[0] = vms[i]->fd;
		vm_enable_cap(vms[i + 1], &cap);
	}
}

#define LOCK_TESTING_THREADS 3

struct locking_thread_input {
	struct kvm_vm *vm;
	int source_fds[LOCK_TESTING_THREADS];
};

static void *locking_test_thread(void *arg)
{
	struct kvm_enable_cap cap = { 0 };
	int i, j;
	struct locking_thread_input *input = (struct locking_test_thread *)arg;

	cap.cap = KVM_CAP_VM_MIGRATE_ENC_CONTEXT_FROM;

	for (i = 0; i < 1000; ++i) {
		j = input->source_fds[i % LOCK_TESTING_THREADS];
		cap.args[0] = input->source_fds[j];
		/*
		 * Call IOCTL directly without checking return code. We are
		 * simply trying to confirm there is no deadlock from userspace
		 * not check correctness of migration here.
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
	memcpy(input[1].source_fds, input[0].source_fds,
	       sizeof(input[1].source_fds));
	memcpy(input[2].source_fds, input[0].source_fds,
	       sizeof(input[2].source_fds));

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
