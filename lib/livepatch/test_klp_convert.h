/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _TEST_KLP_CONVERT_
#define _TEST_KLP_CONVERT_

/* klp-convert symbols - vmlinux */
extern char *saved_command_line;
/* klp-convert symbols - test_klp_convert_mod.ko */
extern char driver_name[];
extern char homonym_string[];
extern const char *get_homonym_string(void);
extern const char *test_klp_get_driver_name(void);

#endif
