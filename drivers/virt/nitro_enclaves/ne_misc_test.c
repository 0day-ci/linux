// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>

#define MAX_PHYS_REGIONS	16

struct phys_regions_test {
	u64 paddr;
	u64 size;
	int expect_rc;
	unsigned long expect_num;
	u64 expect_last_paddr;
	u64 expect_last_size;
} phys_regions_testcases[] = {
	{0x1000, 0x200000, -EINVAL, 0, ~0, ~0},
	{0x200000, 0x1000, -EINVAL, 0, ~0, ~0},
	{0x200000, 0x200000, 0, 1, 0x200000, 0x200000},
	{0x0, 0x200000, 0, 2, 0x0, 0x200000},
	{0x600000, 0x400000, 0, 3, 0x600000, 0x400000},
	{0xa00000, 0x400000, 0, 3, 0x600000, 0x800000},
	{0x1000, 0x200000, -EINVAL, 3, 0x600000, 0x800000},
};

static void ne_misc_test_set_phys_region(struct kunit *test)
{
	struct phys_contig_mem_region *regions;
	size_t sz;
	int i, rc;

	sz = sizeof(*regions) + MAX_PHYS_REGIONS * sizeof(struct phys_mem_region);
	regions = kunit_kzalloc(test, sz, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, regions != NULL);

	for (i = 0; i < ARRAY_SIZE(phys_regions_testcases); i++) {
		rc = ne_add_phys_memory_region(regions, phys_regions_testcases[i].paddr,
					       phys_regions_testcases[i].size);
		KUNIT_EXPECT_EQ(test, rc, phys_regions_testcases[i].expect_rc);
		KUNIT_EXPECT_EQ(test, regions->num, phys_regions_testcases[i].expect_num);

		if (phys_regions_testcases[i].expect_last_paddr == ~0ul)
			continue;

		KUNIT_EXPECT_EQ(test, regions->region[regions->num - 1].paddr,
				phys_regions_testcases[i].expect_last_paddr);
		KUNIT_EXPECT_EQ(test, regions->region[regions->num - 1].size,
				phys_regions_testcases[i].expect_last_size);
	}
}

static struct kunit_case ne_misc_test_cases[] = {
	KUNIT_CASE(ne_misc_test_set_phys_region),
	{}
};

static struct kunit_suite ne_misc_test_suite = {
	.name = "ne_misc_test",
	.test_cases = ne_misc_test_cases,
};

static struct kunit_suite *ne_misc_test_suites[] = {
	&ne_misc_test_suite,
	NULL
};
