/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Dell privacy notification driver
 *
 * Copyright (C) 2021 Dell Inc. All Rights Reserved.
 */

#ifndef _DELL_PRIVACY_WMI_H_
#define _DELL_PRIVACY_WMI_H_

#if IS_ENABLED(CONFIG_DELL_PRIVACY)
bool dell_privacy_present(void);
bool dell_privacy_process_event(int type, int code, int status);
#else /* CONFIG_DELL_PRIVACY */
static inline bool dell_privacy_present(void)
{
	return -ENODEV;
}

static inline bool dell_privacy_process_event(int type, int code, int status)
{
	return false;
}
#endif /* CONFIG_DELL_PRIVACY */
#endif
