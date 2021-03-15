/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_VDUSE_H_
#define _UAPI_VDUSE_H_

#include <linux/types.h>

#define VDUSE_API_VERSION	0

#define VDUSE_CONFIG_DATA_LEN	256
#define VDUSE_NAME_MAX	256

/* the control messages definition for read/write */

enum vduse_req_type {
	VDUSE_SET_VQ_NUM,
	VDUSE_SET_VQ_ADDR,
	VDUSE_SET_VQ_READY,
	VDUSE_GET_VQ_READY,
	VDUSE_SET_VQ_STATE,
	VDUSE_GET_VQ_STATE,
	VDUSE_SET_FEATURES,
	VDUSE_GET_FEATURES,
	VDUSE_SET_STATUS,
	VDUSE_GET_STATUS,
	VDUSE_SET_CONFIG,
	VDUSE_GET_CONFIG,
	VDUSE_UPDATE_IOTLB,
};

struct vduse_vq_num {
	__u32 index;
	__u32 num;
};

struct vduse_vq_addr {
	__u32 index;
	__u64 desc_addr;
	__u64 driver_addr;
	__u64 device_addr;
};

struct vduse_vq_ready {
	__u32 index;
	__u8 ready;
};

struct vduse_vq_state {
	__u32 index;
	__u16 avail_idx;
};

struct vduse_dev_config_data {
	__u32 offset;
	__u32 len;
	__u8 data[VDUSE_CONFIG_DATA_LEN];
};

struct vduse_iova_range {
	__u64 start;
	__u64 last;
};

struct vduse_features {
	__u64 features;
};

struct vduse_status {
	__u8 status;
};

struct vduse_dev_request {
	__u32 type; /* request type */
	__u32 request_id; /* request id */
	__u32 reserved[2]; /* for feature use */
	union {
		struct vduse_vq_num vq_num; /* virtqueue num */
		struct vduse_vq_addr vq_addr; /* virtqueue address */
		struct vduse_vq_ready vq_ready; /* virtqueue ready status */
		struct vduse_vq_state vq_state; /* virtqueue state */
		struct vduse_dev_config_data config; /* virtio device config space */
		struct vduse_iova_range iova; /* iova range for updating */
		struct vduse_features f; /* virtio features */
		struct vduse_status s; /* device status */
		__u32 padding[16]; /* padding */
	};
};

struct vduse_dev_response {
	__u32 request_id; /* corresponding request id */
#define VDUSE_REQUEST_OK	0x00
#define VDUSE_REQUEST_FAILED	0x01
	__u32 result; /* the result of request */
	__u32 reserved[2]; /* for feature use */
	union {
		struct vduse_vq_ready vq_ready; /* virtqueue ready status */
		struct vduse_vq_state vq_state; /* virtqueue state */
		struct vduse_dev_config_data config; /* virtio device config space */
		struct vduse_features f; /* virtio features */
		struct vduse_status s; /* device status */
		__u32 padding[16]; /* padding */
	};
};

/* ioctls */

struct vduse_dev_config {
	char name[VDUSE_NAME_MAX]; /* vduse device name */
	__u32 vendor_id; /* virtio vendor id */
	__u32 device_id; /* virtio device id */
	__u64 bounce_size; /* bounce buffer size for iommu */
	__u16 vq_num; /* the number of virtqueues */
	__u16 vq_size_max; /* the max size of virtqueue */
	__u32 vq_align; /* the allocation alignment of virtqueue's metadata */
};

struct vduse_iotlb_entry {
	int fd;
#define VDUSE_ACCESS_RO 0x1
#define VDUSE_ACCESS_WO 0x2
#define VDUSE_ACCESS_RW 0x3
	__u8 perm; /* access permission of this range */
	__u64 offset; /* the mmap offset on fd */
	__u64 start; /* start of the IOVA range */
	__u64 last; /* last of the IOVA range */
};

struct vduse_vq_eventfd {
	__u32 index; /* virtqueue index */
#define VDUSE_EVENTFD_DEASSIGN -1
	int fd; /* eventfd, -1 means de-assigning the eventfd */
};

#define VDUSE_BASE	0x81

/* Get the version of VDUSE API. This is used for future extension */
#define VDUSE_GET_API_VERSION	_IO(VDUSE_BASE, 0x00)

/* Create a vduse device which is represented by a char device (/dev/vduse/<name>) */
#define VDUSE_CREATE_DEV	_IOW(VDUSE_BASE, 0x01, struct vduse_dev_config)

/* Destroy a vduse device. Make sure there are no references to the char device */
#define VDUSE_DESTROY_DEV	_IOW(VDUSE_BASE, 0x02, char[VDUSE_NAME_MAX])

/* Get a mmap'able iova region */
#define VDUSE_IOTLB_GET_ENTRY	_IOWR(VDUSE_BASE, 0x03, struct vduse_iotlb_entry)

/* Setup an eventfd to receive kick for virtqueue */
#define VDUSE_VQ_SETUP_KICKFD	_IOW(VDUSE_BASE, 0x04, struct vduse_vq_eventfd)

/* Inject an interrupt for specific virtqueue */
#define VDUSE_INJECT_VQ_IRQ	_IO(VDUSE_BASE, 0x05)

#endif /* _UAPI_VDUSE_H_ */
