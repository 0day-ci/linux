// SPDX-License-Identifier: GPL-2.0
/*
 * Just test if we can load the python binding.
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include "tests.h"
#include "util/debug.h"
#include "util/util.h"
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>

int test__python_use(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	char *cmd;
	int ret = -1;
	char *exec_path;
	char buf[PATH_MAX];
	char *pythonpath;
	struct stat sb;

	perf_exe(buf, PATH_MAX);
	exec_path = dirname(buf);

	if (asprintf(&pythonpath, "%s/python", exec_path) < 0)
		return ret;

	if (stat(pythonpath, &sb) || !S_ISDIR(sb.st_mode))
		pythonpath[0] = 0;

	if (asprintf(&cmd, "echo \"import sys ; sys.path.append('%s'); import perf\" | %s %s",
		     pythonpath, PYTHON, verbose > 0 ? "" : "2> /dev/null") < 0)
		goto out;

	pr_debug("python usage test: \"%s\"\n", cmd);
	ret = system(cmd) ? -1 : 0;
	free(cmd);
out:
	free(pythonpath);
	return ret;
}
