/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_POWER_DELIVERY__
#define __USB_POWER_DELIVERY__

#include <linux/kobject.h>

struct pd_capabilities {
	struct kobject kobj;
	unsigned int id;
	struct pd *pd;
	enum typec_role role;
	struct list_head pdos;
	struct list_head node;
};

struct pd {
	struct kobject		kobj;
	struct device		*dev;

	u16			revision; /* 0300H = "3.0" */
	u16			version;

	struct ida		source_cap_ids;
	struct ida		sink_cap_ids;
	struct list_head	source_capabilities;
	struct list_head	sink_capabilities;
};

#define to_pd_capabilities(o) container_of(o, struct pd_capabilities, kobj)
#define to_pd(o) container_of(o, struct pd, kobj)

struct pd *pd_register(struct device *dev, struct pd_desc *desc);
void pd_unregister(struct pd *pd);

int pd_init(void);
void pd_exit(void);

#endif /* __USB_POWER_DELIVERY__ */
