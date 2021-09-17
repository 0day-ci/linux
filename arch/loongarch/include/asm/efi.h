/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_LOONGARCH_EFI_H
#define _ASM_LOONGARCH_EFI_H

#include <linux/efi.h>

extern void __init efi_init(void);
extern void __init efi_runtime_init(void);
extern void efifb_setup_from_dmi(struct screen_info *si, const char *opt);

#define ARCH_EFI_IRQ_FLAGS_MASK  0x00000001  /*bit0: CP0 Status.IE*/

#define arch_efi_call_virt_setup()               \
({                                               \
})

#define arch_efi_call_virt(p, f, args...)        \
({                                               \
	efi_##f##_t * __f;                       \
	__f = p->f;                              \
	__f(args);                               \
})

#define arch_efi_call_virt_teardown()            \
({                                               \
})


#endif /* _ASM_LOONGARCH_EFI_H */
