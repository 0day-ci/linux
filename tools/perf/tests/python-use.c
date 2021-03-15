// SPDX-License-Identifier: GPL-2.0
/*
 * Just test if we can load the python binding.
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include "tests.h"
#include "util/debug.h"
#include <subcmd/exec-cmd.h>

int test__python_use(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	char *cmd;
	int ret;
	char *exec_path = NULL;
	char *pythonpath;

	exec_path = get_exec_abs_path();
	if (exec_path == NULL)
		return -1;

	if (asprintf(&pythonpath, "%spython", exec_path) < 0)
		return -1;

	if (asprintf(&cmd, "echo \"import sys ; sys.path.append('%s'); import perf\" | %s %s",
		     pythonpath, PYTHON, verbose > 0 ? "" : "2> /dev/null") < 0)
		return -1;

	free(exec_path);
	free(pythonpath);
	pr_debug("python usage test: \"%s\"\n", cmd);
	ret = system(cmd) ? -1 : 0;
	free(cmd);
	return ret;
}
