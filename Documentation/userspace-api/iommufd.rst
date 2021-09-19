.. SPDX-License-Identifier: GPL-2.0
.. iommu:

===================
IOMMU Userspace API
===================

Direct device access from userspace has been a crtical feature in
high performance computing and virtualization usages. Linux now
includes multiple device-passthrough frameworks (e.g. VFIO and vDPA)
to manage secure device access from the userspace. One critical
task of those frameworks is to put the assigned device in a secure,
IOMMU-protected context so the device is prevented from doing harm
to the rest of the system.

Currently those frameworks implement their own logic for managing
I/O page tables to isolate user-initiated DMAs. This doesn't scale
to support many new IOMMU features, such as PASID-granular DMA
remapping, nested translation, I/O page fault, IOMMU dirty bit, etc.

The /dev/iommu framework provides an unified interface for managing
I/O page tables for passthrough devices. Existing passthrough
frameworks are expected to use this interface instead of continuing
their ad-hoc implementations.

IOMMUFDs, IOASIDs, Devices and Groups
-------------------------------------

The core concepts in /dev/iommu are IOMMUFDs and IOASIDs. IOMMUFD (by
opening /dev/iommu) is the container holding multiple I/O address
spaces for a user, while IOASID is the fd-local software handle
representing an I/O address space and associated with a single I/O
page table. User manages those address spaces through fd operations,
e.g. by using vfio type1v2 mapping semantics to manage respective
I/O page tables.

IOASID is comparable to the conatiner concept in VFIO. The latter
is also associated to a single I/O address space. A main difference
between them is that multiple IOASIDs in the same IOMMUFD can be
nested together (not supported yet) to allow centralized accounting
of locked pages, while multiple containers are disconnected thus
duplicated accounting is incurred. Typically one IOMMUFD is
sufficient for all intended IOMMU usages for a user.

An I/O address space takes effect in the IOMMU only after it is
attached by a device. One I/O address space can be attached by
multiple devices. One device can be only attached to a single I/O
address space at this point (on par with current vfio behavior).

Device must be bound to an iommufd before the attach operation can
be conducted. The binding operation builds the connection between
the devicefd (opened via device-passthrough framework) and IOMMUFD.
IOMMU-protected security context is esbliashed when the binding
operation is completed. The passthrough framework must block user
access to the assigned device until bind() returns success.

The entire /dev/iommu framework adopts a device-centric model w/o
carrying any container/group legacy as current vfio does. However
the group is the minimum granularity that must be used to ensure
secure user access (refer to vfio.rst). This framework relies on
the IOMMU core layer to map device-centric model into group-granular
isolation.

Managing I/O Address Spaces
---------------------------

When creating an I/O address space (by allocating IOASID), the user
must specify the type of underlying I/O page table. Currently only
one type (kernel-managed) is supported. In the future other types
will be introduced, e.g. to support user-managed I/O page table or
a shared I/O page table which is managed by another kernel sub-
system (mm, ept, etc.). Kernel-managed I/O page table is currently
managed via vfio type1v2 equivalent mapping semantics.

The user also needs to specify the format of the I/O page table
when allocating an IOASID. The format must be compatible to the
attached devices (or more specifically to the IOMMU which serves
the DMA from the attached devices). User can query the device IOMMU
format via IOMMUFD once a device is successfully bound. Attaching a
device to an IOASID with incompatible format is simply rejected.

Currently no-snoop DMA is not supported yet. This implies that
IOASID must be created in an enforce-snoop format and only devices
which can be forced to snoop cache by IOMMU are allowed to be
attached to IOASID. The user should check uAPI extension and get
device info via IOMMUFD to handle such restriction.

Usage Example
-------------

Assume user wants to access PCI device 0000:06:0d.0, which is
exposed under the new /dev/vfio/devices directory by VFIO:

	/* Open device-centric interface and /dev/iommu interface */
	device_fd = open("/dev/vfio/devices/0000:06:0d.0", O_RDWR);
	iommu_fd = open("/dev/iommu", O_RDWR);

	/* Bind device to IOMMUFD */
	bind_data = { .iommu_fd = iommu_fd, .dev_cookie = cookie };
	ioctl(device_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind_data);

	/* Query per-device IOMMU capability/format */
	info = { .dev_cookie = cookie, };
	ioctl(iommu_fd, IOMMU_DEVICE_GET_INFO, &info);

	if (!(info.flags & IOMMU_DEVICE_INFO_ENFORCE_SNOOP)) {
		if (!ioctl(iommu_fd, IOMMU_CHECK_EXTENSION,
				EXT_DMA_NO_SNOOP))
			/* No support of no-snoop DMA */
	}

	if (!ioctl(iommu_fd, IOMMU_CHECK_EXTENSION, EXT_MAP_TYPE1V2))
		/* No support of vfio type1v2 mapping semantics */

	/* Decides IOASID alloc fields based on info */
	alloc_data = { .type = IOMMU_IOASID_TYPE_KERNEL,
		       .flags = IOMMU_IOASID_ENFORCE_SNOOP,
		       .addr_width = info.addr_width, };

	/* Allocate IOASID */
	gpa_ioasid = ioctl(iommu_fd, IOMMU_IOASID_ALLOC, &alloc_data);

	/* Attach device to an IOASID */
	at_data = { .iommu_fd = iommu_fd; .ioasid = gpa_ioasid};
	ioctl(device_fd, VFIO_DEVICE_ATTACH_IOASID, &at_data);

	/* Setup GPA mapping [0 - 1GB] */
	dma_map = {
		.ioasid	= gpa_ioasid,
		.data {
			.flags  = R/W		/* permission */
			.iova	= 0,		/* GPA */
			.vaddr	= 0x40000000,	/* HVA */
			.size	= 1GB,
		},
	};
	ioctl(iommu_fd, IOMMU_MAP_DMA, &dma_map);

	/* DMA */

	/* Unmap GPA mapping [0 - 1GB] */
	dma_unmap = {
		.ioasid	= gpa_ioasid,
		.data {
			.iova	= 0,		/* GPA */
			.size	= 1GB,
		},
	};
	ioctl(iommu_fd, IOMMU_UNMAP_DMA, &dma_unmap);

	/* Detach device from an IOASID */
	dt_data = { .iommu_fd = iommu_fd; .ioasid = gpa_ioasid};
	ioctl(device_fd, VFIO_DEVICE_DETACH_IOASID, &dt_data);

	/* Free IOASID */
	ioctl(iommu_fd, IOMMU_IOASID_FREE, gpa_ioasid);

	close(device_fd);
	close(iommu_fd);

API for device-passthrough frameworks
-------------------------------------

iommufd binding and IOASID attach/detach are initiated via the device-
passthrough framework uAPI.

When a binding operation is requested by the user, the passthrough
framework should call iommufd_bind_device(). When the device fd is
closed by the user, iommufd_unbind_device() should be called
automatically::

	struct iommufd_device *
	iommufd_bind_device(int fd, struct device *dev,
			   u64 dev_cookie);
	void iommufd_unbind_device(struct iommufd_device *idev);

IOASID attach/detach operations are per iommufd_device which is
returned by iommufd_bind_device():

	int iommufd_device_attach_ioasid(struct iommufd_device *idev,
					int ioasid);
	void iommufd_device_detach_ioasid(struct iommufd_device *idev,
					int ioasid);
