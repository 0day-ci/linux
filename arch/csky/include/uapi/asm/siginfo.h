/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CSKY_SIGINFO_H
#define _CSKY_SIGINFO_H

#include <asm-generic/siginfo.h>

#undef __SI_FAULT
#define __SI_FAULT	-2

#endif
