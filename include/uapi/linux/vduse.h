/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_VDUSE_H_
#define _UAPI_VDUSE_H_

#include <linux/types.h>

#define VDUSE_API_VERSION	0

#define VDUSE_NAME_MAX	256

/* the control messages definition for read/write */

enum vduse_req_type {
	/* Get the state for virtqueue from userspace */
	VDUSE_GET_VQ_STATE,
	/* Notify userspace to start the dataplane, no reply */
	VDUSE_START_DATAPLANE,
	/* Notify userspace to stop the dataplane, no reply */
	VDUSE_STOP_DATAPLANE,
	/* Notify userspace to update the memory mapping in device IOTLB */
	VDUSE_UPDATE_IOTLB,
};

struct vduse_vq_state {
	__u32 index; /* virtqueue index */
	__u32 avail_idx; /* virtqueue state (last_avail_idx) */
};

struct vduse_iova_range {
	__u64 start; /* start of the IOVA range */
	__u64 last; /* end of the IOVA range */
};

struct vduse_dev_request {
	__u32 type; /* request type */
	__u32 request_id; /* request id */
#define VDUSE_REQ_FLAGS_NO_REPLY	(1 << 0) /* No need to reply */
	__u32 flags; /* request flags */
	__u32 reserved; /* for future use */
	union {
		struct vduse_vq_state vq_state; /* virtqueue state */
		struct vduse_iova_range iova; /* iova range for updating */
		__u32 padding[16]; /* padding */
	};
};

struct vduse_dev_response {
	__u32 request_id; /* corresponding request id */
#define VDUSE_REQ_RESULT_OK	0x00
#define VDUSE_REQ_RESULT_FAILED	0x01
	__u32 result; /* the result of request */
	__u32 reserved[2]; /* for future use */
	union {
		struct vduse_vq_state vq_state; /* virtqueue state */
		__u32 padding[16]; /* padding */
	};
};

/* ioctls */

struct vduse_dev_config {
	char name[VDUSE_NAME_MAX]; /* vduse device name */
	__u32 vendor_id; /* virtio vendor id */
	__u32 device_id; /* virtio device id */
	__u64 features; /* device features */
	__u64 bounce_size; /* bounce buffer size for iommu */
	__u16 vq_size_max; /* the max size of virtqueue */
	__u16 padding; /* padding */
	__u32 vq_num; /* the number of virtqueues */
	__u32 vq_align; /* the allocation alignment of virtqueue's metadata */
	__u32 config_size; /* the size of the configuration space */
	__u32 reserved[15]; /* for future use */
	__u8 config[0]; /* the buffer of the configuration space */
};

struct vduse_iotlb_entry {
	__u64 offset; /* the mmap offset on fd */
	__u64 start; /* start of the IOVA range */
	__u64 last; /* last of the IOVA range */
#define VDUSE_ACCESS_RO 0x1
#define VDUSE_ACCESS_WO 0x2
#define VDUSE_ACCESS_RW 0x3
	__u8 perm; /* access permission of this range */
};

struct vduse_config_update {
	__u32 offset; /* offset from the beginning of configuration space */
	__u32 length; /* the length to write to configuration space */
	__u8 buffer[0]; /* buffer used to write from */
};

struct vduse_vq_info {
	__u32 index; /* virtqueue index */
	__u32 avail_idx; /* virtqueue state (last_avail_idx) */
	__u64 desc_addr; /* address of desc area */
	__u64 driver_addr; /* address of driver area */
	__u64 device_addr; /* address of device area */
	__u32 num; /* the size of virtqueue */
	__u8 ready; /* ready status of virtqueue */
};

struct vduse_vq_eventfd {
	__u32 index; /* virtqueue index */
#define VDUSE_EVENTFD_DEASSIGN -1
	int fd; /* eventfd, -1 means de-assigning the eventfd */
};

#define VDUSE_BASE	0x81

/* Get the version of VDUSE API. This is used for future extension */
#define VDUSE_GET_API_VERSION	_IOR(VDUSE_BASE, 0x00, __u64)

/* Set the version of VDUSE API. */
#define VDUSE_SET_API_VERSION	_IOW(VDUSE_BASE, 0x01, __u64)

/* Create a vduse device which is represented by a char device (/dev/vduse/<name>) */
#define VDUSE_CREATE_DEV	_IOW(VDUSE_BASE, 0x02, struct vduse_dev_config)

/* Destroy a vduse device. Make sure there are no references to the char device */
#define VDUSE_DESTROY_DEV	_IOW(VDUSE_BASE, 0x03, char[VDUSE_NAME_MAX])

/*
 * Get a file descriptor for the first overlapped iova region,
 * -EINVAL means the iova region doesn't exist.
 */
#define VDUSE_IOTLB_GET_FD	_IOWR(VDUSE_BASE, 0x04, struct vduse_iotlb_entry)

/* Get the negotiated features */
#define VDUSE_DEV_GET_FEATURES	_IOR(VDUSE_BASE, 0x05, __u64)

/* Update the configuration space */
#define VDUSE_DEV_UPDATE_CONFIG	_IOW(VDUSE_BASE, 0x06, struct vduse_config_update)

/* Get the specified virtqueue's information */
#define VDUSE_VQ_GET_INFO	_IOWR(VDUSE_BASE, 0x07, struct vduse_vq_info)

/* Setup an eventfd to receive kick for virtqueue */
#define VDUSE_VQ_SETUP_KICKFD	_IOW(VDUSE_BASE, 0x08, struct vduse_vq_eventfd)

/* Inject an interrupt for specific virtqueue */
#define VDUSE_VQ_INJECT_IRQ	_IOW(VDUSE_BASE, 0x09, __u32)

#endif /* _UAPI_VDUSE_H_ */
