/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MUST_BE_H
#define _LINUX_MUST_BE_H


#define __must_be(e)  BUILD_BUG_ON_ZERO(!(e))


#endif	/* _LINUX_MUST_BE_H */
