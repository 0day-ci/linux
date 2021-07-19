// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for the min max heap.
 */

#include <kunit/test.h>

#include <linux/log2.h>
#include <linux/min_heap.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>

static bool less_than(const void *lhs, const void *rhs)
{
	return *(int *)lhs < *(int *)rhs;
}

static bool greater_than(const void *lhs, const void *rhs)
{
	return *(int *)lhs > *(int *)rhs;
}

static void swap_ints(void *lhs, void *rhs)
{
	int temp = *(int *)lhs;

	*(int *)lhs = *(int *)rhs;
	*(int *)rhs = temp;
}

static void pop_verify_heap(struct kunit *test, bool min_heap,
			    struct min_heap *heap,
			    const struct min_heap_callbacks *funcs)
{
	int *values = heap->data;
	int last;

	last = values[0];
	min_heap_pop(heap, funcs);
	while (heap->nr > 0) {
		if (min_heap) {
			KUNIT_EXPECT_LE(test, last, values[0]);
		} else {
			KUNIT_EXPECT_GE(test, last, values[0]);
		}
		last = values[0];
		min_heap_pop(heap, funcs);
	}
}

static void test_heapify_all(struct kunit *test, bool min_heap)
{
	int values[] = { 3, 1, 2, 4, 0x8000000, 0x7FFFFFF, 0,
			 -3, -1, -2, -4, 0x8000000, 0x7FFFFFF };
	struct min_heap heap = {
		.data = values,
		.nr = ARRAY_SIZE(values),
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.elem_size = sizeof(int),
		.less = min_heap ? less_than : greater_than,
		.swp = swap_ints,
	};
	int i;

	/* Test with known set of values. */
	min_heapify_all(&heap, &funcs);
	pop_verify_heap(test, min_heap, &heap, &funcs);


	/* Test with randomly generated values. */
	heap.nr = ARRAY_SIZE(values);
	for (i = 0; i < heap.nr; i++)
		values[i] = get_random_int();

	min_heapify_all(&heap, &funcs);
	pop_verify_heap(test, min_heap, &heap, &funcs);
}

static void test_heap_push(struct kunit *test, bool min_heap)
{
	const int data[] = { 3, 1, 2, 4, 0x80000000, 0x7FFFFFFF, 0,
			     -3, -1, -2, -4, 0x80000000, 0x7FFFFFFF };
	int values[ARRAY_SIZE(data)];
	struct min_heap heap = {
		.data = values,
		.nr = 0,
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.elem_size = sizeof(int),
		.less = min_heap ? less_than : greater_than,
		.swp = swap_ints,
	};
	int i, temp;

	/* Test with known set of values copied from data. */
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push(&heap, &data[i], &funcs);

	pop_verify_heap(test, min_heap, &heap, &funcs);

	/* Test with randomly generated values. */
	while (heap.nr < heap.size) {
		temp = get_random_int();
		min_heap_push(&heap, &temp, &funcs);
	}
	pop_verify_heap(test, min_heap, &heap, &funcs);
}

static void test_heap_pop_push(struct kunit *test, bool min_heap)
{
	const int data[] = { 3, 1, 2, 4, 0x80000000, 0x7FFFFFFF, 0,
			     -3, -1, -2, -4, 0x80000000, 0x7FFFFFFF };
	int values[ARRAY_SIZE(data)];
	struct min_heap heap = {
		.data = values,
		.nr = 0,
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.elem_size = sizeof(int),
		.less = min_heap ? less_than : greater_than,
		.swp = swap_ints,
	};
	int i, temp;

	/* Fill values with data to pop and replace. */
	temp = min_heap ? 0x80000000 : 0x7FFFFFFF;
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push(&heap, &temp, &funcs);

	/* Test with known set of values copied from data. */
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_pop_push(&heap, &data[i], &funcs);

	pop_verify_heap(test, min_heap, &heap, &funcs);

	heap.nr = 0;
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push(&heap, &temp, &funcs);

	/* Test with randomly generated values. */
	for (i = 0; i < ARRAY_SIZE(data); i++) {
		temp = get_random_int();
		min_heap_pop_push(&heap, &temp, &funcs);
	}
	pop_verify_heap(test, min_heap, &heap, &funcs);
}

static void test_heap(struct kunit *test, bool min_heap)
{
	test_heapify_all(test, min_heap);
	test_heap_push(test, min_heap);
	test_heap_pop_push(test, min_heap);
}

static void test_min_heap(struct kunit *test)
{
	test_heap(test, true);
}

static void test_max_heap(struct kunit *test)
{
	test_heap(test, false);
}

static struct kunit_case __refdata minmax_heap_test_cases[] = {
	KUNIT_CASE(test_min_heap),
	KUNIT_CASE(test_max_heap),
	{}
};

static struct kunit_suite minmax_heap_test_suite = {
	.name = "lib_minmax_heap",
	.test_cases = minmax_heap_test_cases,
};

kunit_test_suites(&minmax_heap_test_suite);


MODULE_LICENSE("GPL");
