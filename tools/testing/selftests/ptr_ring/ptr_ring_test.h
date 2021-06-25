/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _TEST_PTR_RING_IMPL_H
#define _TEST_PTR_RING_IMPL_H

#if defined(__x86_64__) || defined(__i386__)
static inline void cpu_relax(void)
{
	asm volatile ("rep; nop" ::: "memory");
}
#elif defined(__aarch64__)
static inline void cpu_relax(void)
{
	asm volatile("yield" ::: "memory");
}
#else
#define cpu_relax() assert(0)
#endif

static inline void barrier(void)
{
	asm volatile("" ::: "memory");
}

/*
 * This abuses the atomic builtins for thread fences, and
 * adds a compiler barrier.
 */
#define smp_release() do { \
	barrier(); \
	__atomic_thread_fence(__ATOMIC_RELEASE); \
} while (0)

#define smp_acquire() do { \
	__atomic_thread_fence(__ATOMIC_ACQUIRE); \
	barrier(); \
} while (0)

#if defined(__i386__) || defined(__x86_64__)
#define smp_wmb()		barrier()
#else
#define smp_wmb()		smp_release()
#endif

#define READ_ONCE(x)		(*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val)	((*(volatile typeof(x) *)&(x)) = (val))
#define SMP_CACHE_BYTES		64
#define cache_line_size		SMP_CACHE_BYTES
#define unlikely(x)		(__builtin_expect(!!(x), 0))
#define likely(x)		(__builtin_expect(!!(x), 1))
#define ALIGN(x, a)		(((x) + (a) - 1) / (a) * (a))
#define SIZE_MAX		(~(size_t)0)
#define KMALLOC_MAX_SIZE	SIZE_MAX
#define spinlock_t		pthread_spinlock_t
#define gfp_t			int
#define __GFP_ZERO		0x1

#define ____cacheline_aligned_in_smp __attribute__((aligned(SMP_CACHE_BYTES)))

static void *kmalloc(unsigned int size, gfp_t gfp)
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

static void kfree(void *p)
{
	free(p);
}

#define kvmalloc_array		kmalloc_array
#define kvfree			kfree

static void spin_lock_init(spinlock_t *lock)
{
	int r = pthread_spin_init(lock, 0);

	assert(!r);
}

static void spin_lock(spinlock_t *lock)
{
	int ret = pthread_spin_lock(lock);

	assert(!ret);
}

static void spin_unlock(spinlock_t *lock)
{
	int ret = pthread_spin_unlock(lock);

	assert(!ret);
}

static void spin_lock_bh(spinlock_t *lock)
{
	spin_lock(lock);
}

static void spin_unlock_bh(spinlock_t *lock)
{
	spin_unlock(lock);
}

static void spin_lock_irq(spinlock_t *lock)
{
	spin_lock(lock);
}

static void spin_unlock_irq(spinlock_t *lock)
{
	spin_unlock(lock);
}

static void spin_lock_irqsave(spinlock_t *lock, unsigned long f)
{
	spin_lock(lock);
}

static void spin_unlock_irqrestore(spinlock_t *lock, unsigned long f)
{
	spin_unlock(lock);
}

#endif
