/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __WORKQUEUE_SPARSE_H
#define __WORKQUEUE_SPARSE_H

#ifdef __CHECKER__
# define __must_hold(x)		__attribute__((context(x, 1, 1)))
# define __acquires(x)		__attribute__((context(x, 0, 1)))
# define __releases(x)		__attribute__((context(x, 1, 0)))
# define __acquire(x)		__context__(x, 1)
# define __release(x)		__context__(x, -1)
# define __cond_lock(x, c)	((c) ? ({ __acquire(x); 1; }) : 0)
#else
# define __must_hold(x)
# define __acquires(x)
# define __releases(x)
# define __acquire(x)		((void)0)
# define __release(x)		((void)0)
# define __cond_lock(x, c)	(c)
#endif

#endif /* __WORKQUEUE_SPARSE_H */
