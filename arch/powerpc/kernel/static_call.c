// SPDX-License-Identifier: GPL-2.0
#include <linux/memory.h>
#include <linux/static_call.h>

#include <asm/code-patching.h>

void arch_static_call_transform(void *site, void *tramp, void *func, bool tail)
{
	int err;
	unsigned long target = (long)func;
	bool is_short = is_offset_in_branch_range((long)target - (long)tramp);

	if (!tramp)
		return;

	mutex_lock(&text_mutex);

	if (func && !is_short) {
		err = patch_instruction(tramp + 20, ppc_inst(target));
		if (err)
			goto out;
	}

	if (!func)
		err = patch_instruction(tramp, ppc_inst(PPC_RAW_BLR()));
	else if (is_short)
		err = patch_branch(tramp, target, 0);
	else
		err = patch_instruction(tramp, ppc_inst(PPC_RAW_NOP()));
out:
	mutex_unlock(&text_mutex);

	if (err)
		panic("%s: patching failed %pS at %pS\n", __func__, func, tramp);
}
EXPORT_SYMBOL_GPL(arch_static_call_transform);
