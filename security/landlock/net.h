// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock LSM - Network management and hooks
 *
 * Copyright (C) 2022 Huawei Tech. Co., Ltd.
 * Author: Konstantin Meskhidze <konstantin.meskhidze@huawei.com>
 *
 */

#ifndef _SECURITY_LANDLOCK_NET_H
#define _SECURITY_LANDLOCK_NET_H

#include "common.h"
#include "ruleset.h"
#include "setup.h"

__init void landlock_add_net_hooks(void);

int landlock_append_net_rule(struct landlock_ruleset *const ruleset,
			     u16 port, u32 access_hierarchy);

#endif /* _SECURITY_LANDLOCK_NET_H */
