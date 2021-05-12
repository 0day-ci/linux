// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/kup.h>

void kuap_lock_all_ool(void)
{
	kuap_lock_all();
}

void kuap_unlock_all_ool(void)
{
	kuap_unlock_all();
}

