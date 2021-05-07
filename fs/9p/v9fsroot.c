// SPDX-License-Identifier: GPL-2.0
/*
 * 9p root file system support
 *
 * Copyright (c) 2021 Changbin Du <changbin.du@gmail.com>
 */
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/root_dev.h>
#include <linux/kernel.h>

static char root_dev[2048] __initdata = "";
static char root_opts[1024] __initdata = "";

/* v9fsroot=<path>[,options] */
static int __init v9fs_root_setup(char *line)
{
	char *s;
	int len;

	if (strlen(line) >= 1) {
		/* make s point to ',' or '\0' at end of line */
		s = strchrnul(line, ',');
		/* len is strlen(unc) + '\0' */
		len = s - line + 1;
		if (len > sizeof(root_dev)) {
			pr_err("Root-V9FS: path too long\n");
			return 1;
		}
		strscpy(root_dev, line, len);

		if (*s) {
			int n = snprintf(root_opts,
					 sizeof(root_opts), "%s",
					 s + 1);
			if (n >= sizeof(root_opts)) {
				pr_err("Root-V9FS: mount options string too long\n");
				root_opts[sizeof(root_opts)-1] = '\0';
				return 1;
			}
		}
	}

	ROOT_DEV = Root_V9FS;
	return 1;
}

__setup("v9fsroot=", v9fs_root_setup);

int __init v9fs_root_data(char **dev, char **opts)
{
	if (!root_dev[0]) {
		pr_err("Root-V9FS: no rootdev specified\n");
		return -1;
	}

	*dev = root_dev;
	*opts = root_opts;

	return 0;
}
