// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 */

/**
 * DOC: PKS_TEST
 *
 * If CONFIG_PKS_TEST is enabled a debugfs file is created to initiate in
 * kernel testing.  These can be triggered by:
 *
 * $ echo X > /sys/kernel/debug/x86/run_pks
 *
 * where X is:
 *
 * * 0  Loop through all CPUs, report the msr, and check against the default.
 * * 9  Set up and fault on a PKS protected page.
 *
 * NOTE: 9 will fault on purpose.  Therefore, it requires the option to be
 * specified 2 times in a row to ensure the intent to run it.
 *
 * $ cat /sys/kernel/debug/x86/run_pks
 *
 * Will print the result of the last test.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pkeys.h>

#define PKS_TEST_MEM_SIZE (PAGE_SIZE)

#define CHECK_DEFAULTS		0
#define RUN_CRASH_TEST		9

static struct dentry *pks_test_dentry;
static bool crash_armed;

static bool last_test_pass;

struct pks_test_ctx {
	int pkey;
	char data[64];
};

static void *alloc_test_page(int pkey)
{
	return __vmalloc_node_range(PKS_TEST_MEM_SIZE, 1, VMALLOC_START, VMALLOC_END,
				    GFP_KERNEL, PAGE_KERNEL_PKEY(pkey), 0,
				    NUMA_NO_NODE, __builtin_return_address(0));
}

static struct pks_test_ctx *alloc_ctx(u8 pkey)
{
	struct pks_test_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (!ctx) {
		pr_err("Failed to allocate memory for test context\n");
		return ERR_PTR(-ENOMEM);
	}

	ctx->pkey = pkey;
	sprintf(ctx->data, "%s", "DEADBEEF");
	return ctx;
}

static void free_ctx(struct pks_test_ctx *ctx)
{
	kfree(ctx);
}

static void crash_it(void)
{
	struct pks_test_ctx *ctx;
	void *ptr;

	pr_warn("     ***** BEGIN: Unhandled fault test *****\n");

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx)) {
		pr_err("Failed to allocate context???\n");
		return;
	}

	ptr = alloc_test_page(ctx->pkey);
	if (!ptr) {
		pr_err("Failed to vmalloc page???\n");
		return;
	}

	/* This purposely faults */
	memcpy(ptr, ctx->data, 8);

	/* Should never get here if so the test failed */
	last_test_pass = false;

	vfree(ptr);
	free_ctx(ctx);
}

static void check_pkey_settings(void *data)
{
	unsigned long long msr = 0;
	unsigned int cpu = smp_processor_id();

	rdmsrl(MSR_IA32_PKRS, msr);
	if (msr != PKS_INIT_VALUE) {
		pr_err("cpu %d value incorrect : 0x%llx expected 0x%x\n",
			cpu, msr, PKS_INIT_VALUE);
		last_test_pass = false;
	}
}

static void arm_or_run_crash_test(void)
{
	/*
	 * WARNING: Test "9" will crash.
	 *
	 * Arm the test and print a warning.  A second "9" will run the test.
	 */
	if (!crash_armed) {
		pr_warn("CAUTION: The crash test will cause an oops.\n");
		pr_warn("         Specify 9 a second time to run\n");
		pr_warn("         run any other test to clear\n");
		crash_armed = true;
		return;
	}

	crash_it();
	crash_armed = false;
}

static ssize_t pks_read_file(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[64];
	unsigned int len;

	len = sprintf(buf, "%s\n", last_test_pass ? "PASS" : "FAIL");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t pks_write_file(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	int rc;
	long option;
	char buf[2];

	if (copy_from_user(buf, user_buf, 1)) {
		last_test_pass = false;
		return -EFAULT;
	}
	buf[1] = '\0';

	rc = kstrtol(buf, 0, &option);
	if (rc) {
		last_test_pass = false;
		return count;
	}

	last_test_pass = true;

	switch (option) {
	case RUN_CRASH_TEST:
		arm_or_run_crash_test();
		goto skip_arm_clearing;
	case CHECK_DEFAULTS:
		on_each_cpu(check_pkey_settings, NULL, 1);
		break;
	default:
		last_test_pass = false;
		break;
	}

	/* Clear arming on any test run */
	crash_armed = false;

skip_arm_clearing:
	return count;
}

static int pks_release_file(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations fops_init_pks = {
	.read = pks_read_file,
	.write = pks_write_file,
	.llseek = default_llseek,
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

static void __exit pks_test_exit(void)
{
	debugfs_remove(pks_test_dentry);
	pr_info("test exit\n");
}
