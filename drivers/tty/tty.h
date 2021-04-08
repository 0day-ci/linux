/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TTY core internal functions
 */

#ifndef _TTY_INTERNAL_H
#define _TTY_INTERNAL_H

/* tty_audit.c */
#ifdef CONFIG_AUDIT
void tty_audit_add_data(struct tty_struct *tty, const void *data, size_t size);
void tty_audit_tiocsti(struct tty_struct *tty, char ch);
#else
static inline void tty_audit_add_data(struct tty_struct *tty, const void *data,
				      size_t size)
{
}
static inline void tty_audit_tiocsti(struct tty_struct *tty, char ch)
{
}
#endif

#endif
