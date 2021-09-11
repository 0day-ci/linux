// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2021 Open Mobile Platform LLC.
 *
 * Written by: Igor Zhbanov <i.zhbanov@omp.ru, izh1979@gmail.com>
 *
 * NAX (No Anonymous Execution) Linux Security Module
 * This module prevents execution of the code in anonymous or modified pages.
 * For more details, see Documentation/admin-guide/LSM/NAX.rst and
 * Documentation/admin-guide/kernel-parameters.rst
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "NAX: " fmt

#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/ctype.h>
#include <linux/lsm_hooks.h>
#include <linux/mman.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/securebits.h>
#include <linux/security.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/uidgid.h>

#define NAX_MODE_PERMISSIVE 0 /* Log only             */
#define NAX_MODE_ENFORCING  1 /* Enforce and log      */
#define NAX_MODE_KILL       2 /* Kill process and log */

static int max_mode = NAX_MODE_KILL;

static int mode      = CONFIG_SECURITY_NAX_MODE,
	   quiet     = IS_ENABLED(CONFIG_SECURITY_NAX_QUIET),
	   locked    = IS_ENABLED(CONFIG_SECURITY_NAX_LOCKED),
	   check_all = IS_ENABLED(CONFIG_SECURITY_NAX_CHECK_ALL);

#define ALLOWED_CAPS_HEX_LEN (_KERNEL_CAPABILITY_U32S * 8)

static char allowed_caps_hex[ALLOWED_CAPS_HEX_LEN + 1];
static kernel_cap_t __rcu *allowed_caps;
DEFINE_SPINLOCK(allowed_caps_mutex);

static bool
is_interesting_process(void)
{
	bool ret = false;
	const struct cred *cred;
	kuid_t root_uid;
	kernel_cap_t *caps;

	if (check_all)
		return true;

	cred = current_cred();
	root_uid = make_kuid(cred->user_ns, 0);

	rcu_read_lock();
	caps = rcu_dereference(allowed_caps);
	/*
	 * We count a process as interesting if it any of its uid/euid/suid
	 * is zero (because it may call seteuid(0) to gain privileges) or
	 * it has any not allowed capability (even in a user namespace)
	 */
	if ((!issecure(SECURE_NO_SETUID_FIXUP) &&
	     (uid_eq(cred->uid,  root_uid) ||
	      uid_eq(cred->euid, root_uid) ||
	      uid_eq(cred->suid, root_uid))) ||
	    (!cap_issubset(cred->cap_effective, *caps)) ||
	    (!cap_issubset(cred->cap_permitted, *caps)))
		ret = true;

	rcu_read_unlock();
	return ret;
}

static void
log_warn(const char *reason)
{
	if (quiet)
		return;

	pr_warn_ratelimited("%s: pid=%d, uid=%u, comm=\"%s\"\n",
			    reason, current->pid,
			    from_kuid(&init_user_ns, current_cred()->uid),
				      current->comm);
}

static void
kill_current_task(void)
{
	pr_warn("Killing pid=%d, uid=%u, comm=\"%s\"\n",
		current->pid, from_kuid(&init_user_ns, current_cred()->uid),
		current->comm);
	force_sig(SIGKILL);
}

static int
nax_mmap_file(struct file *file, unsigned long reqprot,
	      unsigned long prot, unsigned long flags)
{
	int ret = 0;

	if (mode == NAX_MODE_PERMISSIVE && quiet)
		return 0; /* Skip further checks in this case */

	if (!(prot & PROT_EXEC)) /* Not executable memory */
		return 0;

	if (!is_interesting_process())
		return 0; /* Not interesting processes can do anything */

	if (!file) { /* Anonymous executable memory */
		log_warn("MMAP_ANON_EXEC");
		ret = -EACCES;
	} else if (prot & PROT_WRITE) { /* Mapping file RWX */
		log_warn("MMAP_FILE_WRITE_EXEC");
		ret = -EACCES;
	}

	if (ret && mode == NAX_MODE_KILL)
		kill_current_task();

	return (mode != NAX_MODE_PERMISSIVE) ? ret : 0;
}

static int
nax_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
		  unsigned long prot)
{
	int ret = 0;

	if (mode == NAX_MODE_PERMISSIVE && quiet)
		return 0; /* Skip further checks in this case */

	if (!(prot & PROT_EXEC)) /* Not executable memory */
		return 0;

	if (!is_interesting_process())
		return 0; /* Not interesting processes can do anything */

	if (!(vma->vm_flags & VM_EXEC)) {
		if (vma->vm_start >= vma->vm_mm->start_brk &&
		    vma->vm_end   <= vma->vm_mm->brk) {
			log_warn("MPROTECT_EXEC_HEAP");
			ret = -EACCES;
		} else if (!vma->vm_file &&
			   ((vma->vm_start <= vma->vm_mm->start_stack &&
			     vma->vm_end   >= vma->vm_mm->start_stack) ||
			    vma_is_stack_for_current(vma))) {
			log_warn("MPROTECT_EXEC_STACK");
			ret = -EACCES;
		} else if (vma->vm_file && vma->anon_vma) {
			/*
			 * We are making executable a file mapping that has
			 * had some COW done. Since pages might have been
			 * written, check ability to execute the possibly
			 * modified content. This typically should only
			 * occur for text relocations.
			 */
			log_warn("MPROTECT_EXEC_MODIFIED");
			ret = -EACCES;
		}
	}

	if (!ret) {
		if (!vma->vm_file) { /* Anonymous executable memory */
			log_warn("MPROTECT_ANON_EXEC");
			ret = -EACCES;
		} else if (prot & PROT_WRITE) { /* Remapping file as RWX */
			log_warn("MPROTECT_FILE_WRITE_EXEC");
			ret = -EACCES;
		}
	}

	if (ret && mode == NAX_MODE_KILL)
		kill_current_task();

	return (mode != NAX_MODE_PERMISSIVE) ? ret : 0;
}

static struct security_hook_list nax_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(mmap_file, nax_mmap_file),
	LSM_HOOK_INIT(file_mprotect, nax_file_mprotect),
};

static void
update_allowed_caps(kernel_cap_t *caps)
{
	kernel_cap_t *old_caps;

	*caps = cap_intersect(*caps, CAP_FULL_SET); /* Drop unsupported */
	spin_lock(&allowed_caps_mutex);
	old_caps = rcu_dereference_protected(allowed_caps,
					     lockdep_is_held(&allowed_caps_mutex));
	rcu_assign_pointer(allowed_caps, caps);
	spin_unlock(&allowed_caps_mutex);
	synchronize_rcu();
	kfree(old_caps);
}

static int
set_default_allowed_caps(void)
{
	size_t i;
	kernel_cap_t *caps;

	caps = kmalloc(sizeof(*caps), GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	CAP_FOR_EACH_U32(i)
		caps->cap[i] = (CONFIG_SECURITY_NAX_ALLOWED_CAPS >> (i * 8)) &
			       0xff;

	update_allowed_caps(caps);
	return 0;
}

static int
parse_and_set_caps(char *str)
{
	size_t len, i;
	kernel_cap_t *caps;

	/* len is guaranteed not to exceed ALLOWED_CAPS_HEX_LEN */
	len = strlen(str);
	for (i = 0; i < len; i++)
		if (!isxdigit(str[i]))
			return -EINVAL;

	caps = kmalloc(sizeof(*caps), GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	CAP_FOR_EACH_U32(i) {
		unsigned long l;

		if (kstrtoul(str + (len >= 8 ? len - 8 : 0), 16, &l))
			return -EINVAL;

		caps->cap[i] = l;
		if (len < 8)
			break;

		len -= 8;
		str[len] = '\0';
	}

	update_allowed_caps(caps);
	return 0;
}

#ifdef CONFIG_SYSCTL

static int
nax_dointvec_minmax(struct ctl_table *table, int write,
		    void *buffer, size_t *lenp, loff_t *ppos)
{
	if (write && (!capable(CAP_SYS_ADMIN) || locked))
		return -EPERM;

	return proc_dointvec_minmax(table, write, buffer, lenp, ppos);
}

static int
nax_dostring(struct ctl_table *table, int write, void *buffer,
	     size_t *lenp, loff_t *ppos)
{
	int ret;

	if (write) { /* A user is setting the allowed capabilities */
		int error;
		char *buf = (char *)buffer;
		size_t len = *lenp;

		if (!capable(CAP_SYS_ADMIN) || locked)
			return -EPERM;

		/* Do not allow trailing garbage or excessive length */
		if (len == ALLOWED_CAPS_HEX_LEN + 1) {
			if (buf[--len] != '\n')
				return -EINVAL;
		} else if (len > ALLOWED_CAPS_HEX_LEN || len <= 0) {
			return -EINVAL;
		}

		error = proc_dostring(table, write, buffer, lenp, ppos);
		if (error)
			return error;

		ret = parse_and_set_caps(allowed_caps_hex);
	} else { /* A user is getting the allowed capabilities */
		unsigned int i;
		kernel_cap_t *caps;

		rcu_read_lock();
		caps = rcu_dereference(allowed_caps);
		CAP_FOR_EACH_U32(i)
			snprintf(allowed_caps_hex + i * 8, 9, "%08x",
				 caps->cap[CAP_LAST_U32 - i]);

		rcu_read_unlock();
		ret = proc_dostring(table, write, buffer, lenp, ppos);
	}

	return ret;
}

struct ctl_path nax_sysctl_path[] = {
	{ .procname = "kernel" },
	{ .procname = "nax"    },
	{ }
};

static struct ctl_table nax_sysctl_table[] = {
	{
		.procname     = "allowed_caps",
		.data         = allowed_caps_hex,
		.maxlen       = ALLOWED_CAPS_HEX_LEN + 1,
		.mode         = 0644,
		.proc_handler = nax_dostring,
	}, {
		.procname     = "check_all",
		.data         = &check_all,
		.maxlen       = sizeof(int),
		.mode         = 0644,
		.proc_handler = nax_dointvec_minmax,
		.extra1       = SYSCTL_ZERO,
		.extra2       = SYSCTL_ONE,
	}, {
		.procname     = "locked",
		.data         = &locked,
		.maxlen       = sizeof(int),
		.mode         = 0644,
		.proc_handler = nax_dointvec_minmax,
		.extra1       = SYSCTL_ZERO,
		.extra2       = SYSCTL_ONE,
	}, {
		.procname     = "mode",
		.data         = &mode,
		.maxlen       = sizeof(int),
		.mode         = 0644,
		.proc_handler = nax_dointvec_minmax,
		.extra1       = SYSCTL_ZERO,
		.extra2       = &max_mode,
	}, {
		.procname     = "quiet",
		.data         = &quiet,
		.maxlen       = sizeof(int),
		.mode         = 0644,
		.proc_handler = nax_dointvec_minmax,
		.extra1       = SYSCTL_ZERO,
		.extra2       = SYSCTL_ONE,
	},
	{ }
};

static void __init
nax_init_sysctl(void)
{
	if (!register_sysctl_paths(nax_sysctl_path, nax_sysctl_table))
		panic("NAX: sysctl registration failed.\n");
}

#else /* !CONFIG_SYSCTL */

static inline void
nax_init_sysctl(void)
{

}

#endif /* !CONFIG_SYSCTL */

static int __init setup_allowed_caps(char *str)
{
	/* Do not allow trailing garbage or excessive length */
	if (strnlen(str, ALLOWED_CAPS_HEX_LEN + 1) > ALLOWED_CAPS_HEX_LEN) {
		pr_err("Invalid 'nax_allowed_caps' parameter value (%s)\n",
		       str);
		return 1;
	}

	strscpy(allowed_caps_hex, str, sizeof(allowed_caps_hex));
	if (parse_and_set_caps(allowed_caps_hex))
		pr_err("Invalid 'nax_allowed_caps' parameter value (%s)\n",
		       str);

	return 1;
}
__setup("nax_allowed_caps=", setup_allowed_caps);

static int __init setup_check_all(char *str)
{
	unsigned long val;

	if (!kstrtoul(str, 0, &val))
		check_all = val ? 1 : 0;

	return 1;
}
__setup("nax_quiet=", setup_check_all);

static int __init setup_locked(char *str)
{
	unsigned long val;

	if (!kstrtoul(str, 0, &val))
		locked = val ? 1 : 0;

	return 1;
}
__setup("nax_locked=", setup_locked);

static int __init setup_mode(char *str)
{
	unsigned long val;

	if (!kstrtoul(str, 0, &val)) {
		if (val > max_mode) {
			pr_err("Invalid 'nax_mode' parameter value (%s)\n",
			       str);
			val = max_mode;
		}

		mode = val;
	}

	return 1;
}
__setup("nax_mode=", setup_mode);

static int __init setup_quiet(char *str)
{
	unsigned long val;

	if (!kstrtoul(str, 0, &val))
		quiet = val ? 1 : 0;

	return 1;
}
__setup("nax_quiet=", setup_quiet);

static __init int
nax_init(void)
{
	int rc;

	pr_info("Starting.\n");
	rc = set_default_allowed_caps();
	if (rc < 0)
		return rc;

	security_add_hooks(nax_hooks, ARRAY_SIZE(nax_hooks), "nax");
	nax_init_sysctl();

	return 0;
}

DEFINE_LSM(nax) = {
	.name = "nax",
	.init = nax_init,
};
