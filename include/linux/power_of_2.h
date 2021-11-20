/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_POWER_OF_2_H
#define _LINUX_POWER_OF_2_H


#include <linux/build_bug.h>


#define __IS_POWER_OF_2_OR_0(n)  (((n) & ((n) - 1)) == 0)
#define __IS_POWER_OF_2(n)       (__IS_POWER_OF_2_OR_0(n) && ((n) != 0))

/* Force a compilation error if a constant expression is not a power of 2 */
#define __BUILD_BUG_ON_NOT_POWER_OF_2(n)  BUILD_BUG_ON(!__IS_POWER_OF_2_OR_0(n))
#define BUILD_BUG_ON_NOT_POWER_OF_2(n)    BUILD_BUG_ON(!__IS_POWER_OF_2(n))


#endif	/* _LINUX_POWER_OF_2_H */
