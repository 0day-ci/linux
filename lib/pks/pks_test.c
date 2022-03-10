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
#include <linux/pgtable.h>
#include <linux/pkeys.h>
#include <linux/pks.h>
#include <linux/pks-keys.h>

#include <uapi/asm-generic/mman-common.h>

#include <asm/ptrace.h>

#define PKS_TEST_MEM_SIZE (PAGE_SIZE)

#define CHECK_DEFAULTS		0
#define RUN_SINGLE		1
#define ARM_CTX_SWITCH		2
#define CHECK_CTX_SWITCH	3
#define RUN_EXCEPTION		4
#define RUN_CRASH_TEST		9

DECLARE_PER_CPU(u32, pkrs_cache);

static struct dentry *pks_test_dentry;

DEFINE_MUTEX(test_run_lock);

struct pks_test_ctx {
	u8 pkey;
	bool pass;
	char data[64];
	void *test_page;
	bool fault_seen;
	bool validate_exp_handling;
};

static bool check_pkey_val(u32 pk_reg, u8 pkey, u32 expected)
{
	pk_reg = (pk_reg >> PKR_PKEY_SHIFT(pkey)) & PKEY_ACCESS_MASK;
	return (pk_reg == expected);
}

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

/*
 * Check if the register @pkey value matches @expected value
 *
 * Both the cached and actual MSR must match.
 */
static bool check_pkrs(u8 pkey, u8 expected)
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

static void validate_exception(struct pks_test_ctx *ctx, u32 thread_pkrs)
{
	u8 pkey = ctx->pkey;

	/* Check that the thread state was saved */
	if (!check_pkey_val(thread_pkrs, pkey, PKEY_DISABLE_WRITE)) {
		pr_err("     FAIL: checking aux_pt_regs->thread_pkrs\n");
		ctx->pass = false;
	}

	/* Check that the exception received the default of disabled access */
	if (!check_pkrs(pkey, PKEY_DISABLE_ACCESS)) {
		pr_err("     FAIL: PKRS cache and MSR\n");
		ctx->pass = false;
	}

	/*
	 * Ensure an update can occur during exception without affecting the
	 * interrupted thread.  The interrupted thread is verified after the
	 * exception returns.
	 */
	pks_set_readwrite(pkey);
	if (!check_pkrs(pkey, 0)) {
		pr_err("     FAIL: exception did not change register to 0\n");
		ctx->pass = false;
	}
	pks_set_noaccess(pkey);
	if (!check_pkrs(pkey, PKEY_DISABLE_ACCESS)) {
		pr_err("     FAIL: exception did not change register to 0x%x\n",
			PKEY_DISABLE_ACCESS);
		ctx->pass = false;
	}
}

/* Global data protected by test_run_lock */
struct pks_test_ctx *g_ctx_under_test;

/*
 * Call set_context_for_fault() after the context has been set up and prior to
 * the expected fault.
 */
static void set_context_for_fault(struct pks_test_ctx *ctx)
{
	g_ctx_under_test = ctx;
	/* Ensure the state of the global context is correct prior to a fault */
	barrier();
}

bool pks_test_fault_callback(struct pt_regs *regs, unsigned long address,
			     bool write)
{
	struct pt_regs_extended *ept_regs = to_extended_pt_regs(regs);
	struct pt_regs_auxiliary *aux_pt_regs = &ept_regs->aux;
	u32 pkrs = aux_pt_regs->pkrs;

	pr_debug("PKS Fault callback: ctx %p\n", g_ctx_under_test);

	if (!g_ctx_under_test)
		return false;

	if (g_ctx_under_test->validate_exp_handling) {
		validate_exception(g_ctx_under_test, pkrs);
		/*
		 * Stop this check directly within the exception because the
		 * fault handler clean up code will call again while checking
		 * the PMD entry and there is no need to check this again.
		 */
		g_ctx_under_test->validate_exp_handling = false;
	}

	aux_pt_regs->pkrs = pkey_update_pkval(pkrs, g_ctx_under_test->pkey, 0);
	g_ctx_under_test->fault_seen = true;
	return true;
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
	switch (test->mode) {
	case PKS_TEST_NO_ACCESS:
		pks_set_noaccess(ctx->pkey);
		break;
	case PKS_TEST_RDWR:
		pks_set_readwrite(ctx->pkey);
		break;
	default:
		pr_debug("BUG in test, invalid mode\n");
		return false;
	}

	ctx->fault_seen = false;
	set_context_for_fault(ctx);

	if (test->write)
		memcpy(ptr, ctx->data, 8);
	else
		memcpy(ctx->data, ptr, 8);

	if (test->fault != ctx->fault_seen) {
		pr_err("pkey test FAILED: mode %s; write %s; fault %s != %s\n",
			get_mode_str(test->mode),
			test->write ? "TRUE" : "FALSE",
			test->fault ? "YES" : "NO",
			ctx->fault_seen ? "YES" : "NO");
		return false;
	}

	return true;
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

static bool test_ctx(struct pks_test_ctx *ctx)
{
	bool rc = true;
	int i;
	u8 pkey;
	void *ptr = ctx->test_page;
	pte_t *ptep = NULL;
	unsigned int level;

	ptep = lookup_address((unsigned long)ptr, &level);
	if (!ptep) {
		pr_err("Failed to lookup address???\n");
		return false;
	}

	pkey = pte_flags_pkey(ptep->pte);
	if (pkey != ctx->pkey) {
		pr_err("invalid pkey found: %u, test_pkey: %u\n",
			pkey, ctx->pkey);
		return false;
	}

	for (i = 0; i < ARRAY_SIZE(pkey_test_ary); i++) {
		/* sticky fail */
		if (!run_access_test(ctx, &pkey_test_ary[i], ptr))
			rc = false;
	}

	return rc;
}

static struct pks_test_ctx *alloc_ctx(u8 pkey)
{
	struct pks_test_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->pkey = pkey;
	ctx->pass = true;
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

static bool run_single(struct pks_session_data *sd)
{
	struct pks_test_ctx *ctx;
	bool rc;

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx))
		return false;

	set_ctx_data(sd, ctx);

	rc = test_ctx(ctx);
	pks_set_noaccess(ctx->pkey);

	return rc;
}

static bool run_exception_test(struct pks_session_data *sd)
{
	bool pass = true;
	struct pks_test_ctx *ctx;

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx)) {
		pr_debug("     FAIL: no context\n");
		return false;
	}

	set_ctx_data(sd, ctx);

	/*
	 * Set the thread pkey value to something other than the default of
	 * access disable but something which still causes a fault, disable
	 * writes.
	 */
	pks_update_protection(ctx->pkey, PKEY_DISABLE_WRITE);

	ctx->validate_exp_handling = true;
	set_context_for_fault(ctx);

	memcpy(ctx->test_page, ctx->data, 8);

	if (!ctx->fault_seen) {
		pr_err("     FAIL: did not get an exception\n");
		pass = false;
	}

	/*
	 * The exception code has to enable access to keep the fault from
	 * looping forever.  Therefore full access is seen here rather than
	 * write disabled.
	 *
	 * However, this does verify that the exception state was independent
	 * of the interrupted threads state because validate_exception()
	 * disabled access during the exception.
	 */
	if (!check_pkrs(ctx->pkey, 0)) {
		pr_err("     FAIL: PKRS not restored\n");
		pass = false;
	}

	if (!ctx->pass)
		pass = false;

	return pass;
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

static void arm_ctx_switch(struct pks_session_data *sd)
{
	struct pks_test_ctx *ctx;

	ctx = alloc_ctx(PKS_KEY_TEST);
	if (IS_ERR(ctx)) {
		pr_err("Failed to allocate a context\n");
		sd->last_test_pass = false;
		return;
	}

	set_ctx_data(sd, ctx);

	/* Ensure a known state to test context switch */
	pks_set_readwrite(ctx->pkey);
}

static void check_ctx_switch(struct pks_session_data *sd)
{
	struct pks_test_ctx *ctx = sd->ctx;
	unsigned long reg_pkrs;
	int access;

	sd->last_test_pass = true;

	if (!ctx) {
		pr_err("No Context switch configured\n");
		sd->last_test_pass = false;
		return;
	}

	rdmsrl(MSR_IA32_PKRS, reg_pkrs);

	access = (reg_pkrs >> PKR_PKEY_SHIFT(ctx->pkey)) &
		  PKEY_ACCESS_MASK;
	if (access != 0) {
		pr_err("Context switch check failed: pkey %u: 0x%x reg: 0x%lx\n",
			ctx->pkey, access, reg_pkrs);
		sd->last_test_pass = false;
	}
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

static void cleanup_test(void)
{
	g_ctx_under_test = NULL;
	mutex_unlock(&test_run_lock);
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
	case RUN_SINGLE:
		pr_debug("Single key\n");
		sd->last_test_pass = run_single(file->private_data);
		break;
	case ARM_CTX_SWITCH:
		pr_debug("Arming Context switch test\n");
		arm_ctx_switch(file->private_data);
		break;
	case CHECK_CTX_SWITCH:
		pr_debug("Checking Context switch test\n");
		check_ctx_switch(file->private_data);
		break;
	case RUN_EXCEPTION:
		pr_debug("Exception checking\n");
		sd->last_test_pass = run_exception_test(file->private_data);
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
	cleanup_test();
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
		cleanup_test();
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
