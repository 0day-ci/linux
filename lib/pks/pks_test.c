// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

/**
 * DOC: PKS_TEST
 *
 * When CONFIG_PKS_TEST is enabled a debugfs file is created to facilitate in
 * kernel testing.  Tests can be triggered by writing a test number to
 * /sys/kernel/debug/x86/run_pks
 *
 * Results and debug output can be seen through dynamic debug.
 *
 * Example:
 *
 * .. code-block:: sh
 *
 *	# Enable kernel debug
 *	echo "file pks_test.c +pflm" > /sys/kernel/debug/dynamic_debug/control
 *
 *	# Run test
 *	echo 0 > /sys/kernel/debug/x86/run_pks
 *
 *	# Turn off kernel debug
 *	echo "file pks_test.c -p" > /sys/kernel/debug/dynamic_debug/control
 *
 *	# view kernel debugging output
 *	dmesg -H | grep pks_test
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pks-keys.h>

#define PKS_TEST_MEM_SIZE (PAGE_SIZE)

#define CHECK_DEFAULTS		0
#define RUN_CRASH_TEST		9

static struct dentry *pks_test_dentry;

DEFINE_MUTEX(test_run_lock);

struct pks_test_ctx {
	u8 pkey;
	char data[64];
	void *test_page;
};

static void debug_context(const char *label, struct pks_test_ctx *ctx)
{
	pr_debug("%s [%d] %s <-> %p\n",
		     label,
		     ctx->pkey,
		     ctx->data,
		     ctx->test_page);
}

struct pks_session_data {
	struct pks_test_ctx *ctx;
	bool need_unlock;
	bool crash_armed;
	bool last_test_pass;
};

static void debug_session(const char *label, struct pks_session_data *sd)
{
	pr_debug("%s ctx %p; unlock %d; crash %d; last test %s\n",
		     label,
		     sd->ctx,
		     sd->need_unlock,
		     sd->crash_armed,
		     sd->last_test_pass ? "PASS" : "FAIL");

}

static void debug_result(const char *label, int test_num,
			 struct pks_session_data *sd)
{
	pr_debug("%s [%d]: %s\n",
		     label, test_num,
		     sd->last_test_pass ? "PASS" : "FAIL");
}

bool pks_test_fault_callback(struct pt_regs *regs, unsigned long address,
			     bool write)
{
	return false;
}

static void *alloc_test_page(u8 pkey)
{
	return __vmalloc_node_range(PKS_TEST_MEM_SIZE, 1, VMALLOC_START,
				    VMALLOC_END, GFP_KERNEL,
				    PAGE_KERNEL_PKEY(pkey), 0,
				    NUMA_NO_NODE, __builtin_return_address(0));
}

static void free_ctx(struct pks_test_ctx *ctx)
{
	if (!ctx)
		return;

	vfree(ctx->test_page);
	kfree(ctx);
}

static struct pks_test_ctx *alloc_ctx(u8 pkey)
{
	struct pks_test_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->pkey = pkey;
	sprintf(ctx->data, "%s", "DEADBEEF");

	ctx->test_page = alloc_test_page(ctx->pkey);
	if (!ctx->test_page) {
		pr_debug("Test page allocation failed\n");
		kfree(ctx);
		return ERR_PTR(-ENOMEM);
	}

	debug_context("Context allocated", ctx);
	return ctx;
}

static void set_ctx_data(struct pks_session_data *sd, struct pks_test_ctx *ctx)
{
	if (sd->ctx) {
		pr_debug("Context data already set\n");
		free_ctx(sd->ctx);
	}
	pr_debug("Setting context data; %p\n", ctx);
	sd->ctx = ctx;
}

static void crash_it(struct pks_session_data *sd)
{
	struct pks_test_ctx *ctx;

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx)) {
		pr_err("Failed to allocate context???\n");
		sd->last_test_pass = false;
		return;
	}
	set_ctx_data(sd, ctx);

	pr_debug("Purposely faulting...\n");
	memcpy(ctx->test_page, ctx->data, 8);

	pr_err("ERROR: Should never get here...\n");
	sd->last_test_pass = false;
}

static void check_pkey_settings(void *data)
{
	struct pks_session_data *sd = data;
	unsigned long long msr = 0;
	unsigned int cpu = smp_processor_id();

	rdmsrl(MSR_IA32_PKRS, msr);
	pr_debug("cpu %d 0x%llx\n", cpu, msr);
	if (msr != PKS_INIT_VALUE) {
		pr_err("cpu %d value incorrect : 0x%llx expected 0x%lx\n",
			cpu, msr, PKS_INIT_VALUE);
		sd->last_test_pass = false;
	}
}

static void arm_or_run_crash_test(struct pks_session_data *sd)
{

	/*
	 * WARNING: Test "9" will crash.
	 * Arm the test.
	 * A second "9" will run the test.
	 */
	if (!sd->crash_armed) {
		pr_debug("Arming crash test\n");
		sd->crash_armed = true;
		return;
	}

	sd->crash_armed = false;
	crash_it(sd);
}

static ssize_t pks_read_file(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct pks_session_data *sd = file->private_data;
	char buf[64];
	unsigned int len;

	len = sprintf(buf, "%s\n", sd->last_test_pass ? "PASS" : "FAIL");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t pks_write_file(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct pks_session_data *sd = file->private_data;
	long test_num;
	char buf[2];

	pr_debug("Begin...\n");
	sd->last_test_pass = false;

	if (copy_from_user(buf, user_buf, 1))
		return -EFAULT;
	buf[1] = '\0';

	if (kstrtol(buf, 0, &test_num))
		return -EINVAL;

	if (mutex_lock_interruptible(&test_run_lock))
		return -EBUSY;

	sd->need_unlock = true;
	sd->last_test_pass = true;

	switch (test_num) {
	case RUN_CRASH_TEST:
		pr_debug("crash test\n");
		arm_or_run_crash_test(file->private_data);
		goto unlock_test;
	case CHECK_DEFAULTS:
		pr_debug("check defaults test: 0x%lx\n", PKS_INIT_VALUE);
		on_each_cpu(check_pkey_settings, file->private_data, 1);
		break;
	default:
		pr_debug("Unknown test\n");
		sd->last_test_pass = false;
		count = -ENOENT;
		break;
	}

	/* Clear arming on any test run */
	pr_debug("Clearing crash test arm\n");
	sd->crash_armed = false;

unlock_test:
	/*
	 * Normal exit; clear up the locking flag
	 */
	sd->need_unlock = false;
	mutex_unlock(&test_run_lock);
	debug_result("Test complete", test_num, sd);
	return count;
}

static int pks_open_file(struct inode *inode, struct file *file)
{
	struct pks_session_data *sd = kzalloc(sizeof(*sd), GFP_KERNEL);

	if (!sd)
		return -ENOMEM;

	debug_session("Allocated session", sd);
	file->private_data = sd;

	return 0;
}

static int pks_release_file(struct inode *inode, struct file *file)
{
	struct pks_session_data *sd = file->private_data;

	debug_session("Freeing session", sd);

	/*
	 * Some tests may fault and not return through the normal write
	 * syscall.  The crash test is specifically designed to do this.  Clean
	 * up the run lock when the file is closed if the write syscall does
	 * not exit normally.
	 */
	if (sd->need_unlock)
		mutex_unlock(&test_run_lock);
	free_ctx(sd->ctx);
	kfree(sd);
	return 0;
}

static const struct file_operations fops_init_pks = {
	.read = pks_read_file,
	.write = pks_write_file,
	.llseek = default_llseek,
	.open = pks_open_file,
	.release = pks_release_file,
};

static int __init pks_test_init(void)
{
	if (cpu_feature_enabled(X86_FEATURE_PKS))
		pks_test_dentry = debugfs_create_file("run_pks", 0600, arch_debugfs_dir,
						      NULL, &fops_init_pks);

	return 0;
}
late_initcall(pks_test_init);
