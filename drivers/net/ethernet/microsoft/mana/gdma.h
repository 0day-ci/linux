/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2021, Microsoft Corporation. */

#ifndef _GDMA_H
#define _GDMA_H

#include <linux/dma-mapping.h>
#include <linux/netdevice.h>

#include "shm_channel.h"

enum gdma_request_type {
	GDMA_VERIFY_VF_DRIVER_VERSION	= 1,
	GDMA_QUERY_MAX_RESOURCES	= 2,
	GDMA_LIST_DEVICES		= 3,
	GDMA_REGISTER_DEVICE		= 4,
	GDMA_DEREGISTER_DEVICE		= 5,
	GDMA_GENERATE_TEST_EQE		= 10,
	GDMA_CREATE_QUEUE		= 12,
	GDMA_DISABLE_QUEUE		= 13,
	GDMA_CREATE_DMA_REGION		= 25,
	GDMA_DMA_REGION_ADD_PAGES	= 26,
	GDMA_DESTROY_DMA_REGION		= 27,
};

enum gdma_queue_type {
	GDMA_INVALID_QUEUE,
	GDMA_SQ,
	GDMA_RQ,
	GDMA_CQ,
	GDMA_EQ,
};

enum gdma_work_request_flags {
	GDMA_WR_NONE			= 0,
	GDMA_WR_OOB_IN_SGL		= BIT(0),
	GDMA_WR_SGL_DIRECT		= BIT(1),
	GDMA_WR_CONSUME_CREDIT		= BIT(2),
	GDMA_WR_FENCE			= BIT(3),
	GDMA_WR_CHECK_SN		= BIT(4),
	GDMA_WR_PAD_DATA_BY_FIRST_SGE	= BIT(5),
};

enum gdma_eqe_type {
	GDMA_EQE_COMPLETION		= 3,
	GDMA_EQE_TEST_EVENT		= 64,
	GDMA_EQE_SOC_TO_VF_EVENT	= 128,
	GDMA_EQE_HWC_INIT_EQ_ID_DB	= 129,
	GDMA_EQE_HWC_INIT_DATA		= 130,
	GDMA_EQE_HWC_INIT_DONE		= 131,
	GDMA_EQE_APP_START		= 132,
	GDMA_EQE_APP_END		= 255,
};

enum {
	GDMA_DEVICE_NONE = 0,
	GDMA_DEVICE_HWC = 1,
	GDMA_DEVICE_ANA = 2,
};

struct gdma_resource {
	/* Protect the bitmap */
	spinlock_t lock;

	/* The bitmap size in bits. */
	u32 size;

	/* The bitmap tracks the resources. */
	unsigned long *map;
};

union gdma_doorbell_entry {
	u64	as_uint64;

	struct {
		u64 id		: 24;
		u64 reserved	: 8;
		u64 tail_ptr	: 31;
		u64 arm		: 1;
	} cq;

	struct {
		u64 id		: 24;
		u64 wqe_cnt	: 8;
		u64 tail_ptr	: 32;
	} rq;

	struct {
		u64 id		: 24;
		u64 reserved	: 8;
		u64 tail_ptr	: 32;
	} sq;

	struct {
		u64 id		: 16;
		u64 reserved	: 16;
		u64 tail_ptr	: 31;
		u64 arm		: 1;
	} eq;
} __packed;

struct gdma_msg_hdr {
	u32 hdr_type;
	u32 msg_type;
	u16 msg_version;
	u16 hwc_msg_id;
	u32 msg_size;
} __packed;

struct gdma_dev_id {
	union {
		struct {
			u16 type;
			u16 instance;
		};

		u32 as_uint32;
	};
} __packed;

struct gdma_req_hdr {
	struct gdma_msg_hdr req;
	struct gdma_msg_hdr resp; /* The expected response */
	struct gdma_dev_id dev_id;
	u32 activity_id;
} __packed;

struct gdma_resp_hdr {
	struct gdma_msg_hdr response;
	struct gdma_dev_id dev_id;
	u32 activity_id;
	u32 status;
	u32 reserved;
} __packed;

struct gdma_general_req {
	struct gdma_req_hdr hdr;
} __packed;

#define GDMA_MESSAGE_V1 1

struct gdma_general_resp {
	struct gdma_resp_hdr hdr;
} __packed;

#define GDMA_STANDARD_HEADER_TYPE 0

static inline void gdma_init_req_hdr(struct gdma_req_hdr *hdr, u32 code,
				     u32 req_size, u32 resp_size)
{
	hdr->req.hdr_type = GDMA_STANDARD_HEADER_TYPE;
	hdr->req.msg_type = code;
	hdr->req.msg_version = GDMA_MESSAGE_V1;
	hdr->req.msg_size = req_size;

	hdr->resp.hdr_type = GDMA_STANDARD_HEADER_TYPE;
	hdr->resp.msg_type = code;
	hdr->resp.msg_version = GDMA_MESSAGE_V1;
	hdr->resp.msg_size = resp_size;
}

static inline bool is_gdma_msg(const void *req)
{
	struct gdma_req_hdr *hdr = (struct gdma_req_hdr *)req;

	if (hdr->req.hdr_type == GDMA_STANDARD_HEADER_TYPE &&
	    hdr->resp.hdr_type == GDMA_STANDARD_HEADER_TYPE &&
	    hdr->req.msg_size >= sizeof(struct gdma_req_hdr) &&
	    hdr->resp.msg_size >= sizeof(struct gdma_resp_hdr) &&
	    hdr->req.msg_type != 0 && hdr->resp.msg_type != 0)
		return true;

	return false;
}

static inline bool is_gdma_msg_len(const u32 req_len, const u32 resp_len,
				   const void *req)
{
	struct gdma_req_hdr *hdr = (struct gdma_req_hdr *)req;

	if (req_len >= sizeof(struct gdma_req_hdr) &&
	    resp_len >= sizeof(struct gdma_resp_hdr) &&
	    req_len >= hdr->req.msg_size && resp_len >= hdr->resp.msg_size &&
	    is_gdma_msg(req)) {
		return true;
	}

	return false;
}

/* The 16-byte struct is part of the GDMA work queue entry (WQE). */
struct gdma_sge {
	u64 address;
	u32 mem_key;
	u32 size;
} __packed;

struct gdma_wqe_request {
	struct gdma_sge *sgl;
	u32 num_sge;
	u32 inline_oob_size;
	const void *inline_oob_data;
	u32 flags;
	u32 client_data_unit;
};

enum GDMA_PAGE_TYPE {
	GDMA_PAGE_TYPE_4K,
	GDMA_PAGE_TYPE_8K,
	GDMA_PAGE_TYPE_16K,
	GDMA_PAGE_TYPE_32K,
	GDMA_PAGE_TYPE_64K,
	GDMA_PAGE_TYPE_128K,
	GDMA_PAGE_TYPE_256K,
	GDMA_PAGE_TYPE_512K,
	GDMA_PAGE_TYPE_1M,
	GDMA_PAGE_TYPE_2M,
};

#define GDMA_INVALID_DMA_REGION 0

struct gdma_mem_info {
	struct device *dev;

	dma_addr_t dma_handle;
	void *virt_addr;
	u64 length;

	u64 dma_region;
};

#define REGISTER_ATB_MST_MKEY_LOWER_SIZE 8

struct gdma_dev {
	struct gdma_dev_id dev_id;

	u32 pdid;
	u32 doorbell;
	u32 gpa_mkey;

	/* GDMA driver specific pointer */
	void *driver_data;
};

#define MINIMUM_SUPPORTED_PAGE_SIZE PAGE_SIZE

#define GDMA_CQE_SIZE 64
#define GDMA_EQE_SIZE 16
#define GDMA_MAX_SQE_SIZE 512
#define GDMA_MAX_RQE_SIZE 256

#define GDMA_COMP_DATA_SIZE 0x3C

#define GDMA_EVENT_DATA_SIZE 0xC

/* The WQE size must be a multiple of the Basic Unit, which is 32 bytes. */
#define GDMA_WQE_BU_SIZE 32

#define INVALID_PDID		UINT_MAX
#define INVALID_DOORBELL	UINT_MAX
#define INVALID_MEM_KEY		UINT_MAX
#define INVALID_QUEUE_ID	UINT_MAX
#define INVALID_PCI_MSIX_INDEX  UINT_MAX

struct gdma_comp {
	u32 cqe_data[GDMA_COMP_DATA_SIZE / 4];
	u32 wq_num;
	bool is_sq;
};

struct gdma_event {
	u32 details[GDMA_EVENT_DATA_SIZE / 4];
	u8  type;
};

struct gdma_queue;

#define CQE_POLLING_BUFFER 512
struct ana_eq {
	struct gdma_queue *eq;
	struct gdma_comp cqe_poll[CQE_POLLING_BUFFER];
};

typedef void gdma_eq_callback(void *context, struct gdma_queue *q,
			      struct gdma_event *e);

typedef void gdma_cq_callback(void *context, struct gdma_queue *q);

/* The 'head' is the producer index. For SQ/RQ, when the driver posts a WQE
 * (Note: the WQE size must be a multiple of the 32-byte Basic Unit), the
 * driver increases the 'head' in BUs rather than in bytes, and notifies
 * the HW of the updated head. For EQ/CQ, the driver uses the 'head' to track
 * the HW head, and increases the 'head' by 1 for every processed EQE/CQE.
 *
 * The 'tail' is the consumer index for SQ/RQ. After the CQE of the SQ/RQ is
 * processed, the driver increases the 'tail' to indicate that WQEs have
 * been consumed by the HW, so the driver can post new WQEs into the SQ/RQ.
 *
 * The driver doesn't use the 'tail' for EQ/CQ, because the driver ensures
 * that the EQ/CQ is big enough so they can't overflow, and the driver uses
 * the owner bits mechanism to detect if the queue has become empty.
 */
struct gdma_queue {
	struct gdma_dev *gdma_dev;

	enum gdma_queue_type type;
	u32 id;

	struct gdma_mem_info mem_info;

	void *queue_mem_ptr;
	u32 queue_size;

	bool monitor_avl_buf;

	u32 head;
	u32 tail;

	/* Extra fields specific to EQ/CQ. */
	union {
		struct {
			bool disable_needed;

			gdma_eq_callback *callback;
			void *context;

			unsigned int msix_index;

			u32 log2_throttle_limit;

			/* NAPI data */
			struct napi_struct napi;
			int work_done;
			int budget;
		} eq;

		struct {
			gdma_cq_callback *callback;
			void *context;

			struct gdma_queue *parent; /* For CQ/EQ relationship */
		} cq;
	};
};

struct gdma_queue_spec {
	enum gdma_queue_type type;
	bool monitor_avl_buf;
	unsigned int queue_size;

	/* Extra fields specific to EQ/CQ. */
	union {
		struct {
			gdma_eq_callback *callback;
			void *context;

			unsigned long log2_throttle_limit;
		} eq;

		struct {
			gdma_cq_callback *callback;
			void *context;

			struct gdma_queue *parent_eq;

		} cq;
	};
};

struct gdma_irq_context {
	void (*handler)(void *arg);
	void *arg;
};

struct gdma_context {
	struct pci_dev		*pci_dev;

	int			max_num_queue;
	int			max_num_msix;
	int			num_msix_usable;
	struct gdma_resource	msix_resource;
	struct gdma_irq_context	*irq_contexts;

	/* This maps a CQ index to the queue structure. */
	unsigned int		max_num_cq;
	struct gdma_queue	**cq_table;

	/* Protect eq_test_event and test_event_eq_id  */
	struct mutex		eq_test_event_mutex;
	struct completion	eq_test_event;
	u32			test_event_eq_id;

	void __iomem		*bar0_va;
	void __iomem		*shm_base;
	void __iomem		*db_page_base;
	u32 db_page_size;

	struct shm_channel	shm_channel;

	/* Azure network adapter */
	struct gdma_dev		ana;

	/* Hardware communication channel */
	struct gdma_dev		hwc;
};

#define MAX_NUM_GDMA_DEVICES	4

#define ana_to_gdma_context(d) container_of(d, struct gdma_context, ana)
#define hwc_to_gdma_context(d) container_of(d, struct gdma_context, hwc)

static inline bool gdma_is_ana(struct gdma_dev *gd)
{
	return gd->dev_id.type == GDMA_DEVICE_ANA;
}

static inline bool gdma_is_hwc(struct gdma_dev *gd)
{
	return gd->dev_id.type == GDMA_DEVICE_HWC;
}

static inline struct gdma_context *gdma_dev_to_context(struct gdma_dev *gd)
{
	if (gdma_is_hwc(gd))
		return hwc_to_gdma_context(gd);

	if (gdma_is_ana(gd))
		return ana_to_gdma_context(gd);

	return NULL;
}

u8 *gdma_get_wqe_ptr(const struct gdma_queue *wq, u32 wqe_offset);
u32 gdma_wq_avail_space(struct gdma_queue *wq);

int gdma_test_eq(struct gdma_context *gc, struct gdma_queue *eq);

int gdma_create_hwc_queue(struct gdma_dev *gd,
			  const struct gdma_queue_spec *spec,
			  struct gdma_queue **queue_ptr);

int gdma_create_ana_eq(struct gdma_dev *gd, const struct gdma_queue_spec *spec,
		       struct gdma_queue **queue_ptr);

int gdma_create_ana_wq_cq(struct gdma_dev *gd,
			  const struct gdma_queue_spec *spec,
			  struct gdma_queue **queue_ptr);

void gdma_destroy_queue(struct gdma_context *gc, struct gdma_queue *queue);

int gdma_poll_cq(struct gdma_queue *cq, struct gdma_comp *comp, int num_cqe);

void gdma_arm_cq(struct gdma_queue *cq);

struct gdma_wqe {
	u32 reserved	:24;
	u32 last_vbytes	:8;

	union {
		u32 flags;

		struct {
			u32 num_sge		:8;
			u32 inline_oob_size_div4:3;
			u32 client_oob_in_sgl	:1;
			u32 consume_credit	:1;
			u32 fence		:1;
			u32 reserved_1		:2;
			u32 client_data_unit	:14;
			u32 check_sn		:1;
			u32 sgl_direct		:1;
		};
	};
} __packed;

#define INLINE_OOB_SMALL_SIZE 8
#define INLINE_OOB_LARGE_SIZE 24

static inline u32 gdma_align_inline_oobsize(u32 oob_size)
{
	if (oob_size > INLINE_OOB_SMALL_SIZE)
		return INLINE_OOB_LARGE_SIZE;
	else
		return INLINE_OOB_SMALL_SIZE;
}

#define MAX_TX_WQE_SIZE 512
#define MAX_RX_WQE_SIZE 256

struct gdma_cqe {
	u32 cqe_data[GDMA_COMP_DATA_SIZE / 4];

	union {
		u32 as_uint32;

		struct {
			u32 wq_num	: 24;
			u32 is_sq	: 1;
			u32 reserved	: 4;
			u32 owner_bits	: 3;
		};
	} cqe_info;
} __packed;

#define GDMA_CQE_OWNER_BITS 3

#define GDMA_CQE_OWNER_MASK ((1 << GDMA_CQE_OWNER_BITS) - 1)

#define SET_ARM_BIT 1

#define GDMA_EQE_OWNER_BITS 3

union gdma_eqe_info {
	u32 as_uint32;

	struct {
		u32 type	: 8;
		u32 reserved_1	: 8;
		u32 client_id	: 2;
		u32 reserved_2	: 11;
		u32 owner_bits	: 3;
	};
} __packed;

#define GDMA_EQE_OWNER_MASK ((1 << GDMA_EQE_OWNER_BITS) - 1)
#define INITIALIZED_OWNER_BIT(log2_num_entries) (1UL << (log2_num_entries))

struct gdma_eqe {
	u32 details[GDMA_EVENT_DATA_SIZE / 4];
	u32 eqe_info;
} __packed;

#define GDMA_REG_DB_PAGE_OFFSET	8
#define GDMA_REG_DB_PAGE_SIZE	0x10
#define GDMA_REG_SHM_OFFSET	0x18

struct gdma_posted_wqe_info {
	u32 wqe_size_in_bu;
};

/* GDMA_GENERATE_TEST_EQE */
struct gdma_generate_test_event_req {
	struct gdma_req_hdr hdr;
	u32 queue_index;
} __packed;

/* GDMA_VERIFY_VF_DRIVER_VERSION */
enum {
	GDMA_PROTOCOL_UNDEFINED = 0,
	GDMA_PROTOCOL_V1 = 1,
	GDMA_PROTOCOL_FIRST = GDMA_PROTOCOL_V1,
	GDMA_PROTOCOL_LAST = GDMA_PROTOCOL_V1,
	GDMA_PROTOCOL_VALUE_MAX
};

#define VALID_DRIVER_FLAG_1_SUPPORTED_FLAGS 0
#define VALID_DRIVER_FLAG_2_SUPPORTED_FLAGS 0
#define VALID_DRIVER_FLAG_3_SUPPORTED_FLAGS 0
#define VALID_DRIVER_FLAG_4_SUPPORTED_FLAGS 0

struct gdma_verify_ver_req {
	struct gdma_req_hdr hdr;

	/* Mandatory fields required for protocol establishment */
	u64 protocol_ver_min;
	u64 protocol_ver_max;
	u64 drv_cap_flags1;
	u64 drv_cap_flags2;
	u64 drv_cap_flags3;
	u64 drv_cap_flags4;

	/* Advisory fields */
	u64 drv_ver;
	u32 os_type; /* Linux = 0x10; Windows = 0x20; Other = 0x30 */
	u32 reserved;
	u32 os_ver_major;
	u32 os_ver_minor;
	u32 os_ver_build;
	u32 os_ver_platform;
	u64 reserved_2;
	u8 os_ver_str1[128];
	u8 os_ver_str2[128];
	u8 os_ver_str3[128];
	u8 os_ver_str4[128];
} __packed;

struct gdma_verify_ver_resp {
	struct gdma_resp_hdr hdr;
	u64 gdma_protocol_ver;
	u64 pf_cap_flags1;
	u64 pf_cap_flags2;
	u64 pf_cap_flags3;
	u64 pf_cap_flags4;
} __packed;

/* GDMA_QUERY_MAX_RESOURCES */
struct gdma_query_max_resources_resp {
	struct gdma_resp_hdr hdr;
	u32 status;
	u32 max_sq;
	u32 max_rq;
	u32 max_cq;
	u32 max_eq;
	u32 max_db;
	u32 max_mst;
	u32 max_cq_mod_ctx;
	u32 max_mod_cq;
	u32 max_msix;
} __packed;

/* GDMA_LIST_DEVICES */
struct gdma_list_devices_resp {
	struct gdma_resp_hdr hdr;
	u32 num_of_clients;
	u32 reserved;
	struct gdma_dev_id clients[64];
} __packed;

/* GDMA_REGISTER_DEVICE */
struct gdma_register_device_resp {
	struct gdma_resp_hdr hdr;
	u32 pdid;
	u32 gpa_mkey;
	u32 db_id;
} __packed;

/* GDMA_CREATE_QUEUE */
struct gdma_create_queue_req {
	struct gdma_req_hdr hdr;
	u32 type;
	u32 reserved1;
	u32 pdid;
	u32 doolbell_id;
	u64 dma_region;
	u32 reserved2;
	u32 queue_size;
	u32 log2_throttle_limit;
	u32 eq_pci_msix_index;
	u32 cq_mod_ctx_id;
	u32 cq_parent_eq_id;
	u8  rq_drop_on_overrun;
	u8  rq_err_on_wqe_overflow;
	u8  rq_chain_rec_wqes;
	u8  sq_hw_db;
} __packed;

struct gdma_create_queue_resp {
	struct gdma_resp_hdr hdr;
	u32 queue_index;
} __packed;

/* GDMA_DISABLE_QUEUE */
struct gdma_disable_queue_req {
	struct gdma_req_hdr hdr;
	u32 type;
	u32 queue_index;
	u32 alloc_res_id_on_creation;
} __packed;

/* GDMA_CREATE_DMA_REGION */
struct gdma_create_dma_region_req {
	struct gdma_req_hdr hdr;

	/* The total size of the DMA region */
	u64 length;

	/* The offset in the first page */
	u32 offset_in_page;

	/* enum GDMA_PAGE_TYPE */
	u32 gdma_page_type;

	/* The total number of pages */
	u32 page_count;

	/* If page_addr_list_len is smaller than page_count,
	 * the remaining page addresses will be added via the
	 * message GDMA_DMA_REGION_ADD_PAGES.
	 */
	u32 page_addr_list_len;
	u64 page_addr_list[];
} __packed;

struct gdma_create_dma_region_resp {
	struct gdma_resp_hdr hdr;
	u64 dma_region;
} __packed;

/* GDMA_DMA_REGION_ADD_PAGES */
struct gdma_dma_region_add_pages_req {
	struct gdma_req_hdr hdr;

	u64 dma_region;

	u32 page_addr_list_len;
	u64 page_addr_list[];
} __packed;

/* GDMA_DESTROY_DMA_REGION */
struct gdma_destroy_dma_region_req {
	struct gdma_req_hdr hdr;

	u64 dma_region;
} __packed;

int gdma_verify_vf_version(struct pci_dev *pdev);

int gdma_register_device(struct gdma_dev *gd);
int gdma_deregister_device(struct gdma_dev *gd);

int gdma_post_work_request(struct gdma_queue *wq,
			   const struct gdma_wqe_request *wqe_req,
			   struct gdma_posted_wqe_info *wqe_info);

int gdma_post_and_ring(struct gdma_queue *queue,
		       const struct gdma_wqe_request *wqe,
		       struct gdma_posted_wqe_info *wqe_info);

int gdma_alloc_res_map(u32 res_avail, struct gdma_resource *r);
void gdma_free_res_map(struct gdma_resource *r);

void gdma_wq_ring_doorbell(struct gdma_context *gc, struct gdma_queue *queue);

int gdma_alloc_memory(struct gdma_context *gc, unsigned int length,
		      struct gdma_mem_info *gmi);

void gdma_free_memory(struct gdma_mem_info *gmi);

int gdma_send_request(struct gdma_context *gc, u32 req_len, const void *req,
		      u32 resp_len, void *resp);
#endif /* _GDMA_H */
