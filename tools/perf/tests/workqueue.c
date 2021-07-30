// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/err.h>
#include "tests.h"
#include "util/debug.h"
#include "util/workqueue/threadpool.h"

struct threadpool_test_args_t {
	int pool_size;
};

static int __threadpool__prepare(struct threadpool **pool, int pool_size)
{
	*pool = threadpool__new(pool_size);
	TEST_ASSERT_VAL("threadpool creation failure", !IS_ERR(*pool));
	TEST_ASSERT_VAL("threadpool size is wrong",
			threadpool__size(*pool) == pool_size);

	return TEST_OK;
}

static int __threadpool__teardown(struct threadpool *pool)
{
	threadpool__delete(pool);

	return TEST_OK;
}


static int __test__threadpool(void *_args)
{
	struct threadpool_test_args_t *args = _args;
	struct threadpool *pool;
	int ret;

	ret = __threadpool__prepare(&pool, args->pool_size);
	if (ret)
		goto out;

	ret = __threadpool__teardown(pool);
	if (ret)
		goto out;

out:
	return ret;
}

static const struct threadpool_test_args_t threadpool_test_args[] = {
	{
		.pool_size = 1
	},
	{
		.pool_size = 2
	},
	{
		.pool_size = 4
	},
	{
		.pool_size = 8
	},
	{
		.pool_size = 16
	}
};

struct test_case {
	const char *desc;
	int (*func)(void *args);
	void *args;
	int n_args;
	int arg_size;
};

static struct test_case workqueue_testcase_table[] = {
	{
		.desc = "Threadpool",
		.func = __test__threadpool,
		.args = (void *) threadpool_test_args,
		.n_args = (int)ARRAY_SIZE(threadpool_test_args),
		.arg_size = sizeof(struct threadpool_test_args_t)
	}
};


int test__workqueue(struct test *test __maybe_unused, int i)
{
	int j, ret;
	struct test_case *tc;

	if (i < 0 || i >= (int)ARRAY_SIZE(workqueue_testcase_table))
		return TEST_FAIL;

	tc = &workqueue_testcase_table[i];

	for (j = 0; j < tc->n_args; j++) {
		ret = tc->func((void *)((char *)tc->args + (j*tc->arg_size)));
		if (ret)
			return ret;
	}

	return TEST_OK;
}


int test__workqueue_subtest_get_nr(void)
{
	return (int)ARRAY_SIZE(workqueue_testcase_table);
}

const char *test__workqueue_subtest_get_desc(int i)
{
	if (i < 0 || i >= (int)ARRAY_SIZE(workqueue_testcase_table))
		return NULL;
	return workqueue_testcase_table[i].desc;
}
