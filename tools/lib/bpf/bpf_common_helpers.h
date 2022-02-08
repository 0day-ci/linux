/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_COMMON_HELPERS__
#define __BPF_COMMON_HELPERS__

/*
 * Helper macros that can be used both by libbpf and bpf progs.
 */

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((unsigned long)&((TYPE *)0)->MEMBER)
#endif

#ifndef sizeof_field
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#endif

#ifndef offsetofend
#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER) + sizeof_field(TYPE, MEMBER))
#endif

#ifndef container_of
#define container_of(ptr, type, member)				\
	({							\
		void *__mptr = (void *)(ptr);			\
		((type *)(__mptr - offsetof(type, member)));	\
	})
#endif

#endif
