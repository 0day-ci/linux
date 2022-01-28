/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Intel Corporation */

#ifndef _IECM_H_
#define _IECM_H_

#include <linux/aer.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/version.h>
#include <linux/dim.h>

#include "iecm_txrx.h"

#define IECM_BAR0			0
#define IECM_NO_FREE_SLOT		0xffff

#define IECM_MAX_NUM_VPORTS		1

/* available message levels */
#define IECM_AVAIL_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

enum iecm_state {
	__IECM_STARTUP,
	__IECM_VER_CHECK,
	__IECM_GET_CAPS,
	__IECM_GET_DFLT_VPORT_PARAMS,
	__IECM_INIT_SW,
	__IECM_DOWN,
	__IECM_UP,
	__IECM_STATE_LAST /* this member MUST be last */
};

enum iecm_flags {
	/* Soft reset causes */
	__IECM_SR_Q_CHANGE, /* Soft reset to do queue change */
	__IECM_SR_Q_DESC_CHANGE,
	__IECM_SR_Q_SCH_CHANGE, /* Scheduling mode change in queue context */
	__IECM_SR_MTU_CHANGE,
	__IECM_SR_TC_CHANGE,
	__IECM_SR_RSC_CHANGE,
	__IECM_SR_HSPLIT_CHANGE,
	/* Hard reset causes */
	__IECM_HR_FUNC_RESET, /* Hard reset when txrx timeout */
	__IECM_HR_CORE_RESET, /* when reset event is received on virtchannel */
	__IECM_HR_DRV_LOAD, /* Set on driver load for a clean HW */
	/* Reset in progress */
	__IECM_HR_RESET_IN_PROG,
	/* Resources release in progress*/
	__IECM_REL_RES_IN_PROG,
	/* Generic bits to share a message */
	__IECM_DEL_QUEUES,
	__IECM_UP_REQUESTED, /* Set if open to be called explicitly by driver */
	/* Mailbox interrupt event */
	__IECM_MB_INTR_MODE,
	__IECM_MB_INTR_TRIGGER,
	/* Stats message pending on mailbox */
	__IECM_MB_STATS_PENDING,
	/* Device specific bits */
	/* Request split queue model when creating vport */
	__IECM_REQ_TX_SPLITQ,
	__IECM_REQ_RX_SPLITQ,
	/* Asynchronous add/del ether address in flight */
	__IECM_ADD_ETH_REQ,
	__IECM_DEL_ETH_REQ,
	/* Virtchnl message buffer received needs to be processed */
	__IECM_VC_MSG_PENDING,
	/* To process software marker packets */
	__IECM_SW_MARKER,
	/* must be last */
	__IECM_FLAGS_NBITS,
};

struct iecm_reset_reg {
	u32 rstat;
	u32 rstat_m;
};

/* stub */
struct iecm_vport {
};

enum iecm_user_flags {
	__IECM_PRIV_FLAGS_HDR_SPLIT = 0,
	__IECM_PROMISC_UC = 32,
	__IECM_PROMISC_MC,
	__IECM_USER_FLAGS_NBITS,
};

/* User defined configuration values */
struct iecm_user_config_data {
	u32 num_req_tx_qs; /* user requested TX queues through ethtool */
	u32 num_req_rx_qs; /* user requested RX queues through ethtool */
	u32 num_req_txq_desc;
	u32 num_req_rxq_desc;
	u16 vlan_ethertype;
	void *req_qs_chunks;
	DECLARE_BITMAP(user_flags, __IECM_USER_FLAGS_NBITS);
	DECLARE_BITMAP(etf_qenable, IECM_LARGE_MAX_Q);
	struct list_head mac_filter_list;
	struct list_head vlan_filter_list;
	struct list_head adv_rss_list;
};

struct iecm_rss_data {
	u64 rss_hash;
	u16 rss_key_size;
	u8 *rss_key;
	u16 rss_lut_size;
	u32 *rss_lut;
};

struct iecm_adapter {
	struct pci_dev *pdev;
	const char *drv_name;
	const char *drv_ver;
	u32 virt_ver_maj;
	u32 virt_ver_min;

	u32 tx_timeout_count;
	u32 msg_enable;
	enum iecm_state state;
	DECLARE_BITMAP(flags, __IECM_FLAGS_NBITS);
	struct mutex reset_lock; /* lock to protect reset flows */
	struct iecm_reset_reg reset_reg;

	u16 num_req_msix;
	u16 num_msix_entries;
	struct msix_entry *msix_entries;
	struct virtchnl2_alloc_vectors *req_vec_chunks;

	/* vport structs */
	struct iecm_vport **vports;	/* vports created by the driver */
	struct net_device **netdevs;	/* associated vport netdevs */
	u16 num_alloc_vport;
	u16 next_vport;		/* Next free slot in pf->vport[] - 0-based! */

	struct delayed_work init_task; /* delayed init task */
	struct workqueue_struct *init_wq;
	u32 mb_wait_count;
	struct delayed_work serv_task; /* delayed service task */
	struct workqueue_struct *serv_wq;
	struct delayed_work stats_task; /* delayed statistics task */
	struct workqueue_struct *stats_wq;
	struct delayed_work vc_event_task; /* delayed virtchannel event task */
	struct workqueue_struct *vc_event_wq;
	/* Store the resources data received from control plane */
	void **vport_params_reqd;
	void **vport_params_recvd;
	/* User set parameters */
	struct iecm_user_config_data config_data;
	void *caps;
	struct virtchnl_vlan_caps *vlan_caps;

	wait_queue_head_t vchnl_wq;
	wait_queue_head_t sw_marker_wq;
	struct iecm_rss_data rss_data;
	s32 link_speed;
	/* This is only populated if the VIRTCHNL_VF_CAP_ADV_LINK_SPEED is set
	 * in vf_res->vf_cap_flags. This field should be used going forward and
	 * the enum virtchnl_link_speed above should be considered the legacy
	 * way of storing/communicating link speeds.
	 */
	u32 link_speed_mbps;
	bool link_up;
	int num_vfs;

	struct mutex sw_mutex;		/* lock to protect vport alloc flow */
	/* lock to protect cloud filters*/
	spinlock_t cloud_filter_list_lock;
	/* lock to protect mac filters */
	spinlock_t mac_filter_list_lock;
	/* lock to protect vlan filters */
	spinlock_t vlan_list_lock;
	/* lock to protect advanced RSS filters */
	spinlock_t adv_rss_list_lock;
	/* lock to protect the Flow Director filters */
	spinlock_t fdir_fltr_list_lock;
};

int iecm_probe(struct pci_dev *pdev,
	       const struct pci_device_id __always_unused *ent,
	       struct iecm_adapter *adapter);
void iecm_remove(struct pci_dev *pdev);
#endif /* !_IECM_H_ */
