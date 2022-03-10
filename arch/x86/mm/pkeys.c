// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Memory Protection Keys management
 * Copyright (c) 2015, Intel Corporation.
 */
#include <linux/debugfs.h>		/* debugfs_create_u32()		*/
#include <linux/mm_types.h>             /* mm_struct, vma, etc...       */
#include <linux/pkeys.h>                /* PKEY_*                       */
#include <linux/pks.h>
#include <linux/pks-keys.h>
#include <uapi/asm-generic/mman-common.h>

#include <asm/cpufeature.h>             /* boot_cpu_has, ...            */
#include <asm/mmu_context.h>            /* vma_pkey()                   */
#include <asm/trap_pf.h>		/* X86_PF_WRITE */

int __execute_only_pkey(struct mm_struct *mm)
{
	bool need_to_set_mm_pkey = false;
	int execute_only_pkey = mm->context.execute_only_pkey;
	int ret;

	/* Do we need to assign a pkey for mm's execute-only maps? */
	if (execute_only_pkey == -1) {
		/* Go allocate one to use, which might fail */
		execute_only_pkey = mm_pkey_alloc(mm);
		if (execute_only_pkey < 0)
			return -1;
		need_to_set_mm_pkey = true;
	}

	/*
	 * We do not want to go through the relatively costly
	 * dance to set PKRU if we do not need to.  Check it
	 * first and assume that if the execute-only pkey is
	 * write-disabled that we do not have to set it
	 * ourselves.
	 */
	if (!need_to_set_mm_pkey &&
	    !__pkru_allows_read(read_pkru(), execute_only_pkey)) {
		return execute_only_pkey;
	}

	/*
	 * Set up PKRU so that it denies access for everything
	 * other than execution.
	 */
	ret = arch_set_user_pkey_access(current, execute_only_pkey,
			PKEY_DISABLE_ACCESS);
	/*
	 * If the PKRU-set operation failed somehow, just return
	 * 0 and effectively disable execute-only support.
	 */
	if (ret) {
		mm_set_pkey_free(mm, execute_only_pkey);
		return -1;
	}

	/* We got one, store it and use it from here on out */
	if (need_to_set_mm_pkey)
		mm->context.execute_only_pkey = execute_only_pkey;
	return execute_only_pkey;
}

static inline bool vma_is_pkey_exec_only(struct vm_area_struct *vma)
{
	/* Do this check first since the vm_flags should be hot */
	if ((vma->vm_flags & VM_ACCESS_FLAGS) != VM_EXEC)
		return false;
	if (vma_pkey(vma) != vma->vm_mm->context.execute_only_pkey)
		return false;

	return true;
}

/*
 * This is only called for *plain* mprotect calls.
 */
int __arch_override_mprotect_pkey(struct vm_area_struct *vma, int prot, int pkey)
{
	/*
	 * Is this an mprotect_pkey() call?  If so, never
	 * override the value that came from the user.
	 */
	if (pkey != -1)
		return pkey;

	/*
	 * The mapping is execute-only.  Go try to get the
	 * execute-only protection key.  If we fail to do that,
	 * fall through as if we do not have execute-only
	 * support in this mm.
	 */
	if (prot == PROT_EXEC) {
		pkey = execute_only_pkey(vma->vm_mm);
		if (pkey > 0)
			return pkey;
	} else if (vma_is_pkey_exec_only(vma)) {
		/*
		 * Protections are *not* PROT_EXEC, but the mapping
		 * is using the exec-only pkey.  This mapping was
		 * PROT_EXEC and will no longer be.  Move back to
		 * the default pkey.
		 */
		return ARCH_DEFAULT_PKEY;
	}

	/*
	 * This is a vanilla, non-pkey mprotect (or we failed to
	 * setup execute-only), inherit the pkey from the VMA we
	 * are working on.
	 */
	return vma_pkey(vma);
}

/*
 * Make the default PKRU value (at execve() time) as restrictive
 * as possible.  This ensures that any threads clone()'d early
 * in the process's lifetime will not accidentally get access
 * to data which is pkey-protected later on.
 */
u32 init_pkru_value = PKR_AD_MASK( 1) | PKR_AD_MASK( 2) |
		      PKR_AD_MASK( 3) | PKR_AD_MASK( 4) |
		      PKR_AD_MASK( 5) | PKR_AD_MASK( 6) |
		      PKR_AD_MASK( 7) | PKR_AD_MASK( 8) |
		      PKR_AD_MASK( 9) | PKR_AD_MASK(10) |
		      PKR_AD_MASK(11) | PKR_AD_MASK(12) |
		      PKR_AD_MASK(13) | PKR_AD_MASK(14) |
		      PKR_AD_MASK(15);

static ssize_t init_pkru_read_file(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "0x%x\n", init_pkru_value);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t init_pkru_write_file(struct file *file,
		 const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	ssize_t len;
	u32 new_init_pkru;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	/* Make the buffer a valid string that we can not overrun */
	buf[len] = '\0';
	if (kstrtouint(buf, 0, &new_init_pkru))
		return -EINVAL;

	/*
	 * Don't allow insane settings that will blow the system
	 * up immediately if someone attempts to disable access
	 * or writes to pkey 0.
	 */
	if (new_init_pkru & (PKR_AD_BIT|PKR_WD_BIT))
		return -EINVAL;

	WRITE_ONCE(init_pkru_value, new_init_pkru);
	return count;
}

static const struct file_operations fops_init_pkru = {
	.read = init_pkru_read_file,
	.write = init_pkru_write_file,
	.llseek = default_llseek,
};

static int __init create_init_pkru_value(void)
{
	/* Do not expose the file if pkeys are not supported. */
	if (!cpu_feature_enabled(X86_FEATURE_OSPKE))
		return 0;

	debugfs_create_file("init_pkru", S_IRUSR | S_IWUSR,
			arch_debugfs_dir, NULL, &fops_init_pkru);
	return 0;
}
late_initcall(create_init_pkru_value);

static __init int setup_init_pkru(char *opt)
{
	u32 new_init_pkru;

	if (kstrtouint(opt, 0, &new_init_pkru))
		return 1;

	WRITE_ONCE(init_pkru_value, new_init_pkru);

	return 1;
}
__setup("init_pkru=", setup_init_pkru);

/*
 * Kernel users use the same flags as user space:
 *     PKEY_DISABLE_ACCESS
 *     PKEY_DISABLE_WRITE
 */
u32 pkey_update_pkval(u32 pkval, u8 pkey, u32 accessbits)
{
	int shift = pkey * PKR_BITS_PER_PKEY;

	if (WARN_ON_ONCE(accessbits & ~PKEY_ACCESS_MASK))
		accessbits &= PKEY_ACCESS_MASK;

	pkval &= ~(PKEY_ACCESS_MASK << shift);
	return pkval | accessbits << shift;
}

#ifdef CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS

__static_or_pks_test DEFINE_PER_CPU(u32, pkrs_cache);

/**
 * DOC: DEFINE_PKS_FAULT_CALLBACK
 *
 * Users may also provide a fault handler which can handle a fault differently
 * than an oops.  For example if 'MY_FEATURE' wanted to define a handler they
 * can do so by adding the coresponding entry to the pks_key_callbacks array.
 *
 * .. code-block:: c
 *
 *	#ifdef CONFIG_MY_FEATURE
 *	bool my_feature_pks_fault_callback(struct pt_regs *regs,
 *					   unsigned long address, bool write)
 *	{
 *		if (my_feature_fault_is_ok)
 *			return true;
 *		return false;
 *	}
 *	#endif
 *
 *	static const pks_key_callback pks_key_callbacks[PKS_KEY_MAX] = {
 *		[PKS_KEY_DEFAULT]            = NULL,
 *	#ifdef CONFIG_MY_FEATURE
 *		[PKS_KEY_MY_FEATURE]         = my_feature_pks_fault_callback,
 *	#endif
 *	};
 */
#ifndef CONFIG_PKS_TEST_ALL_KEYS

static const pks_key_callback pks_key_callbacks[PKS_KEY_MAX] = {
#ifdef CONFIG_PKS_TEST
	[PKS_KEY_TEST]		= pks_test_fault_callback,
#endif
};

#else /* CONFIG_PKS_TEST_ALL_KEYS */

static const pks_key_callback pks_key_callbacks[PKS_KEY_MAX] = {
	[1 ... (PKS_KEY_MAX-1)]	= pks_test_fault_callback,
};

#endif

static bool pks_call_fault_callback(struct pt_regs *regs, unsigned long address,
				    bool write, u16 key)
{
	if (key >= PKS_KEY_MAX)
		return false;

	if (pks_key_callbacks[key])
		return pks_key_callbacks[key](regs, address, write);

	return false;
}

bool pks_handle_key_fault(struct pt_regs *regs, unsigned long hw_error_code,
			  unsigned long address)
{
	bool write;
	pgd_t pgd;
	p4d_t p4d;
	pud_t pud;
	pmd_t pmd;
	pte_t pte;

	if (!cpu_feature_enabled(X86_FEATURE_PKS))
		return false;

	write = (hw_error_code & X86_PF_WRITE);

	pgd = READ_ONCE(*(init_mm.pgd + pgd_index(address)));
	if (!pgd_present(pgd))
		return false;

	p4d = READ_ONCE(*p4d_offset(&pgd, address));
	if (p4d_large(p4d))
		return pks_call_fault_callback(regs, address, write,
					       pte_flags_pkey(p4d_val(p4d)));
	if (!p4d_present(p4d))
		return false;

	pud = READ_ONCE(*pud_offset(&p4d, address));
	if (pud_large(pud))
		return pks_call_fault_callback(regs, address, write,
					       pte_flags_pkey(pud_val(pud)));
	if (!pud_present(pud))
		return false;

	pmd = READ_ONCE(*pmd_offset(&pud, address));
	if (pmd_large(pmd))
		return pks_call_fault_callback(regs, address, write,
					       pte_flags_pkey(pmd_val(pmd)));
	if (!pmd_present(pmd))
		return false;

	pte = READ_ONCE(*pte_offset_kernel(&pmd, address));
	return pks_call_fault_callback(regs, address, write,
				       pte_flags_pkey(pte_val(pte)));
}

/*
 * pks_write_pkrs() - Write the pkrs of the current CPU
 * @new_pkrs: New value to write to the current CPU register
 *
 * Optimizes the MSR writes by maintaining a per cpu cache.
 *
 * Context: must be called with preemption disabled
 * Context: must only be called if PKS is enabled
 *
 * It should also be noted that the underlying WRMSR(MSR_IA32_PKRS) is not
 * serializing but still maintains ordering properties similar to WRPKRU.
 * The current SDM section on PKRS needs updating but should be the same as
 * that of WRPKRU.  Quote from the WRPKRU text:
 *
 *     WRPKRU will never execute transiently. Memory accesses
 *     affected by PKRU register will not execute (even transiently)
 *     until all prior executions of WRPKRU have completed execution
 *     and updated the PKRU register.
 */
static inline void pks_write_pkrs(u32 new_pkrs)
{
	u32 pkrs = __this_cpu_read(pkrs_cache);

	lockdep_assert_preemption_disabled();

	if (pkrs != new_pkrs) {
		__this_cpu_write(pkrs_cache, new_pkrs);
		wrmsrl(MSR_IA32_PKRS, new_pkrs);
	}
}

/* x86_pkrs_load() - Update CPU with the incoming thread pkrs value */
void x86_pkrs_load(struct thread_struct *thread)
{
	if (!cpu_feature_enabled(X86_FEATURE_PKS))
		return;

	pks_write_pkrs(thread->pkrs);
}

/*
 * PKRS is a per-logical-processor MSR which overlays additional protection for
 * pages which have been mapped with a protection key.
 *
 * To protect against exceptions having potentially privileged access to memory
 * of an interrupted thread, save the current thread value and set the PKRS
 * value to be used during the exception.
 */
void pks_save_pt_regs(struct pt_regs *regs)
{
	struct pt_regs_auxiliary *aux_pt_regs;

	if (!cpu_feature_enabled(X86_FEATURE_PKS))
		return;

	aux_pt_regs = &to_extended_pt_regs(regs)->aux;
	aux_pt_regs->pkrs = current->thread.pkrs;
	pks_write_pkrs(PKS_INIT_VALUE);
}

void pks_restore_pt_regs(struct pt_regs *regs)
{
	struct pt_regs_auxiliary *aux_pt_regs;

	if (!cpu_feature_enabled(X86_FEATURE_PKS))
		return;

	aux_pt_regs = &to_extended_pt_regs(regs)->aux;
	current->thread.pkrs = aux_pt_regs->pkrs;
	pks_write_pkrs(current->thread.pkrs);
}

void pks_show_regs(struct pt_regs *regs, const char *log_lvl)
{
	struct pt_regs_auxiliary *aux_pt_regs;

	if (!cpu_feature_enabled(X86_FEATURE_PKS))
		return;

	aux_pt_regs = &to_extended_pt_regs(regs)->aux;
	printk("%sPKRS: 0x%x\n", log_lvl, aux_pt_regs->pkrs);
}

/*
 * PKS is independent of PKU and either or both may be supported on a CPU.
 *
 * Context: must be called with preemption disabled
 */
void pks_setup(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_PKS))
		return;

	/*
	 * If the PKS_INIT_VALUE is 0 then pks_write_pkrs() will fail to
	 * initialize the MSR.  Do a single write here to ensure the MSR is
	 * written at least one time.
	 */
	wrmsrl(MSR_IA32_PKRS, PKS_INIT_VALUE);
	pks_write_pkrs(PKS_INIT_VALUE);
	cr4_set_bits(X86_CR4_PKS);
}

static void __pks_update_protection(u8 pkey, u8 protection)
{
	u32 pkrs;

	pkrs = current->thread.pkrs;
	current->thread.pkrs = pkey_update_pkval(pkrs, pkey, protection);

	preempt_disable();
	pks_write_pkrs(current->thread.pkrs);
	preempt_enable();
}

/*
 * Do not call this directly, see pks_set*().
 *
 * @pkey: Key for the domain to change
 * @protection: protection bits to be used
 *
 * Protection utilizes the same protection bits specified for User pkeys
 *     PKEY_DISABLE_ACCESS
 *     PKEY_DISABLE_WRITE
 *
 */
void pks_update_protection(u8 pkey, u8 protection)
{
	if (!cpu_feature_enabled(X86_FEATURE_PKS))
		return;

	if (WARN_ON_ONCE(pkey >= PKS_KEY_MAX))
		return;

	__pks_update_protection(pkey, protection);
}
EXPORT_SYMBOL_GPL(pks_update_protection);

/**
 * pks_update_exception() - Update the protections of a faulted thread
 *
 * @regs: Faulting thread registers
 * @pkey: pkey to update
 * @protection: protection bits to use.
 *
 * CONTEXT: Exception
 *
 * pks_update_exception() updates the faulted threads protections in addition
 * to the protections within the exception.
 *
 * This is useful because the pks_set_*() functions will not work to change the
 * protections of a thread which has been interrupted.  Only the current
 * context is updated by those functions.  Therefore, if a PKS fault callback
 * wants to update the faulted threads protections it must call
 * pks_update_exception().
 */
void pks_update_exception(struct pt_regs *regs, u8 pkey, u8 protection)
{
	struct pt_regs_extended *ept_regs;
	u32 old;

	if (!cpu_feature_enabled(X86_FEATURE_PKS))
		return;

	if (WARN_ON_ONCE(pkey >= PKS_KEY_MAX))
		return;

	__pks_update_protection(pkey, protection);

	ept_regs = to_extended_pt_regs(regs);
	old = ept_regs->aux.pkrs;
	ept_regs->aux.pkrs = pkey_update_pkval(old, pkey, protection);
}
EXPORT_SYMBOL_GPL(pks_update_exception);

#endif /* CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */
