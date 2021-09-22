// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

#ifdef HAVE_DWARF_UNWIND_SUPPORT
DEFINE_SUITE("DWARF unwind", dwarf_unwind);
#endif

struct test *arch_tests[] = {
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	&dwarf_unwind,
#endif
	NULL,
};
