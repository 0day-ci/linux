// SPDX-License-Identifier: GPL-2.0
#include <linux/memory.h>
#include <linux/static_call.h>

#include <asm/code-patching.h>

static int patch_trampoline_32(u32 *addr, unsigned long target)
{
	int err;

	err = patch_instruction(addr++, ppc_inst(PPC_RAW_LIS(_R12, PPC_HA(target))));
	err |= patch_instruction(addr++, ppc_inst(PPC_RAW_ADDI(_R12, _R12, PPC_LO(target))));
	err |= patch_instruction(addr++, ppc_inst(PPC_RAW_MTCTR(_R12)));
	err |= patch_instruction(addr, ppc_inst(PPC_RAW_BCTR()));

	return err;
}

void arch_static_call_transform(void *site, void *tramp, void *func, bool tail)
{
	int err;
	unsigned long target = (long)func;

	if (!tramp)
		return;

	mutex_lock(&text_mutex);

	if (!func)
		err = patch_instruction(tramp, ppc_inst(PPC_RAW_BLR()));
	else if (is_offset_in_branch_range((long)target - (long)tramp))
		err = patch_branch(tramp, target, 0);
	else if (IS_ENABLED(CONFIG_PPC32))
		err = patch_trampoline_32(tramp, target);
	else
		BUILD_BUG();

	mutex_unlock(&text_mutex);

	if (err)
		panic("%s: patching failed %pS at %pS\n", __func__, func, tramp);
}
EXPORT_SYMBOL_GPL(arch_static_call_transform);
