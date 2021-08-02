#include <linux/sysctl.h>
#include <linux/types.h>

#ifdef CONFIG_LWTUNNEL
int nf_conntrack_lwtunnel_sysctl_handler(struct ctl_table *table, int write,
					 void *buffer, size_t *lenp,
					 loff_t *ppos);
#else // CONFIG_LWTUNNEL
int nf_conntrack_lwtunnel_sysctl_handler(struct ctl_table *table, int write,
					 void *buffer, size_t *lenp,
					 loff_t *ppos)
{
    return 0;
}
#endif