/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DYNAMIC_DEBUG_H
#define _DYNAMIC_DEBUG_H

#if defined(CONFIG_JUMP_LABEL)
#include <linux/jump_label.h>
#endif

#include <linux/build_bug.h>

/*
 * An instance of this structure is created in a special
 * ELF section at every dynamic debug callsite.  At runtime,
 * the special section is treated as an array of these.
 */
struct _ddebug {
	/*
	 * These fields are used to drive the user interface
	 * for selecting and displaying debug callsites.
	 */
	const char *modname;
	const char *function;
	const char *filename;
	const char *format;
	unsigned int lineno:18;
#define CLS_BITS 4
	unsigned int class_id:CLS_BITS;
#define _DPRINTK_SITE_UNCLASSED		((1 << CLS_BITS) - 1)
	/*
	 * The flags field controls the behaviour at the callsite.
	 * The bits here are changed dynamically when the user
	 * writes commands to <debugfs>/dynamic_debug/control
	 */
#define _DPRINTK_FLAGS_NONE	0
#define _DPRINTK_FLAGS_PRINT	(1<<0) /* printk() a message using the format */
#define _DPRINTK_FLAGS_INCL_MODNAME	(1<<1)
#define _DPRINTK_FLAGS_INCL_FUNCNAME	(1<<2)
#define _DPRINTK_FLAGS_INCL_LINENO	(1<<3)
#define _DPRINTK_FLAGS_INCL_TID		(1<<4)

#define _DPRINTK_FLAGS_INCL_ANY		\
	(_DPRINTK_FLAGS_INCL_MODNAME | _DPRINTK_FLAGS_INCL_FUNCNAME |\
	 _DPRINTK_FLAGS_INCL_LINENO  | _DPRINTK_FLAGS_INCL_TID)

#if defined DEBUG
#define _DPRINTK_FLAGS_DEFAULT _DPRINTK_FLAGS_PRINT
#else
#define _DPRINTK_FLAGS_DEFAULT 0
#endif
	unsigned int flags:8;
#ifdef CONFIG_JUMP_LABEL
	union {
		struct static_key_true dd_key_true;
		struct static_key_false dd_key_false;
	} key;
#endif
} __attribute__((aligned(8)));



#if defined(CONFIG_DYNAMIC_DEBUG_CORE)

int ddebug_add_module(struct _ddebug *tab, unsigned int n,
				const char *modname);
extern int ddebug_remove_module(const char *mod_name);
extern __printf(2, 3)
void __dynamic_pr_debug(struct _ddebug *descriptor, const char *fmt, ...);

extern int ddebug_dyndbg_module_param_cb(char *param, char *val,
					const char *modname);

struct device;

extern __printf(3, 4)
void __dynamic_dev_dbg(struct _ddebug *descriptor, const struct device *dev,
		       const char *fmt, ...);

struct net_device;

extern __printf(3, 4)
void __dynamic_netdev_dbg(struct _ddebug *descriptor,
			  const struct net_device *dev,
			  const char *fmt, ...);

struct ib_device;

extern __printf(3, 4)
void __dynamic_ibdev_dbg(struct _ddebug *descriptor,
			 const struct ib_device *ibdev,
			 const char *fmt, ...);

#define DEFINE_DYNAMIC_DEBUG_METADATA_CLS(name, cls, fmt)	\
	static struct _ddebug  __aligned(8)			\
	__section("__dyndbg") name = {				\
		.modname = KBUILD_MODNAME,			\
		.function = __func__,				\
		.filename = __FILE__,				\
		.format = (fmt),				\
		.lineno = __LINE__,				\
		.flags = _DPRINTK_FLAGS_DEFAULT,		\
		.class_id = cls,				\
		_DPRINTK_KEY_INIT				\
	};							\
	BUILD_BUG_ON_MSG(cls > _DPRINTK_SITE_UNCLASSED,		\
			 "classid value overflow")

#define DEFINE_DYNAMIC_DEBUG_METADATA(name, fmt)		\
	DEFINE_DYNAMIC_DEBUG_METADATA_CLS(name, _DPRINTK_SITE_UNCLASSED, fmt)

#ifdef CONFIG_JUMP_LABEL

#ifdef DEBUG

#define _DPRINTK_KEY_INIT .key.dd_key_true = (STATIC_KEY_TRUE_INIT)

#define DYNAMIC_DEBUG_BRANCH(descriptor) \
	static_branch_likely(&descriptor.key.dd_key_true)
#else
#define _DPRINTK_KEY_INIT .key.dd_key_false = (STATIC_KEY_FALSE_INIT)

#define DYNAMIC_DEBUG_BRANCH(descriptor) \
	static_branch_unlikely(&descriptor.key.dd_key_false)
#endif

#else /* !CONFIG_JUMP_LABEL */

#define _DPRINTK_KEY_INIT

#ifdef DEBUG
#define DYNAMIC_DEBUG_BRANCH(descriptor) \
	likely(descriptor.flags & _DPRINTK_FLAGS_PRINT)
#else
#define DYNAMIC_DEBUG_BRANCH(descriptor) \
	unlikely(descriptor.flags & _DPRINTK_FLAGS_PRINT)
#endif

#endif /* CONFIG_JUMP_LABEL */

#define __dynamic_func_call_cls(id, cls, fmt, func, ...) do {	\
	DEFINE_DYNAMIC_DEBUG_METADATA_CLS(id, cls, fmt);	\
	if (DYNAMIC_DEBUG_BRANCH(id))				\
		func(&id, ##__VA_ARGS__);			\
} while (0)

#define __dynamic_func_call_no_desc_cls(id, cls, fmt, func, ...) do {	\
	DEFINE_DYNAMIC_DEBUG_METADATA_CLS(id, cls, fmt);		\
	if (DYNAMIC_DEBUG_BRANCH(id))					\
		func(__VA_ARGS__);					\
} while (0)

#define __dynamic_func_call(id, fmt, func, ...)				\
	__dynamic_func_call_cls(id, _DPRINTK_SITE_UNCLASSED,		\
				fmt, func, ##__VA_ARGS__)

#define __dynamic_func_call_no_desc(id, fmt, func, ...)			\
	__dynamic_func_call_no_desc_cls(id, _DPRINTK_SITE_UNCLASSED,	\
					fmt, func, ##__VA_ARGS__)

/*
 * "Factory macro" for generating a call to func, guarded by a
 * DYNAMIC_DEBUG_BRANCH. The dynamic debug descriptor will be
 * initialized using the fmt argument. The function will be called with
 * the address of the descriptor as first argument, followed by all
 * the varargs. Note that fmt is repeated in invocations of this
 * macro.
 */
#define _dynamic_func_call_cls(cls, fmt, func, ...)			\
	__dynamic_func_call_cls(__UNIQUE_ID(ddebug), cls, fmt, func, ##__VA_ARGS__)
#define _dynamic_func_call(fmt, func, ...)				\
	_dynamic_func_call_cls(_DPRINTK_SITE_UNCLASSED, fmt, func, ##__VA_ARGS__)

/*
 * A variant that does the same, except that the descriptor is not
 * passed as the first argument to the function; it is only called
 * with precisely the macro's varargs.
 */
#define _dynamic_func_call_no_desc_cls(fmt, cat, func, ...)		\
	__dynamic_func_call_no_desc_cls(__UNIQUE_ID(ddebug), cat,	\
					fmt, func, ##__VA_ARGS__)

#define _dynamic_func_call_no_desc(fmt, func, ...)			\
	__dynamic_func_call_no_desc_cls(__UNIQUE_ID(ddebug),		\
					_DPRINTK_SITE_UNCLASSED,	\
					fmt, func, ##__VA_ARGS__)

#define dynamic_pr_debug(fmt, ...)				\
	_dynamic_func_call(fmt,	__dynamic_pr_debug,		\
			   pr_fmt(fmt), ##__VA_ARGS__)

#define dynamic_dev_dbg(dev, fmt, ...)				\
	_dynamic_func_call(fmt,__dynamic_dev_dbg, 		\
			   dev, fmt, ##__VA_ARGS__)

#define dynamic_netdev_dbg(dev, fmt, ...)			\
	_dynamic_func_call(fmt, __dynamic_netdev_dbg,		\
			   dev, fmt, ##__VA_ARGS__)

#define dynamic_ibdev_dbg(dev, fmt, ...)			\
	_dynamic_func_call(fmt, __dynamic_ibdev_dbg,		\
			   dev, fmt, ##__VA_ARGS__)

#define dynamic_hex_dump(prefix_str, prefix_type, rowsize,		\
			 groupsize, buf, len, ascii)			\
	_dynamic_func_call_no_desc(__builtin_constant_p(prefix_str) ? prefix_str : "hexdump", \
				   print_hex_dump,			\
				   KERN_DEBUG, prefix_str, prefix_type,	\
				   rowsize, groupsize, buf, len, ascii)

struct kernel_param;
int param_set_dyndbg_classbits(const char *instr, const struct kernel_param *kp);
int param_get_dyndbg_classbits(char *buffer, const struct kernel_param *kp);

#else /* !CONFIG_DYNAMIC_DEBUG_CORE */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/printk.h>

static inline int ddebug_add_module(struct _ddebug *tab, unsigned int n,
				    const char *modname)
{
	return 0;
}

static inline int ddebug_remove_module(const char *mod)
{
	return 0;
}

static inline int ddebug_dyndbg_module_param_cb(char *param, char *val,
						const char *modname)
{
	if (strstr(param, "dyndbg")) {
		/* avoid pr_warn(), which wants pr_fmt() fully defined */
		printk(KERN_WARNING "dyndbg param is supported only in "
			"CONFIG_DYNAMIC_DEBUG builds\n");
		return 0; /* allow and ignore */
	}
	return -EINVAL;
}

#define dynamic_pr_debug(fmt, ...)					\
	do { if (0) printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__); } while (0)
#define dynamic_dev_dbg(dev, fmt, ...)					\
	do { if (0) dev_printk(KERN_DEBUG, dev, fmt, ##__VA_ARGS__); } while (0)
#define dynamic_hex_dump(prefix_str, prefix_type, rowsize,		\
			 groupsize, buf, len, ascii)			\
	do { if (0)							\
		print_hex_dump(KERN_DEBUG, prefix_str, prefix_type,	\
				rowsize, groupsize, buf, len, ascii);	\
	} while (0)

struct kernel_param;
static inline int param_set_dyndbg_classbits(const char *instr, const struct kernel_param *kp)
{ return 0; }
static inline int param_get_dyndbg_classbits(char *buffer, const struct kernel_param *kp)
{ return 0; }

#endif /* !CONFIG_DYNAMIC_DEBUG_CORE */

#define FLAGS_LEN 8
struct dyndbg_classbits_param {
	unsigned long *bits;		/* ref to shared state */
	const char flags[FLAGS_LEN];	/* toggle these flags on bit-changes */
	const int classes[];		/* indexed by bitpos */
};

#if defined(CONFIG_DYNAMIC_DEBUG) || defined(CONFIG_DYNAMIC_DEBUG_CORE)
/**
 * DEFINE_DYNAMIC_DEBUG_CLASSBITS() - bitmap control of classed pr_debugs
 * @sysname: sysfs-node name
 * @_var:    C-identifier holding bit-vector (Bits 0-14 are usable)
 * @_flgs:   string with dyndbg flags: 'p' and/or 'T', and maybe "fmlt" also.
 * @desc:    string summarizing the controls provided
 * @classes: vector of callsite.class_id's (uint:4, 15 is reserved)
 *
 * This macro implements a DRM.debug API style bitmap, mapping bits
 * 0-14 to classes of prdbg's, as initialized in their .class_id fields.
 * @_flgs chooses the debug recipient; p - syslog, T - tracefs, and
 * can include log decorations; m - module, f - function, l - line_num
 */
#define DEFINE_DYNAMIC_DEBUG_CLASSBITS(fsname, _var, _flgs, desc, ...)	\
	MODULE_PARM_DESC(fsname, desc);					\
	static struct dyndbg_classbits_param ddcats_##_var = {		\
		.bits = &(_var),					\
		.flags = _flgs,						\
		.classes = { __VA_ARGS__, _DPRINTK_SITE_UNCLASSED }	\
	};								\
	module_param_cb(fsname, &param_ops_dyndbg_classbits,		\
			&ddcats_##_var, 0644)

extern const struct kernel_param_ops param_ops_dyndbg_classbits;

#else /* no dyndbg configured, throw error on macro use */

#define DEFINE_DYNAMIC_DEBUG_CLASSBITS(fsname, var, bitmap_desc, ...)	\
	BUILD_BUG_ON_MSG(1, "CONFIG_DYNAMIC_DEBUG* needed to use this macro: " #fsname)

#endif

#endif
