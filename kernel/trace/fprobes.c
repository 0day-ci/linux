// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "fprobes: " fmt

#include <linux/fprobes.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/sort.h>

static void fprobe_handler(unsigned long ip, unsigned long parent_ip,
			struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct fprobe *fp;
	int bit;

	fp = container_of(ops, struct fprobe, ftrace);
	if (fprobe_disabled(fp))
		return;

	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0) {
		fp->nmissed++;
		return;
	}

	if (fp->entry_handler)
		fp->entry_handler(fp, ip, ftrace_get_regs(fregs));

	ftrace_test_recursion_unlock(bit);
}
NOKPROBE_SYMBOL(fprobe_handler);

static int convert_func_addresses(struct fprobe *fp)
{
	unsigned int i;
	struct fprobe_entry *ent = fp->entries;

	for (i = 0; i < fp->nentry; i++) {
		if ((ent[i].sym && ent[i].addr) ||
		    (!ent[i].sym && !ent[i].addr))
			return -EINVAL;

		if (ent[i].addr)
			continue;

		ent[i].addr = kallsyms_lookup_name(ent[i].sym);
		if (!ent[i].addr)
			return -ENOENT;
	}

	return 0;
}

/* Since the entry list is sorted, we can search it by bisect */
struct fprobe_entry *fprobe_find_entry(struct fprobe *fp, unsigned long addr)
{
	int d, n;

	d = n = fp->nentry / 2;

	while (fp->entries[n].addr != addr) {
		d /= 2;
		if (d == 0)
			return NULL;
		if (fp->entries[n].addr < addr)
			n += d;
		else
			n -= d;
	}

	return fp->entries + n;
}
EXPORT_SYMBOL_GPL(fprobe_find_entry);

static int fprobe_comp_func(const void *a, const void *b)
{
	return ((struct fprobe_entry *)a)->addr - ((struct fprobe_entry *)b)->addr;
}

/**
 * register_fprobe - Register fprobe to ftrace
 * @fp: A fprobe data structure to be registered.
 *
 * This expects the user set @fp::entry_handler, @fp::entries and @fp::nentry.
 * For each entry of @fp::entries[], user must set 'addr' or 'sym'.
 * Note that you do not set both of 'addr' and 'sym' of the entry.
 */
int register_fprobe(struct fprobe *fp)
{
	unsigned int i;
	int ret;

	if (!fp || !fp->nentry || !fp->entries)
		return -EINVAL;

	ret = convert_func_addresses(fp);
	if (ret < 0)
		return ret;
	/*
	 * Sort the addresses so that the handler can find corresponding user data
	 * immediately.
	 */
	sort(fp->entries, fp->nentry, sizeof(*fp->entries),
	     fprobe_comp_func, NULL);

	fp->nmissed = 0;
	fp->ftrace.func = fprobe_handler;
	fp->ftrace.flags = FTRACE_OPS_FL_SAVE_REGS;

	for (i = 0; i < fp->nentry; i++) {
		ret = ftrace_set_filter_ip(&fp->ftrace, fp->entries[i].addr, 0, 0);
		if (ret < 0)
			return ret;
	}

	return register_ftrace_function(&fp->ftrace);
}
EXPORT_SYMBOL_GPL(register_fprobe);

/**
 * unregister_fprobe - Unregister fprobe from ftrace
 * @fp: A fprobe data structure to be unregistered.
 */
int unregister_fprobe(struct fprobe *fp)
{
	if (!fp || !fp->nentry || !fp->entries)
		return -EINVAL;

	return unregister_ftrace_function(&fp->ftrace);
}
EXPORT_SYMBOL_GPL(unregister_fprobe);
