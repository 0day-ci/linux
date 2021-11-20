/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_POWER_OF_2_H
#define _LINUX_POWER_OF_2_H


#define __IS_POWER_OF_2_OR_0(n)  (((n) & ((n) - 1)) == 0)
#define __IS_POWER_OF_2(n)       (__IS_POWER_OF_2_OR_0(n) && ((n) != 0))


#endif	/* _LINUX_POWER_OF_2_H */
