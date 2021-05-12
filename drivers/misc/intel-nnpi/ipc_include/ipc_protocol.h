/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _IPC_PROTOCOL_H
#define _IPC_PROTOCOL_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>

#define IPC_OP_MAX          BIT(6)
#define NNP_IPC_OPCODE_MASK GENMASK(5, 0)
#define NNP_IPC_MIN_USER_OP  32
#define NNP_IPC_MAX_USER_OP  63
#define NNP_IPC_NUM_USER_OPS (NNP_IPC_MAX_USER_OP - NNP_IPC_MIN_USER_OP + 1)

#define NNP_MSG_SIZE(msg) (sizeof(msg) / sizeof(__le64))

/*
 * NNP_PAGE_SIZE is a size of a page in the protocol.
 * We do not use just PAGE_SIZE since it may differ between
 * card and host.
 */
#define NNP_PAGE_SHIFT       12
#define NNP_PAGE_SIZE        BIT(12)

#define NNP_VERSION_DOT_MASK   GENMASK(4, 0)
#define NNP_VERSION_MINOR_MASK GENMASK(9, 5)
#define NNP_VERSION_MAJOR_MASK GENMASK(14, 10)
#define NNP_VERSION_MAJOR(ver) FIELD_GET(NNP_VERSION_MAJOR_MASK, (ver))
#define NNP_VERSION_MINOR(ver) FIELD_GET(NNP_VERSION_MINOR_MASK, (ver))
#define NNP_VERSION_DOT(ver)   FIELD_GET(NNP_VERSION_DOT_MASK, (ver))
#define NNP_MAKE_VERSION(major, minor, dot) \
	(FIELD_PREP(NNP_VERSION_MAJOR_MASK, (major)) | \
	 FIELD_PREP(NNP_VERSION_MINOR_MASK, (minor)) | \
	 FIELD_PREP(NNP_VERSION_DOT_MASK, (dot)))

#define NNP_IPC_PROTOCOL_VERSION NNP_MAKE_VERSION(4, 1, 0)

#define NNP_IPC_DMA_PFN_BITS         45 /* size of physical address in protocol */
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
#define NNP_IPC_DMA_PFN_MASK         GENMASK_ULL(56, 12)
#define NNP_IPC_DMA_MAX_ADDR         GENMASK_ULL(56, 0)
#else
#define NNP_IPC_DMA_PFN_MASK         GENMASK_ULL(31, 12)
#define NNP_IPC_DMA_MAX_ADDR         GENMASK_ULL(31, 0)
#endif
#define NNP_IPC_DMA_ADDR_TO_PFN(dma_addr) \
				     FIELD_GET(NNP_IPC_DMA_PFN_MASK, (dma_addr))
#define NNP_IPC_DMA_PFN_TO_ADDR(dma_pfn)  \
				     FIELD_PREP(NNP_IPC_DMA_PFN_MASK, (dma_pfn))

#define NNP_IPC_INF_CHANNEL_BITS 8
#define NNP_IPC_CHANNEL_BITS     10
#define NNP_IPC_MAX_CHANNEL_RB   2

/*
 * Structures used inside data packets transferred in the protocol
 */
struct nnp_dma_chain_header {
	__le64 dma_next;
	__le32 total_nents;
	__le32 start_offset;
	__le64 size;
} __packed;

#define DMA_CHAIN_ENTRY_SIZE  sizeof(__le64)
#define DMA_CHAIN_HEADER_SIZE  sizeof(struct nnp_dma_chain_header)
#define DMA_CHAIN_ENTRY_NPAGES_BITS \
	(sizeof(__le64) * __CHAR_BIT__ - NNP_IPC_DMA_PFN_BITS)
#define NNP_MAX_CHUNK_SIZE \
	(((1lu << DMA_CHAIN_ENTRY_NPAGES_BITS) - 1) << NNP_PAGE_SHIFT)

struct nnp_dma_chain_entry {
	u64 dma_chunk_pfn  : NNP_IPC_DMA_PFN_BITS;
	u64 n_pages        : DMA_CHAIN_ENTRY_NPAGES_BITS;
} __packed;

#define DMA_CHAIN_ENTRY_PFN_MASK    GENMASK_ULL(NNP_IPC_DMA_PFN_BITS - 1, 0)
#define DMA_CHAIN_ENTRY_NPAGES_MASK GENMASK_ULL(63, NNP_IPC_DMA_PFN_BITS)

#define NENTS_PER_PAGE \
	((NNP_PAGE_SIZE - DMA_CHAIN_HEADER_SIZE) / DMA_CHAIN_ENTRY_SIZE)

/*
 * IPC messages layout definition
 */

#define NNP_C2H_OP_MASK       GENMASK_ULL(5, 0)

/* NNP_IPC_C2H_OP_QUERY_VERSION_REPLY3 - 3 qwords */
/* qword 0: */
#define NNP_C2H_VERSION_REPLY_QW0_OP_MASK       NNP_C2H_OP_MASK
#define NNP_C2H_VERSION_REPLY_QW0_PROT_VER_MASK GENMASK_ULL(21, 6)
#define NNP_C2H_VERSION_REPLY_QW0_FW_VER_MASK   GENMASK_ULL(37, 22)
#define NNP_C2H_VERSION_REPLY_QW0_CHAN_VER_MASK GENMASK_ULL(53, 38)
/* qword 1: two bits for each possible response opcode specifying its size */
/* qword 2: two bits for each possible command opcode specifying its size */

/* NNP_IPC_C2H_OP_EVENT_REPORT  - 1 qword */
#define NNP_C2H_EVENT_REPORT_OP_MASK         NNP_C2H_OP_MASK
#define NNP_C2H_EVENT_REPORT_CODE_MASK       GENMASK_ULL(12, 6)
#define NNP_C2H_EVENT_REPORT_CHAN_ID_MASK    GENMASK_ULL(20, 13)
#define NNP_C2H_EVENT_REPORT_OBJ_ID_MASK     GENMASK_ULL(36, 21)
#define NNP_C2H_EVENT_REPORT_OBJ_ID2_MASK    GENMASK_ULL(52, 37)
#define NNP_C2H_EVENT_REPORT_VAL_MASK        GENMASK_ULL(60, 53)
#define NNP_C2H_EVENT_REPORT_CHAN_VALID_MASK BIT_ULL(61)
#define NNP_C2H_EVENT_REPORT_OBJ_VALID_MASK  BIT_ULL(62)
#define NNP_C2H_EVENT_REPORT_OBJ2_VALID_MASK BIT_ULL(63)

/* NNP_IPC_C2H_OP_SYS_INFO - 1 qword */
#define NNP_C2H_SYS_INFO_OP_MASK  NNP_C2H_OP_MASK

#define NNP_H2C_OP_MASK       GENMASK_ULL(5, 0)

/* NNP_IPC_H2C_OP_QUERY_VERSION - 1 qword */
#define NNP_H2C_QUERY_VERSION_OP_MASK  NNP_H2C_OP_MASK

/* NNP_IPC_H2C_OP_SETUP_CRASH_DUMP - 2 qwords */
/* qword 0: */
#define NNP_H2C_SETUP_CRASH_DUMP_QW0_OP_MASK       NNP_H2C_OP_MASK
#define NNP_H2C_SETUP_CRASH_DUMP_QW0_DMA_ADDR_MASK GENMASK_ULL(63, 19)
/* qword 1: physical address of BAR2 */

/* NNP_IPC_H2C_OP_SETUP_SYS_INFO_PAGE - 1 qword */
#define NNP_H2C_SETUP_SYS_INFO_OP_MASK       NNP_H2C_OP_MASK
#define NNP_H2C_SETUP_SYS_INFO_NPAGES        GENMASK_ULL(15, 6)
#define NNP_H2C_SETUP_SYS_INFO_DMA_ADDR_MASK GENMASK_ULL(63, 19)

/* NNP_IPC_H2C_OP_CHANNEL_OP - 1 qword */
#define NNP_H2C_CHANNEL_OP_OP_MASK      NNP_H2C_OP_MASK
#define NNP_H2C_CHANNEL_OP_CHAN_ID_MASK GENMASK_ULL(15, 6)
#define NNP_H2C_CHANNEL_OP_DESTROY_MASK BIT_ULL(16)
#define NNP_H2C_CHANNEL_OP_PRIV_MASK    BIT_ULL(31)
#define NNP_H2C_CHANNEL_OP_UID_MASK     GENMASK_ULL(63, 32)

/* NNP_IPC_H2C_OP_CHANNEL_RB_OP - 1 qword */
#define NNP_H2C_CHANNEL_RB_OP_OP_MASK      NNP_H2C_OP_MASK
#define NNP_H2C_CHANNEL_RB_OP_CHAN_ID_MASK GENMASK_ULL(15, 6)
#define NNP_H2C_CHANNEL_RB_OP_H2C_MASK     BIT_ULL(16)
#define NNP_H2C_CHANNEL_RB_OP_ID_MASK      BIT_ULL(17)
#define NNP_H2C_CHANNEL_RB_OP_DESTROY_MASK BIT_ULL(18)
#define NNP_H2C_CHANNEL_RB_OP_HOST_PFN_MASK GENMASK_ULL(63, 19)

/* NNP_IPC_H2C_OP_CHANNEL_HOSTRES_OP - 2 qwords */
/* qword 0: */
#define NNP_H2C_CHANNEL_HOSTRES_QW0_OP_MASK      NNP_H2C_OP_MASK
#define NNP_H2C_CHANNEL_HOSTRES_QW0_CHAN_ID_MASK GENMASK_ULL(15, 6)
#define NNP_H2C_CHANNEL_HOSTRES_QW0_ID_MASK      GENMASK_ULL(31, 16)
#define NNP_H2C_CHANNEL_HOSTRES_QW0_UNMAP_MASK   BIT_ULL(32)
/* qword 1: */
#define NNP_H2C_CHANNEL_HOSTRES_QW1_HOST_PFN_MASK GENMASK_ULL(44, 0)

/* NNP_IPC_H2C_OP_CLOCK_STAMP - 2 qwords */
/* qword 0: */
#define NNP_H2C_CLOCK_STAMP_QW0_OP_MASK   NNP_H2C_OP_MASK
#define NNP_H2C_CLOCK_STAMP_QW0_TYPE_MASK GENMASK_ULL(63, 8)
/* qword 1: clock stamp value */

/*
 * IPC messages opcodes and related utility macros
 */
#define H2C_OPCODE_NAME(name)          NNP_IPC_H2C_OP_ ## name
#define H2C_OPCODE_NAME_STR(name)      #name
#define C2H_OPCODE_NAME(name)          NNP_IPC_C2H_OP_ ## name
#define C2H_OPCODE_NAME_STR(name)      #name

/*
 * Define Host-to-card opcodes  (valid range is 0 - 31)
 */
enum nnp_h2c_opcodes {
	H2C_OPCODE_NAME(QUERY_VERSION)       = 0,
	H2C_OPCODE_NAME(CLOCK_STAMP)         = 2,
	H2C_OPCODE_NAME(SETUP_CRASH_DUMP)    = 6,
	H2C_OPCODE_NAME(SETUP_SYS_INFO_PAGE) = 7,
	H2C_OPCODE_NAME(CHANNEL_OP)          = 22,
	H2C_OPCODE_NAME(CHANNEL_RB_OP)       = 23,
	H2C_OPCODE_NAME(CHANNEL_HOSTRES_OP)  = 24,

	H2C_OPCODE_NAME(BIOS_PROTOCOL)       = 31,
	H2C_OPCODE_NAME(LAST)                = NNP_IPC_H2C_OP_BIOS_PROTOCOL,
};

/*
 * Define Card-to-host opcodes
 */
enum nnp_c2h_opcodes {
	NNP_IPC_C2H_OP_QUERY_VERSION_REPLY  = 0,
	NNP_IPC_C2H_OP_QUERY_VERSION_REPLY2 = 1,
	NNP_IPC_C2H_OP_QUERY_VERSION_REPLY3 = 2,
	NNP_IPC_C2H_OP_EVENT_REPORT         = 4,
	NNP_IPC_C2H_OP_SYS_INFO             = 11,

	NNP_IPC_C2H_OP_BIOS_PROTOCOL        = 31,
	NNP_IPC_C2H_OPCODE_LAST             = NNP_IPC_C2H_OP_BIOS_PROTOCOL,
};

/*
 * IPC messages protocol between the host driver and card BIOS
 */

enum nnp_bios_c2h_msg_types {
	NNP_IPC_C2H_TYPE_BIOS_VERSION  = 0x1,
};

enum nnp_bios_h2c_msg_types {
	NNP_IPC_H2C_TYPE_BOOT_IMAGE_READY  = 0x10,
	NNP_IPC_H2C_TYPE_SYSTEM_INFO_REQ   = 0x11,
};

/* NNP_IPC_C2H_OP_BIOS_PROTOCOL - 1 qword */
#define NNP_C2H_BIOS_PROTOCOL_OP_MASK   NNP_C2H_OP_MASK
/* bios message type */
#define NNP_C2H_BIOS_PROTOCOL_TYPE_MASK GENMASK_ULL(15, 8)
/* message size in bytes */
#define NNP_C2H_BIOS_PROTOCOL_SIZE_MASK GENMASK_ULL(31, 16)

/* BIOS Revision Identification Specification, Rev. 2.0, 01/30/2015 */
struct nnp_c2h_bios_version {
	__le16 board_id[7];
	__le16 board_rev;
	__le16 dot1;
	__le16 board_ext[3];
	__le16 dot2;
	__le16 version_major[4];
	__le16 dot3;
	__le16 build_type;
	__le16 version_minor[2];
	__le16 dot4;
	__le16 time_stamp[10];
	__le16 null_terminator;
} __packed;

struct nnp_c2h_bios_fw_ver_ack_data {
	__le16  code_minor;
	__le16  code_major;
	__le16  code_build_no;
	__le16  code_hot_fix;
	__le16  rcvyminor;
	__le16  rcvymajor;
	__le16  rcvybuildno;
	__le16  rcvy_hot_fix;
	__le16  fitc_minor;
	__le16  fitc_major;
	__le16  fitcbuildno;
	__le16  fitc_hot_fix;
} __packed;

struct nnp_c2h_fw_version {
	__le16  major;
	__le16  minor;
	__le16  hotfix;
	__le16  build;
} __packed;

struct nnp_c2h_cpu_info {
	__le32 cpu_family;      /* CPU Family: 0x000906D0 */
	__u8  cpu_stepping;    /* CPU Stepping */
	__u8  cpu_sku;         /* CPU SKU */
	__le16 cpu_did;         /* range 0x4580-0x45FF */
	__le16 cpu_core_count;   /* Number of enabled cores */
	__le16 cpu_thread_count; /* Number of threads */
} __packed;

struct nnp_c2h_ice_info {
	__le16 ice_count;
	__le32 ice_available_mask;
} __packed;

struct nnp_c2h_system_info {
	__u8  version; /* NNP_SYSTEM_INFO structure version */
	__le16 board_id; /* Board identification- for RVP = 0x25 */
	__u8  fab_id;   /* Board Revision identification */
	__u8  bom_id;   /* Board Bill Of Material identification */
	__u8  platform_type;   /* For RVP= 0x2, M.2 = 0x3 */
	__u8  platform_flavor; /* For NNP = 0x5- Embedded */
	struct nnp_c2h_cpu_info cpu_info; /* CPU Information */
	struct nnp_c2h_ice_info ice_info; /* ICE Information */
	struct nnp_c2h_bios_version bios_ver; /* BIOS version string */
	struct nnp_c2h_bios_fw_ver_ack_data csme_version;
	struct nnp_c2h_fw_version pmc_version;
} __packed;

/* NNP_H2C_BOOT_IMAGE_READY command - 3 qwords */
/* qword 0: */
/* op should be set to NNP_IPC_H2C_OP_BIOS_PROTOCOL */
#define NNP_H2C_BOOT_IMAGE_READY_QW0_OP_MASK         NNP_H2C_OP_MASK
/* bios message type = NNP_IPC_H2C_TYPE_BOOT_IMAGE_READY */
#define NNP_H2C_BOOT_IMAGE_READY_QW0_TYPE_MASK       GENMASK_ULL(15, 8)
/* message size in bytes = 2*sizeof(u64) */
#define NNP_H2C_BOOT_IMAGE_READY_QW0_SIZE_MASK       GENMASK_ULL(31, 16)
/* qword 1: page table describtor address */
/* qword 2: */
#define NNP_H2C_BOOT_IMAGE_READY_QW2_DESC_SIZE_MASK  GENMASK_ULL(31, 0)
#define NNP_H2C_BOOT_IMAGE_READY_QW2_IMAGE_SIZE_MASK GENMASK_ULL(63, 32)

/* NNP_H2C_BIOS_SYS_INFO_REQ command - 3 qwords */
/* qword 0: */
/* op should be set to NNP_IPC_H2C_OP_BIOS_PROTOCOL */
#define NNP_H2C_BIOS_SYS_INFO_REQ_QW0_OP_MASK         NNP_H2C_OP_MASK
/* bios message type = NNP_IPC_H2C_TYPE_SYSTEM_INFO_REQ */
#define NNP_H2C_BIOS_SYS_INFO_REQ_QW0_TYPE_MASK       GENMASK_ULL(15, 8)
/* message size in bytes = 2*sizeof(u64) */
#define NNP_H2C_BIOS_SYS_INFO_REQ_QW0_SIZE_MASK       GENMASK_ULL(31, 16)
/* qword 1: allocate sys info page address */
/* qword 2: */
#define NNP_H2C_BIOS_SYS_INFO_REQ_QW2_SIZE_MASK       GENMASK_ULL(31, 0)

#define NNP_BIOS_VERSION_LEN  \
		(sizeof(struct nnp_c2h_bios_version) / sizeof(__le16))
#define NNP_BOARD_NAME_LEN      72
#define NNP_IMAGE_VERSION_LEN   128
#define NNP_PRD_SERIAL_LEN      16
#define NNP_PART_NUM_LEN        12

struct nnp_sys_info {
	__le32 ice_mask;
	char bios_version[NNP_BIOS_VERSION_LEN];
	char board_name[NNP_BOARD_NAME_LEN];
	char image_version[NNP_IMAGE_VERSION_LEN];
	char prd_serial[NNP_PRD_SERIAL_LEN];
	char brd_part_no[NNP_PART_NUM_LEN];
	__le16  fpga_rev;
	__le64 total_unprotected_memory;
	__le64 total_ecc_memory;
	__u8 stepping;
} __packed;

/*
 * Define header structure for all "channel" message protocols.
 * This protocol defines communication between host UMD and card.
 */
#define NNP_H2C_CHAN_MSG_OP_MASK      NNP_H2C_OP_MASK
#define NNP_H2C_CHAN_MSG_CHAN_ID_MASK GENMASK_ULL(15, 6)

#define NNP_C2H_CHAN_MSG_OP_MASK      NNP_C2H_OP_MASK
#define NNP_C2H_CHAN_MSG_CHAN_ID_MASK GENMASK_ULL(15, 6)

#endif /* of _IPC_PROTOCOL_H */
