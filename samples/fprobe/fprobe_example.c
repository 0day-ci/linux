// SPDX-License-Identifier: GPL-2.0-only
/*
 * Here's a sample kernel module showing the use of fprobe to dump a
 * stack trace and selected registers when kernel_clone() is called.
 *
 * For more information on theory of operation of kprobes, see
 * Documentation/trace/kprobes.rst
 *
 * You will see the trace data in /var/log/messages and on the console
 * whenever kernel_clone() is invoked to create a new process.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fprobes.h>
#include <linux/slab.h>

#define MAX_SYMBOL_LEN 4096
struct fprobe sample_probe;
static char symbol[MAX_SYMBOL_LEN] = "kernel_clone";
module_param_string(symbol, symbol, sizeof(symbol), 0644);

static void sample_entry_handler(struct fprobe *fp, unsigned long ip, struct pt_regs *regs)
{
	const char *sym = "<no-sym>";
	struct fprobe_entry *ent;

	ent = fprobe_find_entry(fp, ip);
	if (ent)
		sym = ent->sym;

	pr_info("Enter <%s> ip = 0x%p (%pS)\n", sym, (void *)ip, (void *)ip);
}

static void sample_exit_handler(struct fprobe *fp, unsigned long ip, struct pt_regs *regs)
{
	unsigned long rip = instruction_pointer(regs);
	const char *sym = "<no-sym>";
	struct fprobe_entry *ent;

	ent = fprobe_find_entry(fp, ip);
	if (ent)
		sym = ent->sym;

	pr_info("Return from <%s> ip = 0x%p to rip = 0x%p (%pS)\n", sym, (void *)ip,
		(void *)rip, (void *)rip);
}

static int __init fprobe_init(void)
{
	struct fprobe_entry *ents;
	char *tmp, *p;
	int ret, count, i;

	sample_probe.entry_handler = sample_entry_handler;
	sample_probe.exit_handler = sample_exit_handler;

	if (strchr(symbol, ',')) {
		tmp = kstrdup(symbol, GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;
		p = tmp;
		count = 1;
		while ((p = strchr(p, ',')) != NULL)
			count++;
	} else {
		count = 1;
		tmp = symbol;
	}

	ents = kzalloc(count * sizeof(*ents), GFP_KERNEL);
	if (!ents) {
		if (tmp != symbol)
			kfree(tmp);
		return -ENOMEM;
	}

	for (i = 0; i < count; i++)
		ents[i].sym = strsep(&tmp, ",");

	sample_probe.entries = ents;
	sample_probe.nentry = count;

	ret = register_fprobe(&sample_probe);
	if (ret < 0) {
		pr_err("register_fprobe failed, returned %d\n", ret);
		return ret;
	}
	pr_info("Planted fprobe at %s\n", symbol);
	return 0;
}

static void __exit fprobe_exit(void)
{
	unregister_fprobe(&sample_probe);
	pr_info("fprobe at %s unregistered\n", symbol);
}

module_init(fprobe_init)
module_exit(fprobe_exit)
MODULE_LICENSE("GPL");
