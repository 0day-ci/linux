// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>

#define MAX_PHYS_REGIONS	16
#define INVALID_VALUE		(~0ull)

struct phys_regions_test {
	u64 paddr;
	u64 size;
	int expect_rc;
	int expect_num;
	u64 expect_last_paddr;
	u64 expect_last_size;
} phys_regions_test_cases[] = {
	/*
	 * Add the region from 0x1000 to (0x1000 + 0x200000 - 1):
	 *   Expected result:
	 *       Failed, start address is not 2M-aligned
	 *
	 * Now the instance of struct phys_contig_mem_regions is:
	 *   num = 0
	 *   region = {}
	 */
	{0x1000, 0x200000, -EINVAL, 0, INVALID_VALUE, INVALID_VALUE},

	/*
	 * Add the region from 0x200000 to (0x200000 + 0x1000 - 1):
	 *   Expected result:
	 *       Failed, size is not 2M-aligned
	 *
	 * Now the instance of struct phys_contig_mem_regions is:
	 *   num = 0
	 *   region = {}
	 */
	{0x200000, 0x1000, -EINVAL, 0, INVALID_VALUE, INVALID_VALUE},

	/*
	 * Add the region from 0x200000 to (0x200000 + 0x200000 - 1):
	 *   Expected result:
	 *       Successful
	 *
	 * Now the instance of struct phys_contig_mem_regions is:
	 *   num = 1
	 *   region = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *   }
	 */
	{0x200000, 0x200000, 0, 1, 0x200000, 0x200000},

	/*
	 * Add the region from 0x0 to (0x0 + 0x200000 - 1):
	 *   Expected result:
	 *       Successful
	 *
	 * Now the instance of struct phys_contig_mem_regions is:
	 *   num = 2
	 *   region = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *       {start=0x0,      end=0x1fffff}, // len=0x200000
	 *   }
	 */
	{0x0, 0x200000, 0, 2, 0x0, 0x200000},

	/*
	 * Add the region from 0x600000 to (0x600000 + 0x400000 - 1):
	 *   Expected result:
	 *       Successful
	 *
	 * Now the instance of struct phys_contig_mem_regions is:
	 *   num = 3
	 *   region = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *       {start=0x0,      end=0x1fffff}, // len=0x200000
	 *       {start=0x600000, end=0x9fffff}, // len=0x400000
	 *   }
	 */
	{0x600000, 0x400000, 0, 3, 0x600000, 0x400000},

	/*
	 * Add the region from 0xa00000 to (0xa00000 + 0x400000 - 1):
	 *   Expected result:
	 *       Successful, merging case!
	 *
	 * Now the instance of struct phys_contig_mem_regions is:
	 *   num = 3
	 *   region = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *       {start=0x0,      end=0x1fffff}, // len=0x200000
	 *       {start=0x600000, end=0xdfffff}, // len=0x800000
	 *   }
	 */
	{0xa00000, 0x400000, 0, 3, 0x600000, 0x800000},

	/*
	 * Add the region from 0x1000 to (0x1000 + 0x200000 - 1):
	 *   Expected result:
	 *       Failed, start address is not 2M-aligned
	 *
	 * Now the instance of struct phys_contig_mem_regions is:
	 *   num = 3
	 *   region = {
	 *       {start=0x200000, end=0x3fffff}, // len=0x200000
	 *       {start=0x0,      end=0x1fffff}, // len=0x200000
	 *       {start=0x600000, end=0xdfffff}, // len=0x800000
	 *   }
	 */
	{0x1000, 0x200000, -EINVAL, 3, 0x600000, 0x800000},
};

static void ne_misc_dev_test_merge_phys_contig_memory_regions(struct kunit *test)
{
	struct phys_contig_mem_regions *regions;
	size_t sz = 0;
	int rc = 0;
	int i = 0;

	sz = sizeof(*regions) + MAX_PHYS_REGIONS * sizeof(struct range);
	regions = kunit_kzalloc(test, sz, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, regions != NULL);

	for (i = 0; i < ARRAY_SIZE(phys_regions_test_cases); i++) {
		struct phys_regions_test *entry = phys_regions_test_cases + i;

		rc = ne_merge_phys_contig_memory_regions(regions,
							 entry->paddr, entry->size);
		KUNIT_EXPECT_EQ(test, rc, entry->expect_rc);
		KUNIT_EXPECT_EQ(test, regions->num, entry->expect_num);

		if (entry->expect_last_paddr == INVALID_VALUE)
			continue;

		KUNIT_EXPECT_EQ(test, regions->region[regions->num - 1].start,
				entry->expect_last_paddr);
		KUNIT_EXPECT_EQ(test, range_len(&regions->region[regions->num - 1]),
				entry->expect_last_size);
	}
}

static struct kunit_case ne_misc_dev_test_cases[] = {
	KUNIT_CASE(ne_misc_dev_test_merge_phys_contig_memory_regions),
	{}
};

static struct kunit_suite ne_misc_dev_test_suite = {
	.name = "ne_misc_dev_test",
	.test_cases = ne_misc_dev_test_cases,
};

static struct kunit_suite *ne_misc_dev_test_suites[] = {
	&ne_misc_dev_test_suite,
	NULL
};
