/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PANIC_NOTIFIERS_H
#define _LINUX_PANIC_NOTIFIERS_H

#include <linux/notifier.h>
#include <linux/types.h>

/*
 * The panic notifier filter infrastructure - each array element holds a
 * function address, to be checked against panic_notifier register/unregister
 * operations; these functions are allowed to be registered in the panic
 * notifier list. This setting is useful for kdump, since users may want
 * some panic notifiers to execute, but not all of them.
 */
extern unsigned long panic_nf_functions[];
extern int panic_nf_count;

extern struct atomic_notifier_head panic_notifier_list;

extern bool crash_kexec_post_notifiers;

#endif	/* _LINUX_PANIC_NOTIFIERS_H */
