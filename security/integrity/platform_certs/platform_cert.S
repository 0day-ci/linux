/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/export.h>
#include <linux/init.h>

	__INITRODATA

	.align 8
#ifdef CONFIG_INTEGRITY_PLATFORM_KEYRING
	.globl platform_certificate_list
platform_certificate_list:
__cert_list_start:
	.incbin "security/integrity/platform_certs/platform_certificate_list"
__cert_list_end:
#endif

	.align 8
	.globl platform_certificate_list_size
platform_certificate_list_size:
#ifdef CONFIG_64BIT
	.quad __cert_list_end - __cert_list_start
#else
	.long __cert_list_end - __cert_list_start
#endif
