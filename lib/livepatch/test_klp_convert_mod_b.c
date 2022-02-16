// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Joe Lawrence <joe.lawrence@redhat.com>

/*
 * A second compilation unit to provide another set of similarly named
 * symbols, forcing a livepatch to use sympos annotations.
 */

static const char homonym_string[] = "homonym string B";
__used static const char *get_homonym_string(void)
{
	return homonym_string;
}
