.. SPDX-License-Identifier: GPL-2.0

==========
efi_secret
==========

This document describes how Confidential Computing secret injection is handled
from the firmware to the operating system.


Introduction
============

Confidential Computing (coco) hardware such as AMD SEV (Secure Encrypted
Virtualization) allows guest owners to inject secrets into the VMs
memory without the host/hypervisor being able to read them.  In SEV,
secret injection is performed early in the VM launch process, before the
guest starts running.

The efi_secret kernel module allows userspace applications to access these
secrets via securityfs.


Secret data flow
================

The guest firmware may reserve a designated memory area for secret injection,
and publish its location (base GPA and length) in the EFI configuration table
under a ``LINUX_EFI_COCO_SECRET_AREA_GUID`` entry
(``adf956ad-e98c-484c-ae11-b51c7d336447``).  This memory area should be marked
by the firmware as ``EFI_RESERVED_TYPE``, and therefore the kernel should not
be use it for its own purposes.

During the VM's launch, the virtual machine manager may inject a secret to that
area.  In AMD SEV and SEV-ES this is performed using the
``KVM_SEV_LAUNCH_SECRET`` command (see [amd-mem-enc]_).  The strucutre of the
injected Guest Owner secret data should be a GUIDed table of secret values; the
binary format is described in ``drivers/virt/coco/efi_secret/efi_secret.c``
under "Structure of the EFI secret area".

On kernel start, the kernel's EFI driver saves the location of the secret
memory (taken from the EFI configuration table) in the ``efi.coco_secret``
field.

When a userspace application needs to access the secrets inside the guest VM,
it loads the efi_secret kernel module (``CONFIG_EFI_SECRET=m``) which exposes
the secrets via securityfs.  The details of the efi_secret filesystem interface
are in [efi-secret-abi]_.



Application usage example
=========================

Consider a guest performing computations on encrypted files.  The Guest Owner
provides the decryption key (= secret) using the secret injection mechanism.
The guest application reads the secret from the efi_secret filesystem and
proceeds to decrypt the files into memory and then performs the needed
computations on the content.

In this example, the host can't read the files from the disk image
because they are encrypted.  Host can't read the decryption key because
it is passed using the secret injection mechanism (= secure channel).
Host can't read the decrypted content from memory because it's a
confidential (memory-encrypted) guest.

Here is a simple example for usage of the efi_secret module in a guest
to which an EFI secret area with 4 secrets was injected during launch::

	# modprobe efi_secret
	# ls -la /sys/kernel/security/coco/efi_secret
	total 0
	drwxr-xr-x 2 root root 0 Jun 28 11:54 .
	drwxr-xr-x 3 root root 0 Jun 28 11:54 ..
	-r--r----- 1 root root 0 Jun 28 11:54 736870e5-84f0-4973-92ec-06879ce3da0b
	-r--r----- 1 root root 0 Jun 28 11:54 83c83f7f-1356-4975-8b7e-d3a0b54312c6
	-r--r----- 1 root root 0 Jun 28 11:54 9553f55d-3da2-43ee-ab5d-ff17f78864d2
	-r--r----- 1 root root 0 Jun 28 11:54 e6f5a162-d67f-4750-a67c-5d065f2a9910

	# xxd /sys/kernel/security/coco/efi_secret/e6f5a162-d67f-4750-a67c-5d065f2a9910
	00000000: 7468 6573 652d 6172 652d 7468 652d 6b61  these-are-the-ka
	00000010: 7461 2d73 6563 7265 7473 0001 0203 0405  ta-secrets......
	00000020: 0607                                     ..

	# rm /sys/kernel/security/coco/efi_secret/e6f5a162-d67f-4750-a67c-5d065f2a9910

	# ls -la /sys/kernel/security/coco/efi_secret
	total 0
	drwxr-xr-x 2 root root 0 Jun 28 11:55 .
	drwxr-xr-x 3 root root 0 Jun 28 11:54 ..
	-r--r----- 1 root root 0 Jun 28 11:54 736870e5-84f0-4973-92ec-06879ce3da0b
	-r--r----- 1 root root 0 Jun 28 11:54 83c83f7f-1356-4975-8b7e-d3a0b54312c6
	-r--r----- 1 root root 0 Jun 28 11:54 9553f55d-3da2-43ee-ab5d-ff17f78864d2


References
==========

See [sev-api-spec]_ for more info regarding SEV ``LAUNCH_SECRET`` operation.

.. [amd-mem-enc] :ref:`Documentation/virt/kvm/amd-memory-encryption <amdmemenc>`
.. [efi-secret-abi] :ref:`Documentation/ABI/testing/securityfs-coco-efi_secret <efisecret>`
.. [sev-api-spec] https://www.amd.com/system/files/TechDocs/55766_SEV-KM_API_Specification.pdf
