/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2016 - 2021 Intel Corporation */
#ifndef IRDMA_UDA_H
#define IRDMA_UDA_H

extern const struct irdma_uda_ops irdma_uda_ops;

#define IRDMA_UDA_MAX_FSI_MGS	4096
#define IRDMA_UDA_MAX_PFS	16
#define IRDMA_UDA_MAX_VFS	128

struct irdma_sc_cqp;

struct irdma_ah_info {
	struct irdma_sc_vsi *vsi;
	u32 pd_idx;
	u32 dst_arpindex;
	u32 dest_ip_addr[4];
	u32 src_ip_addr[4];
	u32 flow_label;
	u32 ah_idx;
	u16 vlan_tag;
	u8 insert_vlan_tag;
	u8 tc_tos;
	u8 hop_ttl;
	u8 mac_addr[ETH_ALEN];
	bool ah_valid:1;
	bool ipv4_valid:1;
	bool do_lpbk:1;
};

struct irdma_sc_ah {
	struct irdma_sc_dev *dev;
	struct irdma_ah_info ah_info;
};

struct irdma_uda_ops {
	void (*init_ah)(struct irdma_sc_dev *dev, struct irdma_sc_ah *ah);
	enum irdma_status_code (*create_ah)(struct irdma_sc_cqp *cqp,
					    struct irdma_ah_info *info,
					    u64 scratch);
	enum irdma_status_code (*modify_ah)(struct irdma_sc_cqp *cqp,
					    struct irdma_ah_info *info,
					    u64 scratch);
	enum irdma_status_code (*destroy_ah)(struct irdma_sc_cqp *cqp,
					     struct irdma_ah_info *info,
					     u64 scratch);
	/* multicast */
	enum irdma_status_code (*mcast_grp_create)(struct irdma_sc_cqp *cqp,
						   struct irdma_mcast_grp_info *info,
						   u64 scratch);
	enum irdma_status_code (*mcast_grp_modify)(struct irdma_sc_cqp *cqp,
						   struct irdma_mcast_grp_info *info,
						   u64 scratch);
	enum irdma_status_code (*mcast_grp_destroy)(struct irdma_sc_cqp *cqp,
						    struct irdma_mcast_grp_info *info,
						    u64 scratch);
	enum irdma_status_code (*mcast_grp_add)(struct irdma_mcast_grp_info *ctx,
						struct irdma_mcast_grp_ctx_entry_info *mg);
	enum irdma_status_code (*mcast_grp_del)(struct irdma_mcast_grp_info *ctx,
						struct irdma_mcast_grp_ctx_entry_info *mg);
};
#endif /* IRDMA_UDA_H */
