.. SPDX-License-Identifier: GPL-2.0

=====================================================
Microsoft Hypervisor Root Partition API Documentation
=====================================================

1. Overview
===========

This document describes APIs for creating and managing guest virtual machines
when running Linux as the root partition on the Microsoft Hypervisor.

Note that this API is not yet stable!

2. Glossary/Terms
=================

hv
--
Short for Hyper-V. This name is used in the kernel to describe interfaces to
the Microsoft Hypervisor.

mshv
----
Short for Microsoft Hypervisor. This is the name of the userland API module
described in this document.

Partition
---------
A virtual machine running on the Microsoft Hypervisor.

Root Partition
--------------
The partition that is created and assumes control when the machine boots. The
root partition can use mshv APIs to create guest partitions.

3. API description
==================

The module is named mshv and can be configured with CONFIG_HYPERV_ROOT_API.

The uapi header files you need are linux/mshv.h, asm/hyperv-tlfs.h, and
asm-generic/hyperv-tlfs.h.

Mshv is file descriptor-based, following a similar pattern to KVM.

To get a handle to the mshv driver, use open("/dev/mshv").

3.1 MSHV_CHECK_EXTENSION
------------------------
:Type: /dev/mshv ioctl
:Parameters: pointer to a u32
:Returns: 0 if extension unsupported, positive number if supported

This ioctl takes a single argument corresponding to an API extension to check
support for.

If the extension is supported, MSHV_CHECK_EXTENSION will return a positive
number. If not, it will return 0.

The first extension that can be checked is MSHV_CAP_CORE_API_STABLE. This
will be supported when the core API is stable.

3.2 MSHV_CREATE_PARTITION
-------------------------
:Type: /dev/mshv ioctl
:Parameters: struct mshv_create_partition
:Returns: partition file descriptor, or -1 on failure

This ioctl creates a guest partition, returning a file descriptor to use as a
handle for partition ioctls.

3.3 MSHV_MAP_GUEST_MEMORY and MSHV_UNMAP_GUEST_MEMORY
-----------------------------------------------------
:Type: partition ioctl
:Parameters: struct mshv_user_mem_region
:Returns: 0 on success

Create a mapping from memory in the user space of the calling process (running
in the root partition) to a region of guest physical memory in a guest partition.

Mappings must be disjoint from each other in both userspace and guest physical
address space.

Note: In the current implementation, this memory is pinned to real physical
memory to stop the pages being moved by Linux in the root partition,
and subsequently being clobbered by the hypervisor. So the region is backed
by real physical memory.

3.4 MSHV_CREATE_VP
------------------
:Type: partition ioctl
:Parameters: struct mshv_create_vp
:Returns: vp file descriptor, or -1 on failure

Create a virtual processor in a guest partition, returning a file descriptor to
represent the vp and perform ioctls on.

3.5 MSHV_GET_VP_REGISTERS and MSHV_SET_VP_REGISTERS
---------------------------------------------------
:Type: vp ioctl
:Parameters: struct mshv_vp_registers
:Returns: 0 on success

Get/set vp registers. See asm/hyperv-tlfs.h for the complete set of registers.
Includes general purpose platform registers, MSRs, and virtual registers that
are part of Microsoft Hypervisor platform and not directly exposed to the guest.

3.6 MSHV_RUN_VP
---------------
:Type: vp ioctl
:Parameters: struct hv_message
:Returns: 0 on success

Run the vp, returning when it triggers an intercept, or if the calling thread
is interrupted by a signal. In this case errno will be set to EINTR.

On return, the vp will be suspended.
This ioctl will fail on any vp that's already running (not suspended).

Information about the intercept is returned in the hv_message struct.

3.7 MSHV_INSTALL_INTERCEPT
--------------------------
:Type: partition ioctl
:Parameters: struct mshv_install_intercept
:Returns: 0 on success

Enable and configure different types of intercepts. Intercepts are events in a
guest partition that will suspend the guest vp and send a message to the root
partition (returned from MSHV_RUN_VP).

3.8 MSHV_ASSERT_INTERRUPT
--------------------------
:Type: partition ioctl
:Parameters: struct mshv_assert_interrupt
:Returns: 0 on success

Assert interrupts in partitions that use Microsoft Hypervisor's internal
emulated LAPIC. This must be enabled on partition creation with the flag:
HV_PARTITION_CREATION_FLAG_LAPIC_ENABLED

3.9 MSHV_GET_VP_STATE and MSHV_SET_VP_STATE
--------------------------
:Type: vp ioctl
:Parameters: struct mshv_vp_state
:Returns: 0 on success

Get/set various vp state. Currently these can be used to get and set
emulated LAPIC state, and xsave data.

3.10 mmap(vp)
-------------
:Type: vp mmap
:Parameters: offset should be HV_VP_MMAP_REGISTERS_OFFSET
:Returns: 0 on success

Maps a page into userspace that can be used to get and set common registers
while the vp is suspended.
The page is laid out in struct hv_vp_register_page in asm/hyperv-tlfs.h.

3.11 MSHV_SET_PARTITION_PROPERTY and MSHV_GET_PARTITION_PROPERTY
----------------------------------------------------------------
:Type: partition ioctl
:Parameters: struct mshv_partition_property
:Returns: 0 on success

Can be used to get/set various properties of a partition.

Some properties can only be set at partition creation. For these, there are
parameters in MSHV_CREATE_PARTITION.


