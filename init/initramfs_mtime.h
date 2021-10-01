/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CONFIG_INITRAMFS_PRESERVE_MTIME
long do_utime(char *filename, time64_t mtime) __init;
void dir_add(const char *name, time64_t mtime) __init;
void dir_utime(void) __init;
#else
static long __init do_utime(char *filename, time64_t mtime) { return 0; }
static void __init dir_add(const char *name, time64_t mtime) {}
static void __init dir_utime(void) {}
#endif
