/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STATIC_CALL_TYPES_H
#define _STATIC_CALL_TYPES_H

#include <linux/types.h>
#include <linux/stringify.h>
#include <linux/compiler.h>

#define STATIC_CALL_KEY_PREFIX		__SCK__
#define STATIC_CALL_KEY_PREFIX_STR	__stringify(STATIC_CALL_KEY_PREFIX)
#define STATIC_CALL_KEY_PREFIX_LEN	(sizeof(STATIC_CALL_KEY_PREFIX_STR) - 1)
#define STATIC_CALL_KEY(name)		__PASTE(STATIC_CALL_KEY_PREFIX, name)
#define STATIC_CALL_KEY_STR(name)	__stringify(STATIC_CALL_KEY(name))

#define STATIC_CALL_TRAMP_PREFIX	__SCT__
#define STATIC_CALL_TRAMP_PREFIX_STR	__stringify(STATIC_CALL_TRAMP_PREFIX)
#define STATIC_CALL_TRAMP_PREFIX_LEN	(sizeof(STATIC_CALL_TRAMP_PREFIX_STR) - 1)
#define STATIC_CALL_TRAMP(name)		__PASTE(STATIC_CALL_TRAMP_PREFIX, name)
#define STATIC_CALL_TRAMP_STR(name)	__stringify(STATIC_CALL_TRAMP(name))

#define STATIC_CALL_GETKEY_PREFIX	__SCG__
#define STATIC_CALL_GETKEY_PREFIX_STR	__stringify(STATIC_CALL_GETKEY_PREFIX)
#define STATIC_CALL_GETKEY_PREFIX_LEN	(sizeof(STATIC_CALL_GETKEY_PREFIX_STR) - 1)
#define STATIC_CALL_GETKEY(name)	__PASTE(STATIC_CALL_GETKEY_PREFIX, name)

#define STATIC_CALL_QUERY_PREFIX	__SCQ__
#define STATIC_CALL_QUERY(name)		__PASTE(STATIC_CALL_QUERY_PREFIX, name)

/*
 * Flags in the low bits of static_call_site::key.
 */
#define STATIC_CALL_SITE_TAIL 1UL	/* tail call */
#define STATIC_CALL_SITE_INIT 2UL	/* init section */
#define STATIC_CALL_SITE_FLAGS 3UL

/*
 * The static call site table needs to be created by external tooling (objtool
 * or a compiler plugin).
 */
struct static_call_site {
	s32 addr;
	s32 key;
	s32 helper;
};

#define DECLARE_STATIC_CALL(name, func)					\
	extern __weak struct static_call_key STATIC_CALL_KEY(name);	\
	extern __weak struct static_call_key *STATIC_CALL_GETKEY(name)(void);\
	extern __weak typeof(func) *STATIC_CALL_QUERY(name)(void);	\
	extern struct static_call_tramp STATIC_CALL_TRAMP(name)

#define __static_call_query(name)					\
	((typeof(STATIC_CALL_QUERY(name)()))READ_ONCE(STATIC_CALL_KEY(name).func))

#ifdef MODULE
/* the key might not be exported */
#define static_call_query(name)						\
	(&STATIC_CALL_KEY(name) ? __static_call_query(name)		\
				: STATIC_CALL_QUERY(name)())
#else
#define static_call_query(name)	__static_call_query(name)
#endif

#ifdef CONFIG_HAVE_STATIC_CALL

#define static_call(name)						\
({									\
	__STATIC_CALL_ADDRESSABLE(name);				\
	((typeof(STATIC_CALL_QUERY(name)()))&STATIC_CALL_TRAMP(name));	\
})

#ifdef CONFIG_HAVE_STATIC_CALL_INLINE

/*
 * __ADDRESSABLE() is used to ensure the key symbol doesn't get stripped from
 * the symbol table so that objtool can reference it when it generates the
 * .static_call_sites section.
 */
#define __STATIC_CALL_ADDRESSABLE(name) \
	__ADDRESSABLE(STATIC_CALL_GETKEY(name)) \
	__ADDRESSABLE(STATIC_CALL_KEY(name))

struct static_call_key {
	void *func;
	union {
		/* bit 0: 0 = mods, 1 = sites */
		unsigned long type;
		struct static_call_mod *mods;
		struct static_call_site *sites;
	};
};

#else /* !CONFIG_HAVE_STATIC_CALL_INLINE */

#define __STATIC_CALL_ADDRESSABLE(name)

struct static_call_key {
	void *func;
};

#endif /* CONFIG_HAVE_STATIC_CALL_INLINE */

#else

struct static_call_key {
	void *func;
};

#define static_call(name)	static_call_query(name)

#endif /* CONFIG_HAVE_STATIC_CALL */

#endif /* _STATIC_CALL_TYPES_H */
