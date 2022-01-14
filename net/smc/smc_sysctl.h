/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SMC_SYSCTL_H
#define _SMC_SYSCTL_H

#ifdef CONFIG_SYSCTL

int smc_sysctl_init(void);
void smc_sysctl_exit(void);

#else

int smc_sysctl_init(void)
{
	return 0;
}

void smc_sysctl_exit(void) { }

#endif /* CONFIG_SYSCTL */

#endif /* _SMC_SYSCTL_H */
