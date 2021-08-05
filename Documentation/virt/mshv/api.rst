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

