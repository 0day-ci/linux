/*
 * include/linux/wakeup_reason.h
 *
 * Logs the reason which caused the kernel to resume
 * from the suspend mode.
 *
 * Copyright (C) 2021 Linaro, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_WAKEUP_REASON_H
#define _LINUX_WAKEUP_REASON_H

#define MAX_WAKEUP_REASON_STR_LEN 256

#ifdef CONFIG_SUSPEND
ssize_t log_ws_wakeup_reason(void);
ssize_t log_irq_wakeup_reason(unsigned int irq_number);
void clear_wakeup_reason(void);
ssize_t last_wakeup_reason_get(char *buf, ssize_t max);
#else
ssize_t log_ws_wakeup_reason(void) { }
ssize_t log_irq_wakeup_reason(unsigned int irq_number) { }
void clear_wakeup_reason(void) { }
ssize_t last_wakeup_reason_get(char *buf, ssize_t max) { }
#endif

#endif /* _LINUX_WAKEUP_REASON_H */
