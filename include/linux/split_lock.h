#ifndef _LINUX_SPLIT_LOCK_H
#define _LINUX_SPLIT_LOCK_H

#include <linux/lockdep_types.h>

struct split_lock {
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define SPLIT_DEP_MAP_INIT(lockname)					\
	.dep_map = {							\
		.name = #lockname,					\
		.wait_type_inner = LD_WAIT_SPIN,			\
	}
#else
#define SPLIT_DEP_MAP_INIT(lockname)
#endif

#define DEFINE_SPLIT_LOCK(name)						\
struct split_lock name = {						\
	SPLIT_DEP_MAP_INIT(name)					\
};

/*
 * This is only called if we're contended.  We use a non-atomic test
 * to reduce contention on the cacheline while we wait.
 */
static inline void split_lock_spin(struct split_lock *lock, int bitnum,
		unsigned long *addr)
{
	preempt_enable();
	do {
		cpu_relax();
	} while (test_bit(bitnum, addr));
	preempt_disable();
}

static inline void split_lock_unlock(struct split_lock *lock, int bitnum,
		unsigned long *addr)
{
}
#endif /* _LINUX_SPLIT_LOCK_H */
