/* SPDX-License-Identifier: GPL-2.0 */
/* Simple ftrace probe wrapper */
#ifndef _LINUX_FPROBES_H
#define _LINUX_FPROBES_H

#include <linux/compiler.h>
#include <linux/ftrace.h>

/*
 * fprobe_entry - function entry for fprobe
 * @sym: The symbol name of the function.
 * @addr: The address of @sym.
 * @data: per-entry data
 *
 * User must specify either @sym or @addr (not both). @data is optional.
 */
struct fprobe_entry {
	const char	*sym;
	unsigned long	addr;
	void		*data;
};

struct fprobe {
	struct fprobe_entry	*entries;
	unsigned int		nentry;

	struct ftrace_ops	ftrace;
	unsigned long		nmissed;
	unsigned int		flags;
	void (*entry_handler) (struct fprobe *, unsigned long, struct pt_regs *);
};

#define FPROBE_FL_DISABLED	1

static inline bool fprobe_disabled(struct fprobe *fp)
{
	return (fp) ? fp->flags & FPROBE_FL_DISABLED : false;
}

#ifdef CONFIG_FPROBES
int register_fprobe(struct fprobe *fp);
int unregister_fprobe(struct fprobe *fp);
struct fprobe_entry *fprobe_find_entry(struct fprobe *fp, unsigned long addr);
#else
static inline int register_fprobe(struct fprobe *fp)
{
	return -ENOTSUPP;
}
static inline int unregister_fprobe(struct fprobe *fp)
{
	return -ENOTSUPP;
}
struct fprobe_entry *fprobe_find_entry(struct fprobe *fp, unsigned long addr)
{
	return NULL;
}
#endif

static inline void disable_fprobe(struct fprobe *fp)
{
	if (fp)
		fp->flags |= FPROBE_FL_DISABLED;
}

static inline void enable_fprobe(struct fprobe *fp)
{
	if (fp)
		fp->flags &= ~FPROBE_FL_DISABLED;
}

#endif
