// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/kup.h>
#include <asm/smp.h>

struct static_key_false disable_kuep_key;

void setup_kuep(bool disabled)
{
	if (disabled) {
		pr_info("KUEP cannot be disabled for the time being\n");
		disabled = false;
	}

	if (!disabled)
		update_user_segments(mfsr(0) | SR_NX);

	if (smp_processor_id() != boot_cpuid)
		return;

	if (disabled)
		static_branch_enable(&disable_kuep_key);
	else
		pr_info("Activating Kernel Userspace Execution Prevention\n");
}
