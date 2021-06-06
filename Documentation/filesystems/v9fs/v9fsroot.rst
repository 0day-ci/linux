.. SPDX-License-Identifier: GPL-2.0

==========================================
Mounting root file system via v9fs (9p.ko)
==========================================

:Author: Changbin Du <changbin.du@gmail.com>

The CONFIG_9P_FS_ROOT option enables experimental root file system
support for v9fs.

It introduces a new kernel command-line option called 'v9fsroot='
which will tell the kernel to mount the root file system by
utilizing the 9p protocol.


Kernel command line
===================

::

    root=/dev/v9fs

This is just a virtual device that basically tells the kernel to mount
the root file system via 9p protocol.

::

    v9fsroot=<path>[,options]

Enables the kernel to mount the root file system via 9p specified in this
option.

path
	Could be a remote file server, Plan 9 From User Space applications
	or mount tag of virtio transport.

options
	Optional mount options.

Examples
========
Test it under QEMU on a kernel built with CONFIG_9P_FS_ROOT and
CONFIG_IP_PNP options enabled::

    # qemu-system-x86_64 -enable-kvm -cpu host -m 1024 \
    -virtfs local,path=$rootfs_dir,mount_tag=r,security_model=passthrough,id=r \
    -kernel /path/to/linux/arch/x86/boot/bzImage -nographic \
    -append "root=/dev/v9fs v9fsroot=r,trans=virtio rw console=ttyS0 3"

The above example mounts v9fs with tag 'r' as rootfs in qemu guest via
virtio transport.
