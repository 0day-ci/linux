==================================
VDUSE - "vDPA Device in Userspace"
==================================

vDPA (virtio data path acceleration) device is a device that uses a
datapath which complies with the virtio specifications with vendor
specific control path. vDPA devices can be both physically located on
the hardware or emulated by software. VDUSE is a framework that makes it
possible to implement software-emulated vDPA devices in userspace.

How VDUSE works
------------
Each userspace vDPA device is created by the VDUSE_CREATE_DEV ioctl on
the character device (/dev/vduse/control). Then a device file with the
specified name (/dev/vduse/$NAME) will appear, which can be used to
implement the userspace vDPA device's control path and data path.

To implement control path, a message-based communication protocol and some
types of control messages are introduced in the VDUSE framework:

- VDUSE_SET_VQ_ADDR: Set the vring address of virtqueue.

- VDUSE_SET_VQ_NUM: Set the size of virtqueue

- VDUSE_SET_VQ_READY: Set ready status of virtqueue

- VDUSE_GET_VQ_READY: Get ready status of virtqueue

- VDUSE_SET_VQ_STATE: Set the state for virtqueue

- VDUSE_GET_VQ_STATE: Get the state for virtqueue

- VDUSE_SET_FEATURES: Set virtio features supported by the driver

- VDUSE_GET_FEATURES: Get virtio features supported by the device

- VDUSE_SET_STATUS: Set the device status

- VDUSE_GET_STATUS: Get the device status

- VDUSE_SET_CONFIG: Write to device specific configuration space

- VDUSE_GET_CONFIG: Read from device specific configuration space

- VDUSE_UPDATE_IOTLB: Notify userspace to update the memory mapping in device IOTLB

Those control messages are mostly based on the vdpa_config_ops in
include/linux/vdpa.h which defines a unified interface to control
different types of vdpa device. Userspace needs to read()/write()
on the VDUSE device file to receive/reply those control messages
from/to VDUSE kernel module as follows:

.. code-block:: c

	static int vduse_message_handler(int dev_fd)
	{
		int len;
		struct vduse_dev_request req;
		struct vduse_dev_response resp;

		len = read(dev_fd, &req, sizeof(req));
		if (len != sizeof(req))
			return -1;

		resp.request_id = req.request_id;

		switch (req.type) {

		/* handle different types of message */

		}

		len = write(dev_fd, &resp, sizeof(resp));
		if (len != sizeof(resp))
			return -1;

		return 0;
	}

In the data path, vDPA device's iova regions will be mapped into userspace
with the help of VDUSE_IOTLB_GET_ENTRY ioctl on the VDUSE device file:

- VDUSE_IOTLB_GET_ENTRY: get a mmap'able iova region containing the specified iova.
  Userspace can access this iova region by passing corresponding size, offset, perm
  and fd to mmap(). For example:

.. code-block:: c

	static int perm_to_prot(uint8_t perm)
	{
		int prot = 0;

		switch (perm) {
		case VDUSE_ACCESS_WO:
			prot |= PROT_WRITE;
			break;
		case VDUSE_ACCESS_RO:
			prot |= PROT_READ;
			break;
		case VDUSE_ACCESS_RW:
			prot |= PROT_READ | PROT_WRITE;
			break;
		}

		return prot;
	}

	static void *iova_to_va(int dev_fd, uint64_t iova, uint64_t *len)
	{
		void *addr;
		size_t size;
		struct vduse_iotlb_entry entry;

		entry.start = iova;
		if (ioctl(dev_fd, VDUSE_IOTLB_GET_ENTRY, &entry))
			return NULL;

		size = entry.last - entry.start + 1;
		*len = entry.last - iova + 1;
		addr = mmap(0, size, perm_to_prot(entry.perm), MAP_SHARED,
			    entry.fd, entry.offset);

		if (addr == MAP_FAILED)
			return NULL;

		/* do something to cache this iova region */

		return addr + iova - entry.start;
	}

Besides, the following ioctls on the VDUSE device file are provided to support
interrupt injection and setting up eventfd for virtqueue kicks:

- VDUSE_VQ_SETUP_KICKFD: set the kickfd for virtqueue, this eventfd is used
  by VDUSE kernel module to notify userspace to consume the vring.

- VDUSE_INJECT_VQ_IRQ: inject an interrupt for specific virtqueue

- VDUSE_INJECT_CONFIG_IRQ: inject a config interrupt

Register VDUSE device on vDPA bus
---------------------------------
In order to make the VDUSE device work, administrator needs to use the management
API (netlink) to register it on vDPA bus. Some sample codes are show below:

.. code-block:: c

	static int netlink_add_vduse(const char *name, int device_id)
	{
		struct nl_sock *nlsock;
		struct nl_msg *msg;
		int famid;

		nlsock = nl_socket_alloc();
		if (!nlsock)
			return -ENOMEM;

		if (genl_connect(nlsock))
			goto free_sock;

		famid = genl_ctrl_resolve(nlsock, VDPA_GENL_NAME);
		if (famid < 0)
			goto close_sock;

		msg = nlmsg_alloc();
		if (!msg)
			goto close_sock;

		if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, famid, 0, 0,
		    VDPA_CMD_DEV_NEW, 0))
			goto nla_put_failure;

		NLA_PUT_STRING(msg, VDPA_ATTR_DEV_NAME, name);
		NLA_PUT_STRING(msg, VDPA_ATTR_MGMTDEV_DEV_NAME, "vduse");
		NLA_PUT_U32(msg, VDPA_ATTR_DEV_ID, device_id);

		if (nl_send_sync(nlsock, msg))
			goto close_sock;

		nl_close(nlsock);
		nl_socket_free(nlsock);

		return 0;
	nla_put_failure:
		nlmsg_free(msg);
	close_sock:
		nl_close(nlsock);
	free_sock:
		nl_socket_free(nlsock);
		return -1;
	}

MMU-based IOMMU Driver
----------------------
VDUSE framework implements an MMU-based on-chip IOMMU driver to support
mapping the kernel DMA buffer into the userspace iova region dynamically.
This is mainly designed for virtio-vdpa case (kernel virtio drivers).

The basic idea behind this driver is treating MMU (VA->PA) as IOMMU (IOVA->PA).
The driver will set up MMU mapping instead of IOMMU mapping for the DMA transfer
so that the userspace process is able to use its virtual address to access
the DMA buffer in kernel.

And to avoid security issue, a bounce-buffering mechanism is introduced to
prevent userspace accessing the original buffer directly which may contain other
kernel data. During the mapping, unmapping, the driver will copy the data from
the original buffer to the bounce buffer and back, depending on the direction of
the transfer. And the bounce-buffer addresses will be mapped into the user address
space instead of the original one.
