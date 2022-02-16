// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Joe Lawrence <joe.lawrence@redhat.com>

#include <linux/types.h>

/*
 * test_klp_convert_data will create klp-relocations to these variables.
 * They are named for the storage class of the variable that refers to
 * the.  This makes it easier to correlate those symbols to the
 * relocations that refer to them in readelf output.
 */
__used static int global_small = 0x1111;
// .rela.data.rel.ro, .rela.rodata supported ???:
__used static int const_global_small = 0x2222;
__used static int static_small = 0x3333;
__used static int static_const_small = 0x4444;

__used static int global_large[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };
// .rela.data.rel.ro, .rela.rodata supported ???:
__used static int const_global_large[4] = { 0x5555, 0x6666, 0x7777, 0x8888 };
__used static int static_large[4] = { 0x9999, 0xaaaa, 0xbbbb, 0xcccc };
__used static int static_const_large[4] = { 0xdddd, 0xeeee, 0xffff, 0x0000 };

__used static int local_small = 0x1111;
__used static int const_local_small = 0x2222;
__used static int static_local_small = 0x3333;
__used static int static_const_local_small = 0x4444;

__used static int local_large[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };
__used static int const_local_large[4] = { 0x5555, 0x6666, 0x7777, 0x8888 };
__used static int static_local_large[4] = { 0x9999, 0xaaaa, 0xbbbb, 0xcccc };
__used static int static_const_local_large[4] = { 0xdddd, 0xeeee, 0xffff, 0x0000 };

// .rela.data..ro_after_init supported ???:
__used static int static_ro_after_init = 0x1111;
__used static int static_read_mostly = 0x2222;
