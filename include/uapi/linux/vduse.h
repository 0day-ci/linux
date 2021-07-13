/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_VDUSE_H_
#define _UAPI_VDUSE_H_

#include <linux/types.h>

#define VDUSE_BASE	0x81

/* The ioctls for control device (/dev/vduse/control) */

#define VDUSE_API_VERSION	0

/*
 * Get the version of VDUSE API that kernel supported (VDUSE_API_VERSION).
 * This is used for future extension.
 */
#define VDUSE_GET_API_VERSION	_IOR(VDUSE_BASE, 0x00, __u64)

/* Set the version of VDUSE API that userspace supported. */
#define VDUSE_SET_API_VERSION	_IOW(VDUSE_BASE, 0x01, __u64)

/*
 * The basic configuration of a VDUSE device, which is used by
 * VDUSE_CREATE_DEV ioctl to create a VDUSE device.
 */
struct vduse_dev_config {
#define VDUSE_NAME_MAX	256
	char name[VDUSE_NAME_MAX]; /* vduse device name, needs to be NUL terminated */
	__u32 vendor_id; /* virtio vendor id */
	__u32 device_id; /* virtio device id */
	__u64 features; /* virtio features */
	__u64 bounce_size; /* the size of bounce buffer for data transfer */
	__u32 vq_num; /* the number of virtqueues */
	__u32 vq_align; /* the allocation alignment of virtqueue's metadata */
	__u32 reserved[15]; /* for future use */
	__u32 config_size; /* the size of the configuration space */
	__u8 config[0]; /* the buffer of the configuration space */
};

/* Create a VDUSE device which is represented by a char device (/dev/vduse/$NAME) */
#define VDUSE_CREATE_DEV	_IOW(VDUSE_BASE, 0x02, struct vduse_dev_config)

/*
 * Destroy a VDUSE device. Make sure there are no more references
 * to the char device (/dev/vduse/$NAME).
 */
#define VDUSE_DESTROY_DEV	_IOW(VDUSE_BASE, 0x03, char[VDUSE_NAME_MAX])

/* The ioctls for VDUSE device (/dev/vduse/$NAME) */

/*
 * The information of one IOVA region, which is retrieved from
 * VDUSE_IOTLB_GET_FD ioctl.
 */
struct vduse_iotlb_entry {
	__u64 offset; /* the mmap offset on returned file descriptor */
	__u64 start; /* start of the IOVA range: [start, last] */
	__u64 last; /* last of the IOVA range: [start, last] */
#define VDUSE_ACCESS_RO 0x1
#define VDUSE_ACCESS_WO 0x2
#define VDUSE_ACCESS_RW 0x3
	__u8 perm; /* access permission of this region */
};

/*
 * Find the first IOVA region that overlaps with the range [start, last]
 * and return the corresponding file descriptor. Return -EINVAL means the
 * IOVA region doesn't exist. Caller should set start and last fields.
 */
#define VDUSE_IOTLB_GET_FD	_IOWR(VDUSE_BASE, 0x10, struct vduse_iotlb_entry)

/*
 * Get the negotiated virtio features. It's a subset of the features in
 * struct vduse_dev_config which can be accepted by virtio driver. It's
 * only valid after FEATURES_OK status bit is set.
 */
#define VDUSE_DEV_GET_FEATURES	_IOR(VDUSE_BASE, 0x11, __u64)

/*
 * The information that is used by VDUSE_DEV_SET_CONFIG ioctl to update
 * device configuration space.
 */
struct vduse_config_data {
	__u32 offset; /* offset from the beginning of configuration space */
	__u32 length; /* the length to write to configuration space */
	__u8 buffer[0]; /* buffer used to write from */
};

/* Set device configuration space */
#define VDUSE_DEV_SET_CONFIG	_IOW(VDUSE_BASE, 0x12, struct vduse_config_data)

/*
 * Inject a config interrupt. It's usually used to notify virtio driver
 * that device configuration space has changed.
 */
#define VDUSE_DEV_INJECT_IRQ	_IO(VDUSE_BASE, 0x13)

/*
 * The basic configuration of a virtqueue, which is used by
 * VDUSE_VQ_SETUP ioctl to setup a virtqueue.
 */
struct vduse_vq_config {
	__u32 index; /* virtqueue index */
	__u16 max_size; /* the max size of virtqueue */
};

/*
 * Setup the specified virtqueue. Make sure all virtqueues have been
 * configured before the device is attached to vDPA bus.
 */
#define VDUSE_VQ_SETUP		_IOW(VDUSE_BASE, 0x14, struct vduse_vq_config)

struct vduse_vq_state_split {
	__u16 avail_index; /* available index */
};

struct vduse_vq_state_packed {
	__u16 last_avail_counter:1; /* last driver ring wrap counter observed by device */
	__u16 last_avail_idx:15; /* device available index */
	__u16 last_used_counter:1; /* device ring wrap counter */
	__u16 last_used_idx:15; /* used index */
};

/*
 * The information of a virtqueue, which is retrieved from
 * VDUSE_VQ_GET_INFO ioctl.
 */
struct vduse_vq_info {
	__u32 index; /* virtqueue index */
	__u32 num; /* the size of virtqueue */
	__u64 desc_addr; /* address of desc area */
	__u64 driver_addr; /* address of driver area */
	__u64 device_addr; /* address of device area */
	union {
		struct vduse_vq_state_split split; /* split virtqueue state */
		struct vduse_vq_state_packed packed; /* packed virtqueue state */
	};
	__u8 ready; /* ready status of virtqueue */
};

/* Get the specified virtqueue's information. Caller should set index field. */
#define VDUSE_VQ_GET_INFO	_IOWR(VDUSE_BASE, 0x15, struct vduse_vq_info)

/*
 * The eventfd configuration for the specified virtqueue. It's used by
 * VDUSE_VQ_SETUP_KICKFD ioctl to setup kick eventfd.
 */
struct vduse_vq_eventfd {
	__u32 index; /* virtqueue index */
#define VDUSE_EVENTFD_DEASSIGN -1
	int fd; /* eventfd, -1 means de-assigning the eventfd */
};

/*
 * Setup kick eventfd for specified virtqueue. The kick eventfd is used
 * by VDUSE kernel module to notify userspace to consume the avail vring.
 */
#define VDUSE_VQ_SETUP_KICKFD	_IOW(VDUSE_BASE, 0x16, struct vduse_vq_eventfd)

/*
 * Inject an interrupt for specific virtqueue. It's used to notify virtio driver
 * to consume the used vring.
 */
#define VDUSE_VQ_INJECT_IRQ	_IOW(VDUSE_BASE, 0x17, __u32)

/* The control messages definition for read/write on /dev/vduse/$NAME */

enum vduse_req_type {
	/* Get the state for specified virtqueue from userspace */
	VDUSE_GET_VQ_STATE,
	/* Set the device status */
	VDUSE_SET_STATUS,
	/*
	 * Notify userspace to update the memory mapping for specified
	 * IOVA range via VDUSE_IOTLB_GET_FD ioctl
	 */
	VDUSE_UPDATE_IOTLB,
};

struct vduse_vq_state {
	__u32 index; /* virtqueue index */
	union {
		struct vduse_vq_state_split split; /* split virtqueue state */
		struct vduse_vq_state_packed packed; /* packed virtqueue state */
	};
};

struct vduse_dev_status {
	__u8 status; /* device status */
};

struct vduse_iova_range {
	__u64 start; /* start of the IOVA range: [start, end] */
	__u64 last; /* last of the IOVA range: [start, end] */
};

struct vduse_dev_request {
	__u32 type; /* request type */
	__u32 request_id; /* request id */
	__u32 reserved[2]; /* for future use */
	union {
		struct vduse_vq_state vq_state; /* virtqueue state, only use index */
		struct vduse_dev_status s; /* device status */
		struct vduse_iova_range iova; /* IOVA range for updating */
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

#endif /* _UAPI_VDUSE_H_ */
