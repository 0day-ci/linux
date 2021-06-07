/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * net/dsa/dsa_priv.h - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 */

#ifndef __DSA_PRIV_H
#define __DSA_PRIV_H

#include <linux/if_bridge.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/netpoll.h>
#include <net/dsa.h>
#include <net/gro_cells.h>

enum {
	DSA_NOTIFIER_AGEING_TIME,
	DSA_NOTIFIER_BRIDGE_JOIN,
	DSA_NOTIFIER_BRIDGE_LEAVE,
	DSA_NOTIFIER_FDB_ADD,
	DSA_NOTIFIER_FDB_DEL,
	DSA_NOTIFIER_HSR_JOIN,
	DSA_NOTIFIER_HSR_LEAVE,
	DSA_NOTIFIER_LAG_CHANGE,
	DSA_NOTIFIER_LAG_JOIN,
	DSA_NOTIFIER_LAG_LEAVE,
	DSA_NOTIFIER_MDB_ADD,
	DSA_NOTIFIER_MDB_DEL,
	DSA_NOTIFIER_VLAN_ADD,
	DSA_NOTIFIER_VLAN_DEL,
	DSA_NOTIFIER_MTU,
	DSA_NOTIFIER_TAG_PROTO,
	DSA_NOTIFIER_MRP_ADD,
	DSA_NOTIFIER_MRP_DEL,
	DSA_NOTIFIER_MRP_ADD_RING_ROLE,
	DSA_NOTIFIER_MRP_DEL_RING_ROLE,
};

/* DSA_NOTIFIER_AGEING_TIME */
struct dsa_notifier_ageing_time_info {
	unsigned int ageing_time;
};

/* DSA_NOTIFIER_BRIDGE_* */
struct dsa_notifier_bridge_info {
	struct net_device *br;
	int tree_index;
	int sw_index;
	int port;
};

/* DSA_NOTIFIER_FDB_* */
struct dsa_notifier_fdb_info {
	int sw_index;
	int port;
	const unsigned char *addr;
	u16 vid;
};

/* DSA_NOTIFIER_MDB_* */
struct dsa_notifier_mdb_info {
	const struct switchdev_obj_port_mdb *mdb;
	int sw_index;
	int port;
};

/* DSA_NOTIFIER_LAG_* */
struct dsa_notifier_lag_info {
	struct net_device *lag;
	int sw_index;
	int port;

	struct netdev_lag_upper_info *info;
};

/* DSA_NOTIFIER_VLAN_* */
struct dsa_notifier_vlan_info {
	const struct switchdev_obj_port_vlan *vlan;
	int sw_index;
	int port;
	struct netlink_ext_ack *extack;
};

/* DSA_NOTIFIER_MTU */
struct dsa_notifier_mtu_info {
	bool propagate_upstream;
	int sw_index;
	int port;
	int mtu;
};

/* DSA_NOTIFIER_TAG_PROTO_* */
struct dsa_notifier_tag_proto_info {
	const struct dsa_device_ops *tag_ops;
};

/* DSA_NOTIFIER_MRP_* */
struct dsa_notifier_mrp_info {
	const struct switchdev_obj_mrp *mrp;
	int sw_index;
	int port;
};

/* DSA_NOTIFIER_MRP_* */
struct dsa_notifier_mrp_ring_role_info {
	const struct switchdev_obj_ring_role_mrp *mrp;
	int sw_index;
	int port;
};

struct dsa_switchdev_event_work {
	struct dsa_switch *ds;
	int port;
	struct work_struct work;
	unsigned long event;
	/* Specific for SWITCHDEV_FDB_ADD_TO_DEVICE and
	 * SWITCHDEV_FDB_DEL_TO_DEVICE
	 */
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

/* DSA_NOTIFIER_HSR_* */
struct dsa_notifier_hsr_info {
	struct net_device *hsr;
	int sw_index;
	int port;
};

struct dsa_slave_priv {
	/* Copy of CPU port xmit for faster access in slave transmit hot path */
	struct sk_buff *	(*xmit)(struct sk_buff *skb,
					struct net_device *dev);

	struct gro_cells	gcells;

	/* DSA port data, such as switch, port index, etc. */
	struct dsa_port		*dp;

#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll		*netpoll;
#endif

	/* TC context */
	struct list_head	mall_tc_list;
};

/* dsa.c */
const struct dsa_device_ops *dsa_tag_driver_get(int tag_protocol);
void dsa_tag_driver_put(const struct dsa_device_ops *ops);
const struct dsa_device_ops *dsa_find_tagger_by_name(const char *buf);

bool dsa_schedule_work(struct work_struct *work);
const char *dsa_tag_protocol_to_str(const struct dsa_device_ops *ops);

/* master.c */
int dsa_master_setup(struct net_device *dev, struct dsa_port *cpu_dp);
void dsa_master_teardown(struct net_device *dev);

static inline struct net_device *dsa_master_find_slave(struct net_device *dev,
						       int device, int port)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	struct dsa_switch_tree *dst = cpu_dp->dst;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dp->ds->index == device && dp->index == port &&
		    dp->type == DSA_PORT_TYPE_USER)
			return dp->slave;

	return NULL;
}

/* port.c */
void dsa_port_set_tag_protocol(struct dsa_port *cpu_dp,
			       const struct dsa_device_ops *tag_ops);
int dsa_port_set_state(struct dsa_port *dp, u8 state);
int dsa_port_enable_rt(struct dsa_port *dp, struct phy_device *phy);
int dsa_port_enable(struct dsa_port *dp, struct phy_device *phy);
void dsa_port_disable_rt(struct dsa_port *dp);
void dsa_port_disable(struct dsa_port *dp);
int dsa_port_bridge_join(struct dsa_port *dp, struct net_device *br,
			 struct netlink_ext_ack *extack);
void dsa_port_bridge_leave(struct dsa_port *dp, struct net_device *br);
int dsa_port_lag_change(struct dsa_port *dp,
			struct netdev_lag_lower_state_info *linfo);
int dsa_port_lag_join(struct dsa_port *dp, struct net_device *lag_dev,
		      struct netdev_lag_upper_info *uinfo,
		      struct netlink_ext_ack *extack);
void dsa_port_lag_leave(struct dsa_port *dp, struct net_device *lag_dev);
int dsa_port_vlan_filtering(struct dsa_port *dp, bool vlan_filtering,
			    struct netlink_ext_ack *extack);
bool dsa_port_skip_vlan_configuration(struct dsa_port *dp);
int dsa_port_ageing_time(struct dsa_port *dp, clock_t ageing_clock);
int dsa_port_mtu_change(struct dsa_port *dp, int new_mtu,
			bool propagate_upstream);
int dsa_port_fdb_add(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid);
int dsa_port_fdb_del(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid);
int dsa_port_fdb_dump(struct dsa_port *dp, dsa_fdb_dump_cb_t *cb, void *data);
int dsa_port_mdb_add(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_mdb_del(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_pre_bridge_flags(const struct dsa_port *dp,
			      struct switchdev_brport_flags flags,
			      struct netlink_ext_ack *extack);
int dsa_port_bridge_flags(const struct dsa_port *dp,
			  struct switchdev_brport_flags flags,
			  struct netlink_ext_ack *extack);
int dsa_port_mrouter(struct dsa_port *dp, bool mrouter,
		     struct netlink_ext_ack *extack);
int dsa_port_vlan_add(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan,
		      struct netlink_ext_ack *extack);
int dsa_port_vlan_del(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan);
int dsa_port_mrp_add(const struct dsa_port *dp,
		     const struct switchdev_obj_mrp *mrp);
int dsa_port_mrp_del(const struct dsa_port *dp,
		     const struct switchdev_obj_mrp *mrp);
int dsa_port_mrp_add_ring_role(const struct dsa_port *dp,
			       const struct switchdev_obj_ring_role_mrp *mrp);
int dsa_port_mrp_del_ring_role(const struct dsa_port *dp,
			       const struct switchdev_obj_ring_role_mrp *mrp);
int dsa_port_link_register_of(struct dsa_port *dp);
void dsa_port_link_unregister_of(struct dsa_port *dp);
int dsa_port_hsr_join(struct dsa_port *dp, struct net_device *hsr);
void dsa_port_hsr_leave(struct dsa_port *dp, struct net_device *hsr);
extern const struct phylink_mac_ops dsa_port_phylink_mac_ops;

static inline bool dsa_port_offloads_bridge_port(struct dsa_port *dp,
						 struct net_device *dev)
{
	return dsa_port_to_bridge_port(dp) == dev;
}

static inline bool dsa_port_offloads_bridge(struct dsa_port *dp,
					    struct net_device *bridge_dev)
{
	/* DSA ports connected to a bridge, and event was emitted
	 * for the bridge.
	 */
	return dp->bridge_dev == bridge_dev;
}

/* Returns true if any port of this tree offloads the given net_device */
static inline bool dsa_tree_offloads_bridge_port(struct dsa_switch_tree *dst,
						 struct net_device *dev)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_offloads_bridge_port(dp, dev))
			return true;

	return false;
}

/* slave.c */
extern const struct dsa_device_ops notag_netdev_ops;
extern struct notifier_block dsa_slave_switchdev_notifier;
extern struct notifier_block dsa_slave_switchdev_blocking_notifier;

void dsa_slave_mii_bus_init(struct dsa_switch *ds);
int dsa_slave_create(struct dsa_port *dp);
void dsa_slave_destroy(struct net_device *slave_dev);
int dsa_slave_suspend(struct net_device *slave_dev);
int dsa_slave_resume(struct net_device *slave_dev);
int dsa_slave_register_notifier(void);
void dsa_slave_unregister_notifier(void);
void dsa_slave_setup_tagger(struct net_device *slave);
int dsa_slave_change_mtu(struct net_device *dev, int new_mtu);

static inline struct dsa_port *dsa_slave_to_port(const struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	return p->dp;
}

static inline struct net_device *
dsa_slave_to_master(const struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return dp->cpu_dp->master;
}

/* switch.c */
int dsa_switch_register_notifier(struct dsa_switch *ds);
void dsa_switch_unregister_notifier(struct dsa_switch *ds);

/* dsa2.c */
void dsa_lag_map(struct dsa_switch_tree *dst, struct net_device *lag);
void dsa_lag_unmap(struct dsa_switch_tree *dst, struct net_device *lag);
int dsa_tree_notify(struct dsa_switch_tree *dst, unsigned long e, void *v);
int dsa_broadcast(unsigned long e, void *v);
int dsa_tree_change_tag_proto(struct dsa_switch_tree *dst,
			      struct net_device *master,
			      const struct dsa_device_ops *tag_ops,
			      const struct dsa_device_ops *old_tag_ops);

extern struct list_head dsa_tree_list;

#endif
