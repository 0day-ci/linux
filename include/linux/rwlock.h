#ifndef __LINUX_RWLOCK_H
#define __LINUX_RWLOCK_H

#ifndef __LINUX_SPINLOCK_H
# error "please don't include this file directly"
#endif

/*
 * rwlock related methods
 *
 * split out from spinlock.h
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 */

#ifdef CONFIG_DEBUG_SPINLOCK
  extern void __rwlock_init(rwlock_t *lock, const char *name,
			    struct lock_class_key *key);
# define rwlock_init(lock)					\
do {								\
	static struct lock_class_key __key;			\
								\
	__rwlock_init((lock), #lock, &__key);			\
} while (0)
#else
# define rwlock_init(lock)					\
	do { *(lock) = __RW_LOCK_UNLOCKED(lock); } while (0)
#endif

#ifdef CONFIG_DEPT
#define DEPT_EVT_RWLOCK_R		1UL
#define DEPT_EVT_RWLOCK_W		(1UL << 1)
#define DEPT_EVT_RWLOCK_RW		(DEPT_EVT_RWLOCK_R | DEPT_EVT_RWLOCK_W)

#define dept_rwlock_wlock(m, ne, t, n, e_fn, ip)			\
do {									\
	if (t) {							\
		dept_ecxt_enter(m, DEPT_EVT_RWLOCK_W, ip, __func__, e_fn, ne);\
		dept_ask_event(m);					\
	} else if (n) {							\
		dept_skip(m);						\
	} else {							\
		dept_wait(m, DEPT_EVT_RWLOCK_RW, ip, __func__, ne);	\
		dept_ecxt_enter(m, DEPT_EVT_RWLOCK_W, ip, __func__, e_fn, ne);\
		dept_ask_event(m);					\
	}								\
} while (0)
#define dept_rwlock_rlock(m, ne, t, n, e_fn, ip, q)			\
do {									\
	if (t) {							\
		dept_ecxt_enter(m, DEPT_EVT_RWLOCK_R, ip, __func__, e_fn, ne);\
		dept_ask_event(m);					\
	} else if (n) {							\
		dept_skip(m);						\
	} else {							\
		dept_wait(m, (q) ? DEPT_EVT_RWLOCK_RW : DEPT_EVT_RWLOCK_W, ip, __func__, ne);\
		dept_ecxt_enter(m, DEPT_EVT_RWLOCK_R, ip, __func__, e_fn, ne);\
		dept_ask_event(m);					\
	}								\
} while (0)
#define dept_rwlock_wunlock(m, ip)					\
do {									\
	if (!dept_unskip_if_skipped(m)) {				\
		dept_event(m, DEPT_EVT_RWLOCK_W, ip, __func__);		\
		dept_ecxt_exit(m, ip);					\
	}								\
} while (0)
#define dept_rwlock_runlock(m, ip)					\
do {									\
	if (!dept_unskip_if_skipped(m)) {				\
		dept_event(m, DEPT_EVT_RWLOCK_R, ip, __func__);		\
		dept_ecxt_exit(m, ip);					\
	}								\
} while (0)
#else
#define dept_rwlock_wlock(m, ne, t, n, e_fn, ip)	do { } while (0)
#define dept_rwlock_rlock(m, ne, t, n, e_fn, ip, q)	do { } while (0)
#define dept_rwlock_wunlock(m, ip)			do { } while (0)
#define dept_rwlock_runlock(m, ip)			do { } while (0)
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
 extern void do_raw_read_lock(rwlock_t *lock) __acquires(lock);
 extern int do_raw_read_trylock(rwlock_t *lock);
 extern void do_raw_read_unlock(rwlock_t *lock) __releases(lock);
 extern void do_raw_write_lock(rwlock_t *lock) __acquires(lock);
 extern int do_raw_write_trylock(rwlock_t *lock);
 extern void do_raw_write_unlock(rwlock_t *lock) __releases(lock);
#else
# define do_raw_read_lock(rwlock)	do {__acquire(lock); arch_read_lock(&(rwlock)->raw_lock); } while (0)
# define do_raw_read_trylock(rwlock)	arch_read_trylock(&(rwlock)->raw_lock)
# define do_raw_read_unlock(rwlock)	do {arch_read_unlock(&(rwlock)->raw_lock); __release(lock); } while (0)
# define do_raw_write_lock(rwlock)	do {__acquire(lock); arch_write_lock(&(rwlock)->raw_lock); } while (0)
# define do_raw_write_trylock(rwlock)	arch_write_trylock(&(rwlock)->raw_lock)
# define do_raw_write_unlock(rwlock)	do {arch_write_unlock(&(rwlock)->raw_lock); __release(lock); } while (0)
#endif

/*
 * Define the various rw_lock methods.  Note we define these
 * regardless of whether CONFIG_SMP or CONFIG_PREEMPT are set. The various
 * methods are defined as nops in the case they are not required.
 */
#define read_trylock(lock)	__cond_lock(lock, _raw_read_trylock(lock))
#define write_trylock(lock)	__cond_lock(lock, _raw_write_trylock(lock))

#define write_lock(lock)	_raw_write_lock(lock)
#define read_lock(lock)		_raw_read_lock(lock)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define write_lock_nested(lock, subclass)	_raw_write_lock_nested(lock, subclass)
#else
#define write_lock_nested(lock, subclass)	_raw_write_lock(lock)
#endif

#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)

#define read_lock_irqsave(lock, flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = _raw_read_lock_irqsave(lock);	\
	} while (0)
#define write_lock_irqsave(lock, flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = _raw_write_lock_irqsave(lock);	\
	} while (0)

#else

#define read_lock_irqsave(lock, flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		_raw_read_lock_irqsave(lock, flags);	\
	} while (0)
#define write_lock_irqsave(lock, flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		_raw_write_lock_irqsave(lock, flags);	\
	} while (0)

#endif

#define read_lock_irq(lock)		_raw_read_lock_irq(lock)
#define read_lock_bh(lock)		_raw_read_lock_bh(lock)
#define write_lock_irq(lock)		_raw_write_lock_irq(lock)
#define write_lock_bh(lock)		_raw_write_lock_bh(lock)
#define read_unlock(lock)		_raw_read_unlock(lock)
#define write_unlock(lock)		_raw_write_unlock(lock)
#define read_unlock_irq(lock)		_raw_read_unlock_irq(lock)
#define write_unlock_irq(lock)		_raw_write_unlock_irq(lock)

#define read_unlock_irqrestore(lock, flags)			\
	do {							\
		typecheck(unsigned long, flags);		\
		_raw_read_unlock_irqrestore(lock, flags);	\
	} while (0)
#define read_unlock_bh(lock)		_raw_read_unlock_bh(lock)

#define write_unlock_irqrestore(lock, flags)		\
	do {						\
		typecheck(unsigned long, flags);	\
		_raw_write_unlock_irqrestore(lock, flags);	\
	} while (0)
#define write_unlock_bh(lock)		_raw_write_unlock_bh(lock)

#define write_trylock_irqsave(lock, flags) \
({ \
	local_irq_save(flags); \
	write_trylock(lock) ? \
	1 : ({ local_irq_restore(flags); 0; }); \
})

#ifdef arch_rwlock_is_contended
#define rwlock_is_contended(lock) \
	 arch_rwlock_is_contended(&(lock)->raw_lock)
#else
#define rwlock_is_contended(lock)	((void)(lock), 0)
#endif /* arch_rwlock_is_contended */

#endif /* __LINUX_RWLOCK_H */
