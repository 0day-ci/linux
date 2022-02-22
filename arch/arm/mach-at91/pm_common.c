// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/kernel.h>
#include <linux/parser.h>
#include <linux/string.h>

#include "pm.h"

const match_table_t pm_modes __initconst = {
	{ AT91_PM_STANDBY,	"standby" },
	{ AT91_PM_ULP0,		"ulp0" },
	{ AT91_PM_ULP0_FAST,    "ulp0-fast" },
	{ AT91_PM_ULP1,		"ulp1" },
	{ AT91_PM_BACKUP,	"backup" },
	{ -1, NULL },
};

int at91_pm_common_modes_select(char *str, int *standby_mode, int *suspend_mode)
{
	char *s;
	substring_t args[MAX_OPT_ARGS];
	int standby, suspend;

	if (!str)
		return 0;

	s = strsep(&str, ",");
	standby = match_token(s, pm_modes, args);
	if (standby < 0)
		return 0;

	suspend = match_token(str, pm_modes, args);
	if (suspend < 0)
		return 0;

	*standby_mode = standby;
	*suspend_mode = suspend;

	return 0;
}
