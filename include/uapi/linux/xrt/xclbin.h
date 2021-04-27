/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  Xilinx FPGA compiled binary container format
 *
 *  Copyright (C) 2015-2021, Xilinx Inc
 */

#ifndef _XCLBIN_H_
#define _XCLBIN_H_

#if defined(__KERNEL__)

#include <linux/types.h>

#elif defined(__cplusplus)

#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <uuid/uuid.h>

#else

#include <stdlib.h>
#include <stdint.h>
#include <uuid/uuid.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOC: Container format for Xilinx FPGA images
 * The container stores bitstreams, metadata and firmware images.
 * xclbin/xsabin is an ELF-like binary container format. It is a structured
 * series of sections. There is a file header followed by several section
 * headers which is followed by sections. A section header points to an
 * actual section. There is an optional signature at the end. The
 * following figure illustrates a typical xclbin:
 *
 *     +---------------------+
 *     |                     |
 *     |       HEADER        |
 *     +---------------------+
 *     |   SECTION  HEADER   |
 *     |                     |
 *     +---------------------+
 *     |        ...          |
 *     |                     |
 *     +---------------------+
 *     |   SECTION  HEADER   |
 *     |                     |
 *     +---------------------+
 *     |       SECTION       |
 *     |                     |
 *     +---------------------+
 *     |         ...         |
 *     |                     |
 *     +---------------------+
 *     |       SECTION       |
 *     |                     |
 *     +---------------------+
 *     |      SIGNATURE      |
 *     |      (OPTIONAL)     |
 *     +---------------------+
 */

enum XCLBIN_MODE {
	XCLBIN_FLAT = 0,
	XCLBIN_PR,
	XCLBIN_TANDEM_STAGE2,
	XCLBIN_TANDEM_STAGE2_WITH_PR,
	XCLBIN_HW_EMU,
	XCLBIN_SW_EMU,
	XCLBIN_MODE_MAX
};

enum axlf_section_kind {
	BITSTREAM = 0,
	CLEARING_BITSTREAM,
	EMBEDDED_METADATA,
	FIRMWARE,
	DEBUG_DATA,
	SCHED_FIRMWARE,
	MEM_TOPOLOGY,
	CONNECTIVITY,
	IP_LAYOUT,
	DEBUG_IP_LAYOUT,
	DESIGN_CHECK_POINT,
	CLOCK_FREQ_TOPOLOGY,
	MCS,
	BMC,
	BUILD_METADATA,
	KEYVALUE_METADATA,
	USER_METADATA,
	DNA_CERTIFICATE,
	PDI,
	BITSTREAM_PARTIAL_PDI,
	PARTITION_METADATA,
	EMULATION_DATA,
	SYSTEM_METADATA,
	SOFT_KERNEL,
	ASK_FLASH,
	AIE_METADATA,
	ASK_GROUP_TOPOLOGY,
	ASK_GROUP_CONNECTIVITY
};

enum MEM_TYPE {
	MEM_DDR3 = 0,
	MEM_DDR4,
	MEM_DRAM,
	MEM_STREAMING,
	MEM_PREALLOCATED_GLOB,
	MEM_ARE,
	MEM_HBM,
	MEM_BRAM,
	MEM_URAM,
	MEM_STREAMING_CONNECTION
};

enum IP_TYPE {
	IP_MB = 0,
	IP_KERNEL,
	IP_DNASC,
	IP_DDR4_CONTROLLER,
	IP_MEM_DDR4,
	IP_MEM_HBM
};

struct axlf_section_header {
	uint32_t section_kind;	    /* Section type */
	char section_name[16];	    /* Examples: "stage2", "clear1", */
				    /* "clear2", "ocl1", "ocl2, */
				    /* "ublaze", "sched" */
	char rsvd[4];
	uint64_t section_offset;    /* File offset of section data */
	uint64_t section_size;	    /* Size of section data */
} __packed;

struct axlf_header {
	uint64_t length;		    /* Total size of the xclbin file */
	uint64_t time_stamp;		    /* Number of seconds since epoch */
					    /* when xclbin was created */
	uint64_t feature_rom_timestamp;     /* TimeSinceEpoch of the featureRom */
	uint16_t version_patch;	    /* Patch Version */
	uint8_t version_major;	    /* Major Version - Version: 2.1.0 */
	uint8_t version_minor;	    /* Minor Version */
	uint32_t mode;		    /* XCLBIN_MODE */
	union {
		struct {
			uint64_t platform_id;	/* 64 bit platform ID: */
					/* vendor-device-subvendor-subdev */
			uint64_t feature_id;	/* 64 bit feature id */
		} rom;
		unsigned char rom_uuid[16];	/* feature ROM UUID for which */
						/* this xclbin was generated */
	};
	unsigned char platform_vbnv[64];	/* e.g. */
		/* xilinx:xil-accel-rd-ku115:4ddr-xpr:3.4: null terminated */
	union {
		char next_axlf[16];		/* Name of next xclbin file */
						/* in the daisy chain */
		unsigned char uuid[16];		/* uuid of this xclbin */
	};
	char debug_bin[16];			/* Name of binary with debug */
						/* information */
	uint32_t num_sections;		/* Number of section headers */
	char rsvd[4];
} __packed;

struct axlf {
	char magic[8];			/* Should be "xclbin2\0" */
	int32_t signature_length;		/* Length of the signature */
						/* -1 indicates no signature */
	unsigned char reserved[28];		/* Note: Initialized to 0xFFs */

	unsigned char key_block[256];		/* Signature for validation */
						/* of binary */
	uint64_t unique_id;			/* axlf's uniqueId, use it to */
						/* skip redownload etc */
	struct axlf_header header;		/* Inline header */
	struct axlf_section_header sections[1];   /* One or more section */
						    /* headers follow */
} __packed;

/* bitstream information */
struct xlnx_bitstream {
	uint8_t freq[8];
	char bits[1];
} __packed;

/****	MEMORY TOPOLOGY SECTION ****/
struct mem_data {
	uint8_t type; /* enum corresponding to mem_type */
	uint8_t used; /* if 0 this bank is not present */
	uint8_t rsvd[6];
	union {
		uint64_t size; /* if mem_type DDR, then size in KB */
		uint64_t route_id; /* if streaming then "route_id" */
	};
	union {
		uint64_t base_address;/* if DDR then the base address */
		uint64_t flow_id; /* if streaming then "flow id" */
	};
	unsigned char tag[16]; /* DDR: BANK0,1,2,3, has to be null */
			/* terminated; if streaming then stream0, 1 etc */
} __packed;

struct mem_topology {
	int32_t count; /* Number of mem_data */
	struct mem_data mem_data[1]; /* Should be sorted on mem_type */
} __packed;

/****	CONNECTIVITY SECTION ****/
/* Connectivity of each argument of CU(Compute Unit). It will be in terms
 * of argument index associated. For associating CU instances with arguments
 * and banks, start at the connectivity section. Using the ip_layout_index
 * access the ip_data.name. Now we can associate this CU instance with its
 * original CU name and get the connectivity as well. This enables us to form
 * related groups of CU instances.
 */

struct connection {
	int32_t arg_index; /* From 0 to n, may not be contiguous as scalars */
			   /* skipped */
	int32_t ip_layout_index; /* index into the ip_layout section */
			   /* ip_layout.ip_data[index].type == IP_KERNEL */
	int32_t mem_data_index; /* index of the mem_data . Flag error is */
				/* used false. */
} __packed;

struct connectivity {
	int32_t count;
	struct connection connection[1];
} __packed;

/****	IP_LAYOUT SECTION ****/

/* IP Kernel */
#define IP_INT_ENABLE_MASK	  0x0001
#define IP_INTERRUPT_ID_MASK  0x00FE
#define IP_INTERRUPT_ID_SHIFT 0x1

enum IP_CONTROL {
	AP_CTRL_HS = 0,
	AP_CTRL_CHAIN,
	AP_CTRL_NONE,
	AP_CTRL_ME,
	ACCEL_ADAPTER
};

#define IP_CONTROL_MASK	 0xFF00
#define IP_CONTROL_SHIFT 0x8

/* IPs on AXI lite - their types, names, and base addresses. */
struct ip_data {
	uint32_t type; /* map to IP_TYPE enum */
	union {
		uint32_t properties; /* Default: 32-bits to indicate ip */
				     /* specific property. */
		/* type: IP_KERNEL
		 *	    int_enable   : Bit  - 0x0000_0001;
		 *	    interrupt_id : Bits - 0x0000_00FE;
		 *	    ip_control   : Bits = 0x0000_FF00;
		 */
		struct {		 /* type: IP_MEM_* */
			uint16_t index;
			uint8_t pc_index;
			uint8_t unused;
		} indices;
	};
	uint64_t base_address;
	uint8_t name[64]; /* eg Kernel name corresponding to KERNEL */
			    /* instance, can embed CU name in future. */
} __packed;

struct ip_layout {
	int32_t count;
	struct ip_data ip_data[1]; /* All the ip_data needs to be sorted */
				     /* by base_address. */
} __packed;

/*** Debug IP section layout ****/
enum DEBUG_IP_TYPE {
	UNDEFINED = 0,
	LAPC,
	ILA,
	AXI_MM_MONITOR,
	AXI_TRACE_FUNNEL,
	AXI_MONITOR_FIFO_LITE,
	AXI_MONITOR_FIFO_FULL,
	ACCEL_MONITOR,
	AXI_STREAM_MONITOR,
	AXI_STREAM_PROTOCOL_CHECKER,
	TRACE_S2MM,
	AXI_DMA,
	TRACE_S2MM_FULL
};

struct debug_ip_data {
	uint8_t type; /* type of enum DEBUG_IP_TYPE */
	uint8_t index_lowbyte;
	uint8_t properties;
	uint8_t major;
	uint8_t minor;
	uint8_t index_highbyte;
	uint8_t reserved[2];
	uint64_t base_address;
	char	name[128];
} __packed;

struct debug_ip_layout {
	uint16_t count;
	struct debug_ip_data debug_ip_data[1];
} __packed;

/* Supported clock frequency types */
enum XCLBIN_CLOCK_TYPE {
	CT_UNUSED = 0,			   /* Initialized value */
	CT_DATA	  = 1,			   /* Data clock */
	CT_KERNEL = 2,			   /* Kernel clock */
	CT_SYSTEM = 3			   /* System Clock */
};

/* Clock Frequency Entry */
struct clock_freq {
	uint16_t freq_MHZ;		   /* Frequency in MHz */
	uint8_t type;			   /* Clock type (enum CLOCK_TYPE) */
	uint8_t unused[5];		   /* Not used - padding */
	char name[128];			   /* Clock Name */
} __packed;

/* Clock frequency section */
struct clock_freq_topology {
	int16_t count;		   /* Number of entries */
	struct clock_freq clock_freq[1]; /* Clock array */
} __packed;

/* Supported MCS file types */
enum MCS_TYPE {
	MCS_UNKNOWN = 0,		   /* Initialized value */
	MCS_PRIMARY = 1,		   /* The primary mcs file data */
	MCS_SECONDARY = 2,		   /* The secondary mcs file data */
};

/* One chunk of MCS data */
struct mcs_chunk {
	uint8_t type;			   /* MCS data type */
	uint8_t unused[7];		   /* padding */
	uint64_t offset;		   /* data offset from the start of */
					   /* the section */
	uint64_t size;		   /* data size */
} __packed;

/* MCS data section */
struct mcs {
	int8_t count;			   /* Number of chunks */
	int8_t unused[7];		   /* padding */
	struct mcs_chunk chunk[1];	   /* MCS chunks followed by data */
} __packed;

/* bmc data section */
struct bmc {
	uint64_t offset;		   /* data offset from the start of */
					   /* the section */
	uint64_t size;		   /* data size (bytes) */
	char image_name[64];		   /* Name of the image */
					   /* (e.g., MSP432P401R) */
	char device_name[64];		   /* Device ID	(e.g., VCU1525)	 */
	char version[64];
	char md5value[33];		   /* MD5 Expected Value */
				/* (e.g., 56027182079c0bd621761b7dab5a27ca) */
	char padding[7];		   /* Padding */
} __packed;

/* soft kernel data section, used by classic driver */
struct soft_kernel {
	/** Prefix Syntax:
	 *  mpo - member, pointer, offset
	 *  This variable represents a zero terminated string
	 *  that is offseted from the beginning of the section.
	 *  The pointer to access the string is initialized as follows:
	 *  char * pCharString = (address_of_section) + (mpo value)
	 */
	uint32_t mpo_name;	   /* Name of the soft kernel */
	uint32_t image_offset;   /* Image offset */
	uint32_t image_size;	   /* Image size */
	uint32_t mpo_version;	   /* Version */
	uint32_t mpo_md5_value;	   /* MD5 checksum */
	uint32_t mpo_symbol_name;  /* Symbol name */
	uint32_t num_instances;  /* Number of instances */
	uint8_t padding[36];	   /* Reserved for future use */
	uint8_t reserved_ext[16];   /* Reserved for future extended data */
} __packed;

enum CHECKSUM_TYPE {
	CST_UNKNOWN = 0,
	CST_SDBM = 1,
	CST_LAST
};

#ifdef __cplusplus
}
#endif

#endif
