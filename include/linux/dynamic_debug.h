/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DYNAMIC_DEBUG_H
#define _DYNAMIC_DEBUG_H

#if defined(CONFIG_JUMP_LABEL)
#include <linux/jump_label.h>
#endif

/*
 * A pair of these structs are created in 2 special ELF sections
 * (__dyndbg, __dyndbg_sites) for every dynamic debug callsite.
 * At runtime, the sections are treated as arrays.
 */
struct _ddebug;
struct _ddebug_site {
	/*
	 * These fields (and lineno) are used to:
	 * - decorate log messages per site flags
	 * - select callsites for modification via >control
	 * - display callsites & settings in `cat control`
	 */
	const char *modname;
	const char *filename;
	const char *function;
} __aligned(8);

struct _ddebug {
	struct _ddebug_site *site;
	/* format is always needed, lineno shares word with flags */
	const char *format;
	const unsigned lineno:18;
	unsigned _index:14;
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
#define _DPRINTK_FLAGS_DELETE_SITE	(1<<7) /* drop site info to save ram */

#define _DPRINTK_FLAGS_INCL_ANYSITE		\
	(_DPRINTK_FLAGS_INCL_MODNAME		\
	 | _DPRINTK_FLAGS_INCL_FUNCNAME		\
	 | _DPRINTK_FLAGS_INCL_LINENO)
#define _DPRINTK_FLAGS_INCL_ANY			\
	(_DPRINTK_FLAGS_INCL_ANYSITE		\
	 | _DPRINTK_FLAGS_INCL_TID)

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
} __aligned(8);


#if defined(CONFIG_DYNAMIC_DEBUG_CORE)

/* exported for module authors to exercise >control */
int dynamic_debug_exec_queries(const char *query, const char *modname);

int ddebug_add_module(struct _ddebug *tab, struct _ddebug_site *sites,
		      unsigned int numdbgs, const char *modname);
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

/**
 * DEFINE_DYNAMIC_DEBUG_TABLE(), DECLARE_DYNAMIC_DEBUG_TABLE()
 *
 * These are special versions of DEFINE_DYNAMIC_DEBUG_METADATA().  The
 * job is to create/reserve a module-header struct-pair as the last
 * element of the module's sub-vectors of __dyndbg & __dyndbg_sites,
 * ie at a fixed offset from them.  I expect to settle on 1 of these
 * 2; DEFINE_ has seen most testing recently, and is favored.
 *
 * With this record reliably in-situ at a fixed offset for each
 * callsite, we can use ._index to remember this offset, find the
 * header, find the parallel vector, then index the corresponding site
 * data.
 *
 * This macro is invoked at the bottom of this header.  It is
 * typically invoked multiple times for a module, generally at least
 * once per object file.  The combination of .gnu.linkonce._ and
 * __weak appear to resolve earlier troubles with compile errors or
 * multiple copies of headers.
 */

#define DECLARE_DYNAMIC_DEBUG_TABLE_(_sym_, _mod_)	       	\
	static struct _ddebug_site				\
	__section(".gnu.linkonce.dyndbg_site")			\
		__aligned(8)					\
		__maybe_unused					\
		_sym_##_dyndbg_site;				\
	static struct _ddebug					\
	__section(".gnu.linkonce.dyndbg")			\
		__aligned(8)					\
		__maybe_unused					\
		_sym_##_dyndbg_base

#define DYNAMIC_DEBUG_TABLE_refer(_sym_)			\
	static void __used _sym_##_take_internal_refs(void)	\
{								\
	struct _ddebug_site * dc = &_sym_##_dyndbg_site;	\
	struct _ddebug * dp = &_sym_##_dyndbg_base;		\
	printk(KERN_INFO "%s %d\n", dc->function, dp->lineno);	\
}

#define DECLARE_DYNAMIC_DEBUG_TABLE()		       			\
	DECLARE_DYNAMIC_DEBUG_TABLE_(KBUILD_MODSYM, KBUILD_MODNAME);	\
	DYNAMIC_DEBUG_TABLE_refer(KBUILD_MODSYM)

//#define DEFN_SC static // clashes with extern forward decl
//#define DEFN_SC extern // warning: ‘KBUILD_MODNAME_dyndbg_site’ initialized and declared ‘extern’
//#define DEFN_SC // no section allowd on locals
#define DEFN_SC __weak

#define DEFINE_DYNAMIC_DEBUG_TABLE_(_sym_,_mod_)	       	\
	DEFN_SC struct _ddebug_site				\
	__section(".gnu.linkonce.dyndbg_site")			\
		__used __aligned(8)				\
	_sym_ ##_dyndbg_site = {				\
		.modname = _mod_,				\
		.filename = __FILE__,				\
		.function = (void*) _mod_			\
	};							\
	DEFN_SC struct _ddebug					\
	__section(".gnu.linkonce.dyndbg")			\
		__used __aligned(8)				\
	_sym_ ##_dyndbg_base = {				\
		.site = & _sym_ ##_dyndbg_site,			\
		.format = _mod_,				\
		.lineno = 0					\
	}

/* above init conditions as distinguishing predicate.
 * (site == iter->site) should work but doesnt, possibly cuz MODSYM
 * expansion problem
 */
#define is_dyndbg_header_pair(iter, site)			\
	((iter->format == site->modname)			\
	 && (site->modname == site->function))

// build-time expensive, shows repetitive includes
// #pragma message "<" __stringify(KBUILD_MODSYM) "> adding DYNDBG_TABLE"

#define DEFINE_DYNAMIC_DEBUG_TABLE()				\
	DEFINE_DYNAMIC_DEBUG_TABLE_(KBUILD_MODSYM, KBUILD_MODNAME);

#define DEFINE_DYNAMIC_DEBUG_METADATA_(_mod_, name, fmt)	\
	static struct _ddebug_site  __aligned(8)		\
	__section("__dyndbg_sites") name##_site = {		\
		.modname = _mod_,				\
		.filename = __FILE__,				\
		.function = __func__,				\
	};							\
	static struct _ddebug  __aligned(8)			\
	__section("__dyndbg") name = {				\
		.site = &name##_site,				\
		.format = (fmt),				\
		.lineno = __LINE__,				\
		.flags = _DPRINTK_FLAGS_DEFAULT,		\
		._index = 0,					\
		_DPRINTK_KEY_INIT				\
	}

#define DEFINE_DYNAMIC_DEBUG_METADATA(name, fmt)		\
	DEFINE_DYNAMIC_DEBUG_METADATA_(KBUILD_MODNAME, name, fmt)

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

#define __dynamic_func_call(id, fmt, func, ...) do {	\
	DEFINE_DYNAMIC_DEBUG_METADATA(id, fmt);		\
	if (DYNAMIC_DEBUG_BRANCH(id))			\
		func(&id, ##__VA_ARGS__);		\
} while (0)

#define __dynamic_func_call_no_desc(id, fmt, func, ...) do {	\
	DEFINE_DYNAMIC_DEBUG_METADATA(id, fmt);			\
	if (DYNAMIC_DEBUG_BRANCH(id))				\
		func(__VA_ARGS__);				\
} while (0)

/*
 * "Factory macro" for generating a call to func, guarded by a
 * DYNAMIC_DEBUG_BRANCH. The dynamic debug descriptor will be
 * initialized using the fmt argument. The function will be called with
 * the address of the descriptor as first argument, followed by all
 * the varargs. Note that fmt is repeated in invocations of this
 * macro.
 */
#define _dynamic_func_call(fmt, func, ...)				\
	__dynamic_func_call(__UNIQUE_ID(ddebug), fmt, func, ##__VA_ARGS__)
/*
 * A variant that does the same, except that the descriptor is not
 * passed as the first argument to the function; it is only called
 * with precisely the macro's varargs.
 */
#define _dynamic_func_call_no_desc(fmt, func, ...)	\
	__dynamic_func_call_no_desc(__UNIQUE_ID(ddebug), fmt, func, ##__VA_ARGS__)

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

static inline int dynamic_debug_exec_queries(const char *query, const char *modname)
{
	pr_warn("kernel not built with CONFIG_DYNAMIC_DEBUG_CORE\n");
	return 0;
}

#endif /* !CONFIG_DYNAMIC_DEBUG_CORE */

#if ((defined(CONFIG_DYNAMIC_DEBUG) ||						\
      (defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE)))	\
     && defined(KBUILD_MODNAME)							\
     && !defined(NO_DYNAMIC_DEBUG_TABLE))

/* transparently invoked, except for -DNO_DYNAMIC_DEBUG_TABLE */
DEFINE_DYNAMIC_DEBUG_TABLE()

#endif

#endif
