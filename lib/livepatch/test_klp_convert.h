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
extern char klp_string_a[] __asm__("klp_string.12345");
extern char klp_string_b[] __asm__("klp_string.67890");
extern int global_small;
// .rela.data.rel.ro, .rela.rodata supported ???:
extern int const_global_small;
extern int static_small;
extern int static_const_small;
extern int global_large[];
// .rela.data.rel.ro, .rela.rodata supported ???:
extern int const_global_large[];
extern int static_large[];
extern int static_const_large[];
extern int local_small;
extern int const_local_small;
extern int static_local_small;
extern int static_const_local_small;
extern int local_large[4];
extern int const_local_large[4];
extern int static_local_large[4];
extern int static_const_local_large[4];
// .rela.data..ro_after_init supported ???:
extern int static_ro_after_init;
extern int static_read_mostly;

/* klp-convert symbols - vmlinux */
extern struct static_key_false tracepoint_printk_key;

/* klp-convert symbols - test_klp_keys_mod.ko */
extern struct static_key_true test_klp_true_key;
extern struct static_key_false test_klp_false_key;


#endif
