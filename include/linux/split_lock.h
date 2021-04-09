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

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define split_lock_init(_lock) do {					\
	static struct lock_class_key __key;				\
	lockdep_init_map_wait(&(_lock)->dep_map, #_lock, &__key, 0,	\
				LD_WAIT_SPIN);				\
} while (0)
#else
static inline void split_lock_init(struct split_lock *sl) { }
#endif

#endif /* _LINUX_SPLIT_LOCK_H */
