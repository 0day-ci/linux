/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_PIDTYPE_H
#define _UAPI_LINUX_PIDTYPE_H

/*
 * Type of a process-related ID.  So far, it is used only
 * for prctl(PR_SCHED_CORE);  not to be confused with type field
 * of f_owner_ex structure argument of fcntl(F_SETOWN_EX).
 */
enum __kernel_pidtype
{
	__PIDTYPE_PID,
	__PIDTYPE_TGID,
	__PIDTYPE_PGID,
	__PIDTYPE_SID,

	__PIDTYPE_MAX /* Non-UAPI */
};

#endif /* _UAPI_LINUX_PIDTYPE_H */
