// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests were selected from NVM Express NVM Command Set Specification 1.0a,
 * section 5.2.1.3.5 "64b CRC Test Cases" available here:
 *
 *   https://nvmexpress.org/wp-content/uploads/NVMe-NVM-Command-Set-Specification-1.0a-2021.07.26-Ratified.pdf
 *
 * Copyright 2022 Keith Busch <kbusch@kernel.org>
 */

#include <linux/crc64.h>
#include <linux/module.h>

static unsigned int tests_passed;
static unsigned int tests_run;

#define ALL_ZEROS 0x6482D367EB22B64EULL
#define ALL_FFS 0xC0DDBA7302ECA3ACULL
#define INC 0x3E729F5F6750449CULL
#define DEC 0x9A2DF64B8E9E517EULL

static u8 buffer[4096];

#define CRC_CHECK(c, v) do {					\
	tests_run++;						\
	if (c != v)						\
		printk("BUG at %s:%d expected:%llx got:%llx\n", \
			__func__, __LINE__, v, c);		\
	else							\
		tests_passed++;					\
} while (0)


static int crc_tests(void)
{
	__u64 crc;
	int i;

	memset(buffer, 0, sizeof(buffer));
	crc = crc64_rocksoft(~0ULL, buffer, 4096);
	CRC_CHECK(crc, ALL_ZEROS);

	memset(buffer, 0xff, sizeof(buffer));
	crc = crc64_rocksoft(~0ULL, buffer, 4096);
	CRC_CHECK(crc, ALL_FFS);

	for (i = 0; i < 4096; i++)
		buffer[i] = i & 0xff;
	crc = crc64_rocksoft(~0ULL, buffer, 4096);
	CRC_CHECK(crc, INC);

	for (i = 0; i < 4096; i++)
		buffer[i] = 0xff - (i & 0xff);
	crc = crc64_rocksoft(~0ULL, buffer, 4096);
	CRC_CHECK(crc, DEC);

	printk("CRC64: %u of %u tests passed\n", tests_passed, tests_run);
	return (tests_run == tests_passed) ? 0 : -EINVAL;
}

static void crc_exit(void)
{
}

module_init(crc_tests);
module_exit(crc_exit);
MODULE_AUTHOR("Keith Busch <kbusch@kernel.org>");
MODULE_LICENSE("GPL");
