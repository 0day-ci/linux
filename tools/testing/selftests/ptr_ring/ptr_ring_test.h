/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _TEST_PTR_RING_TEST_H
#define _TEST_PTR_RING_TEST_H

#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>

/* Assuming the cache line size is 64 for most cpu,
 * change it accordingly if the running cpu has different
 * cache line size in order to get more accurate result.
 */
#define SMP_CACHE_BYTES	64

#define cpu_relax()	sched_yield()
#define smp_release()	atomic_thread_fence(memory_order_release)
#define smp_acquire()	atomic_thread_fence(memory_order_acquire)
#define smp_wmb()	smp_release()
#define smp_store_release(p, v)	\
		atomic_store_explicit(p, v, memory_order_release)

#define READ_ONCE(x)		(*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val)	((*(volatile typeof(x) *)&(x)) = (val))
#define cache_line_size		SMP_CACHE_BYTES
#define unlikely(x)		(__builtin_expect(!!(x), 0))
#define likely(x)		(__builtin_expect(!!(x), 1))
#define ALIGN(x, a)		(((x) + (a) - 1) / (a) * (a))
#define SIZE_MAX		(~(size_t)0)
#define KMALLOC_MAX_SIZE	SIZE_MAX
#define spinlock_t		pthread_spinlock_t
#define gfp_t			int
#define __GFP_ZERO		0x1

#define ____cacheline_aligned_in_smp \
		__attribute__((aligned(SMP_CACHE_BYTES)))

static inline void *kmalloc(unsigned int size, gfp_t gfp)
{
	void *p;

	p = memalign(64, size);
	if (!p)
		return p;

	if (gfp & __GFP_ZERO)
		memset(p, 0, size);

	return p;
}

static inline void *kzalloc(unsigned int size, gfp_t flags)
{
	return kmalloc(size, flags | __GFP_ZERO);
}

static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;
	return kmalloc(n * size, flags);
}

static inline void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}

static inline void kfree(void *p)
{
	free(p);
}

#define kvmalloc_array		kmalloc_array
#define kvfree			kfree

static inline void spin_lock_init(spinlock_t *lock)
{
	int r = pthread_spin_init(lock, 0);

	assert(!r);
}

static inline void spin_lock(spinlock_t *lock)
{
	int ret = pthread_spin_lock(lock);

	assert(!ret);
}

static inline void spin_unlock(spinlock_t *lock)
{
	int ret = pthread_spin_unlock(lock);

	assert(!ret);
}

static inline void spin_lock_bh(spinlock_t *lock)
{
	spin_lock(lock);
}

static inline void spin_unlock_bh(spinlock_t *lock)
{
	spin_unlock(lock);
}

static inline void spin_lock_irq(spinlock_t *lock)
{
	spin_lock(lock);
}

static inline void spin_unlock_irq(spinlock_t *lock)
{
	spin_unlock(lock);
}

static inline void spin_lock_irqsave(spinlock_t *lock,
				     unsigned long f)
{
	spin_lock(lock);
}

static inline void spin_unlock_irqrestore(spinlock_t *lock,
					  unsigned long f)
{
	spin_unlock(lock);
}

#endif
