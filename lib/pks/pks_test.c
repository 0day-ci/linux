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
 * * 1  Allocate a single key and check all 3 permissions on a page.
 * * 2  'arm context' for context switch test
 * * 3  Check the context armed in '2' to ensure the MSR value was preserved
 * * 4  Test that the exception thread PKRS remains independent of the
 *      interrupted threads PKRS
 * * 5  Test setting a key to RD/WR in a fault callback to abandon a key
 * * 8  Loop through all CPUs, report the msr, and check against the default.
 * * 9  Set up and fault on a PKS protected page.
 *
 * NOTE: 9 will fault on purpose.  Therefore, it requires the option to be
 * specified 2 times in a row to ensure the intent to run it.
 *
 * $ cat /sys/kernel/debug/x86/run_pks
 *
 * Will print the result of the last test.
 *
 * To automate context switch testing a user space program is provided in:
 *
 *	.../tools/testing/selftests/x86/test_pks.c
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pkeys.h>
#include <uapi/asm-generic/mman-common.h>

#include <asm/pks.h>
#include <asm/ptrace.h>       /* for struct pt_regs */

#include <asm/pks.h>

#define PKS_TEST_MEM_SIZE (PAGE_SIZE)

#define CHECK_DEFAULTS		0
#define RUN_SINGLE		1
#define ARM_CTX_SWITCH		2
#define CHECK_CTX_SWITCH	3
#define RUN_EXCEPTION		4
#define RUN_FAULT_ABANDON	5
#define RUN_CRASH_TEST		9

DECLARE_PER_CPU(u32, pkrs_cache);

static struct dentry *pks_test_dentry;
static bool crash_armed;

static bool last_test_pass;
static int test_armed_key;
static int fault_cnt;
static int prev_fault_cnt;

struct pks_test_ctx {
	int pkey;
	bool pass;
	char data[64];
};
static struct pks_test_ctx *test_exception_ctx;

static bool check_pkey_val(u32 pk_reg, int pkey, u32 expected)
{
	pk_reg = (pk_reg >> PKR_PKEY_SHIFT(pkey)) & PKEY_ACCESS_MASK;
	return (pk_reg == expected);
}

/*
 * Check if the register @pkey value matches @expected value
 *
 * Both the cached and actual MSR must match.
 */
static bool check_pkrs(int pkey, u32 expected)
{
	bool ret = true;
	u64 pkrs;
	u32 *tmp_cache;

	tmp_cache = get_cpu_ptr(&pkrs_cache);
	if (!check_pkey_val(*tmp_cache, pkey, expected))
		ret = false;
	put_cpu_ptr(tmp_cache);

	rdmsrl(MSR_IA32_PKRS, pkrs);
	if (!check_pkey_val(pkrs, pkey, expected))
		ret = false;

	return ret;
}

static void check_exception(u32 thread_pkrs)
{
	/* Check the thread saved state */
	if (!check_pkey_val(thread_pkrs, test_armed_key, PKEY_DISABLE_WRITE)) {
		pr_err("     FAIL: checking ept_regs->thread_pkrs\n");
		test_exception_ctx->pass = false;
	}

	/* Check that the exception state has disabled access */
	if (!check_pkrs(test_armed_key, PKEY_DISABLE_ACCESS)) {
		pr_err("     FAIL: PKRS cache and MSR\n");
		test_exception_ctx->pass = false;
	}

	/*
	 * Ensure an update can occur during exception without affecting the
	 * interrupted thread.  The interrupted thread is checked after
	 * exception...
	 */
	pks_mk_readwrite(test_armed_key);
	if (!check_pkrs(test_armed_key, 0)) {
		pr_err("     FAIL: exception did not change register to 0\n");
		test_exception_ctx->pass = false;
	}
	pks_mk_noaccess(test_armed_key);
	if (!check_pkrs(test_armed_key, PKEY_DISABLE_ACCESS)) {
		pr_err("     FAIL: exception did not change register to 0x%x\n",
			PKEY_DISABLE_ACCESS);
		test_exception_ctx->pass = false;
	}
}

/*
 * pks_test_callback() is called by the fault handler to indicate it saw a PKey
 * fault.
 *
 * NOTE: The callback is responsible for clearing any condition which would
 * cause the fault to re-trigger.
 */
bool pks_test_callback(struct pt_regs *regs)
{
	struct pt_regs_extended *ept_regs = to_extended_pt_regs(regs);
	struct pt_regs_auxiliary *aux_pt_regs = &ept_regs->aux;
	bool armed = (test_armed_key != 0);
	u32 pkrs = aux_pt_regs->pks_thread_pkrs;

	if (test_exception_ctx) {
		check_exception(pkrs);
		/*
		 * Stop this check directly within the exception because the
		 * fault handler clean up code will call again while checking
		 * the PMD entry and there is no need to check this again.
		 */
		test_exception_ctx = NULL;
	}

	if (armed) {
		/* Enable read and write to stop faults */
		aux_pt_regs->pks_thread_pkrs = pkey_update_pkval(pkrs,
								 test_armed_key,
								 0);
		fault_cnt++;
	}

	return armed;
}

static bool fault_caught(void)
{
	bool ret = (fault_cnt != prev_fault_cnt);

	prev_fault_cnt = fault_cnt;
	return ret;
}

enum pks_access_mode {
	PKS_TEST_NO_ACCESS,
	PKS_TEST_RDWR,
};

#define PKS_WRITE true
#define PKS_READ false
#define PKS_FAULT_EXPECTED true
#define PKS_NO_FAULT_EXPECTED false

static char *get_mode_str(enum pks_access_mode mode)
{
	switch (mode) {
	case PKS_TEST_NO_ACCESS:
		return "No Access";
	case PKS_TEST_RDWR:
		return "Read Write";
	default:
		pr_err("BUG in test invalid mode\n");
		break;
	}

	return "";
}

struct pks_access_test {
	enum pks_access_mode mode;
	bool write;
	bool fault;
};

static struct pks_access_test pkey_test_ary[] = {
	{ PKS_TEST_NO_ACCESS,     PKS_WRITE,  PKS_FAULT_EXPECTED },
	{ PKS_TEST_NO_ACCESS,     PKS_READ,   PKS_FAULT_EXPECTED },

	{ PKS_TEST_RDWR,          PKS_WRITE,  PKS_NO_FAULT_EXPECTED },
	{ PKS_TEST_RDWR,          PKS_READ,   PKS_NO_FAULT_EXPECTED },
};

static bool run_access_test(struct pks_test_ctx *ctx,
			   struct pks_access_test *test,
			   void *ptr)
{
	bool fault;

	switch (test->mode) {
	case PKS_TEST_NO_ACCESS:
		pks_mk_noaccess(ctx->pkey);
		break;
	case PKS_TEST_RDWR:
		pks_mk_readwrite(ctx->pkey);
		break;
	default:
		pr_err("BUG in test invalid mode\n");
		return false;
	}

	WRITE_ONCE(test_armed_key, ctx->pkey);

	if (test->write)
		memcpy(ptr, ctx->data, 8);
	else
		memcpy(ctx->data, ptr, 8);

	fault = fault_caught();

	WRITE_ONCE(test_armed_key, 0);

	if (test->fault != fault) {
		pr_err("pkey test FAILED: mode %s; write %s; fault %s != %s\n",
			get_mode_str(test->mode),
			test->write ? "TRUE" : "FALSE",
			test->fault ? "YES" : "NO",
			fault ? "YES" : "NO");
		return false;
	}

	return true;
}

static void *alloc_test_page(int pkey)
{
	return __vmalloc_node_range(PKS_TEST_MEM_SIZE, 1, VMALLOC_START, VMALLOC_END,
				    GFP_KERNEL, PAGE_KERNEL_PKEY(pkey), 0,
				    NUMA_NO_NODE, __builtin_return_address(0));
}

static bool test_ctx(struct pks_test_ctx *ctx)
{
	bool rc = true;
	int i;
	u8 pkey;
	void *ptr = NULL;
	pte_t *ptep = NULL;
	unsigned int level;

	ptr = alloc_test_page(ctx->pkey);
	if (!ptr) {
		pr_err("Failed to vmalloc page???\n");
		return false;
	}

	ptep = lookup_address((unsigned long)ptr, &level);
	if (!ptep) {
		pr_err("Failed to lookup address???\n");
		rc = false;
		goto done;
	}

	pkey = pte_flags_pkey(ptep->pte);
	if (pkey != ctx->pkey) {
		pr_err("invalid pkey found: %u, test_pkey: %u\n",
			pkey, ctx->pkey);
		rc = false;
		goto done;
	}

	for (i = 0; i < ARRAY_SIZE(pkey_test_ary); i++) {
		/* sticky fail */
		if (!run_access_test(ctx, &pkey_test_ary[i], ptr))
			rc = false;
	}

done:
	vfree(ptr);

	return rc;
}

static struct pks_test_ctx *alloc_ctx(u8 pkey)
{
	struct pks_test_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (!ctx) {
		pr_err("Failed to allocate memory for test context\n");
		return ERR_PTR(-ENOMEM);
	}

	ctx->pkey = pkey;
	ctx->pass = true;
	sprintf(ctx->data, "%s", "DEADBEEF");
	return ctx;
}

static void free_ctx(struct pks_test_ctx *ctx)
{
	kfree(ctx);
}

static bool run_single(void)
{
	struct pks_test_ctx *ctx;
	bool rc;

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx))
		return false;

	rc = test_ctx(ctx);
	pks_mk_noaccess(ctx->pkey);
	free_ctx(ctx);

	return rc;
}

static bool run_exception_test(void)
{
	void *ptr = NULL;
	bool pass = true;
	struct pks_test_ctx *ctx;

	pr_info("     ***** BEGIN: exception checking\n");

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx)) {
		pr_err("     FAIL: no context\n");
		pass = false;
		goto result;
	}
	ctx->pass = true;

	ptr = alloc_test_page(ctx->pkey);
	if (!ptr) {
		pr_err("     FAIL: no vmalloc page\n");
		pass = false;
		goto free_context;
	}

	pks_update_protection(ctx->pkey, PKEY_DISABLE_WRITE);

	WRITE_ONCE(test_exception_ctx, ctx);
	WRITE_ONCE(test_armed_key, ctx->pkey);

	memcpy(ptr, ctx->data, 8);

	if (!fault_caught()) {
		pr_err("     FAIL: did not get an exception\n");
		pass = false;
	}

	/*
	 * NOTE The exception code has to enable access (b00) to keep the fault
	 * from looping forever.  Therefore full access is seen here rather
	 * than write disabled.
	 *
	 * Furthermore, check_exception() disabled access during the exception
	 * so this is testing that the thread value was restored back to the
	 * thread value.
	 */
	if (!check_pkrs(test_armed_key, 0)) {
		pr_err("     FAIL: PKRS not restored\n");
		pass = false;
	}

	if (!ctx->pass)
		pass = false;

	WRITE_ONCE(test_armed_key, 0);

	vfree(ptr);
free_context:
	free_ctx(ctx);
result:
	pr_info("     ***** END: exception checking : %s\n",
		 pass ? "PASS" : "FAIL");
	return pass;
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

	pks_mk_noaccess(ctx->pkey);

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

static void arm_ctx_switch(struct file *file)
{
	struct pks_test_ctx *ctx;

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx)) {
		pr_err("Failed to allocate a context\n");
		last_test_pass = false;
		return;
	}

	/* Store context for later checks */
	if (file->private_data) {
		pr_warn("Context already armed\n");
		free_ctx(file->private_data);
	}
	file->private_data = ctx;

	/* Ensure a known state to test context switch */
	pks_mk_readwrite(ctx->pkey);
}

static void check_ctx_switch(struct file *file)
{
	struct pks_test_ctx *ctx;
	unsigned long reg_pkrs;
	int access;

	last_test_pass = true;

	if (!file->private_data) {
		pr_err("No Context switch configured\n");
		last_test_pass = false;
		return;
	}

	ctx = file->private_data;

	rdmsrl(MSR_IA32_PKRS, reg_pkrs);

	access = (reg_pkrs >> PKR_PKEY_SHIFT(ctx->pkey)) &
		  PKEY_ACCESS_MASK;
	if (access != 0) {
		last_test_pass = false;
		pr_err("Context switch check failed: pkey %d: 0x%x reg: 0x%lx\n",
			ctx->pkey, access, reg_pkrs);
	}
}

struct {
	struct pks_test_ctx *ctx;
	void *test_page;
	bool armed;
	bool callback_seen;
} fault_callback_ctx;

bool pks_test_fault_callback(struct pt_regs *regs, unsigned long address,
			     bool write)
{
	if (!fault_callback_ctx.armed)
		return false;

	fault_callback_ctx.armed = false;
	fault_callback_ctx.callback_seen = true;

	pks_update_exception(regs, fault_callback_ctx.ctx->pkey, 0);

	return true;
}

static bool run_fault_clear_test(void)
{
	struct pks_test_ctx *ctx;
	void *test_page;
	bool rc = true;

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx))
		return false;

	test_page = alloc_test_page(ctx->pkey);
	if (!test_page) {
		pr_err("Failed to vmalloc page???\n");
		free_ctx(ctx);
		return false;
	}

	test_armed_key = PKS_KEY_TEST;
	fault_callback_ctx.ctx = ctx;
	fault_callback_ctx.test_page = test_page;
	fault_callback_ctx.armed = true;
	fault_callback_ctx.callback_seen = false;

	pks_mk_noaccess(test_armed_key);

	/* fault */
	memcpy(test_page, ctx->data, 8);

	if (!fault_callback_ctx.callback_seen) {
		pr_err("Failed to see the callback\n");
		rc = false;
		goto done;
	}

	/* no fault */
	fault_callback_ctx.callback_seen = false;
	memcpy(test_page, ctx->data, 8);

	if (fault_caught() || fault_callback_ctx.callback_seen) {
		pr_err("The key failed to be set RD/WR in the callback\n");
		return false;
	}

done:
	free_ctx(ctx);
	return rc;
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
	case RUN_SINGLE:
		last_test_pass = run_single();
		break;
	case ARM_CTX_SWITCH:
		/* start of context switch test */
		arm_ctx_switch(file);
		break;
	case CHECK_CTX_SWITCH:
		/* After context switch MSR should be restored */
		check_ctx_switch(file);
		break;
	case RUN_EXCEPTION:
		last_test_pass = run_exception_test();
		break;
	case RUN_FAULT_ABANDON:
		last_test_pass = run_fault_clear_test();
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
	struct pks_test_ctx *ctx = file->private_data;

	if (ctx)
		free_ctx(ctx);

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
