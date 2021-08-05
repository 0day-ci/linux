/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_GENERIC_HYPERV_TLFS_USER_H
#define _UAPI_ASM_GENERIC_HYPERV_TLFS_USER_H

#ifndef BIT
#define BIT(X)	(1ULL << (X))
#endif

/* Userspace-visible partition creation flags */
#define HV_PARTITION_CREATION_FLAG_SMT_ENABLED_GUEST                BIT(0)
#define HV_PARTITION_CREATION_FLAG_GPA_LARGE_PAGES_DISABLED         BIT(3)
#define HV_PARTITION_CREATION_FLAG_GPA_SUPER_PAGES_ENABLED          BIT(4)
#define HV_PARTITION_CREATION_FLAG_LAPIC_ENABLED                    BIT(13)

#endif
