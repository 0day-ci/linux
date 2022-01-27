.. SPDX-License-Identifier: GPL-2.0

======================
Memory Protection Keys
======================

Memory Protection Keys provide a mechanism for enforcing page-based
protections, but without requiring modification of the page tables when an
application changes protection domains.

PKeys Userspace (PKU) is a feature which is found on Intel's Skylake "Scalable
Processor" Server CPUs and later.  And it will be available in future
non-server Intel parts and future AMD processors.

Protection Keys for Supervisor pages (PKS) is available in the SDM since May
2020.

pkeys work by dedicating 4 previously Reserved bits in each page table entry to
a "protection key", giving 16 possible keys.

Protections for each key are defined with a per-CPU user-accessible register
(PKRU).  Each of these is a 32-bit register storing two bits (Access Disable
and Write Disable) for each of 16 keys.

Being a CPU register, PKRU is inherently thread-local, potentially giving each
thread a different set of protections from every other thread.

For Userspace (PKU), there are two instructions (RDPKRU/WRPKRU) for reading and
writing to the register.

For Supervisor (PKS), the register (MSR_IA32_PKRS) is accessible only to the
kernel through rdmsr and wrmsr.

The feature is only available in 64-bit mode, even though there is
theoretically space in the PAE PTEs.  These permissions are enforced on data
access only and have no effect on instruction fetches.



Syscalls for user space keys
============================

There are 3 system calls which directly interact with pkeys::

	int pkey_alloc(unsigned long flags, unsigned long init_access_rights)
	int pkey_free(int pkey);
	int pkey_mprotect(unsigned long start, size_t len,
			  unsigned long prot, int pkey);

Before a pkey can be used, it must first be allocated with
pkey_alloc().  An application calls the WRPKRU instruction
directly in order to change access permissions to memory covered
with a key.  In this example WRPKRU is wrapped by a C function
called pkey_set().
::

	int real_prot = PROT_READ|PROT_WRITE;
	pkey = pkey_alloc(0, PKEY_DISABLE_WRITE);
	ptr = mmap(NULL, PAGE_SIZE, PROT_NONE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	ret = pkey_mprotect(ptr, PAGE_SIZE, real_prot, pkey);
	... application runs here

Now, if the application needs to update the data at 'ptr', it can
gain access, do the update, then remove its write access::

	pkey_set(pkey, 0); // clear PKEY_DISABLE_WRITE
	*ptr = foo; // assign something
	pkey_set(pkey, PKEY_DISABLE_WRITE); // set PKEY_DISABLE_WRITE again

Now when it frees the memory, it will also free the pkey since it
is no longer in use::

	munmap(ptr, PAGE_SIZE);
	pkey_free(pkey);

.. note:: pkey_set() is a wrapper for the RDPKRU and WRPKRU instructions.
          An example implementation can be found in
          tools/testing/selftests/x86/protection_keys.c.

Behavior
========

The kernel attempts to make protection keys consistent with the
behavior of a plain mprotect().  For instance if you do this::

	mprotect(ptr, size, PROT_NONE);
	something(ptr);

you can expect the same effects with protection keys when doing this::

	pkey = pkey_alloc(0, PKEY_DISABLE_WRITE | PKEY_DISABLE_READ);
	pkey_mprotect(ptr, size, PROT_READ|PROT_WRITE, pkey);
	something(ptr);

That should be true whether something() is a direct access to 'ptr'
like::

	*ptr = foo;

or when the kernel does the access on the application's behalf like
with a read()::

	read(fd, ptr, 1);

The kernel will send a SIGSEGV in both cases, but si_code will be set
to SEGV_PKERR when violating protection keys versus SEGV_ACCERR when
the plain mprotect() permissions are violated.


Kernel API for PKS support
==========================

Overview
--------

Similar to user space pkeys, supervisor pkeys allow additional protections to
be defined for a supervisor mappings.  Unlike user space pkeys, violations of
these protections result in a kernel oops unless a PKS fault handler is
provided which handles the fault.

Supervisor Memory Protection Keys (PKS) is a feature which is found on Intel's
Sapphire Rapids (and later) "Scalable Processor" Server CPUs.  It will also be
available in future non-server Intel parts.

Also qemu has support as well: https://www.qemu.org/2021/04/30/qemu-6-0-0/

Kconfig
-------
Kernel users intending to use PKS support should depend on
ARCH_HAS_SUPERVISOR_PKEYS, and select ARCH_ENABLE_SUPERVISOR_PKEYS to turn on
this support within the core.

PKS Key Allocation
------------------
.. kernel-doc:: include/linux/pks-keys.h
        :doc: PKS_KEY_ALLOCATION

Adding Pages to a PKey protected domain
---------------------------------------

.. kernel-doc:: arch/x86/include/asm/pgtable_types.h
        :doc: PKS_KEY_ASSIGNMENT


Changing permissions of individual keys
---------------------------------------

.. kernel-doc:: include/linux/pks-keys.h
        :identifiers: pks_mk_readwrite pks_mk_noaccess

.. kernel-doc:: arch/x86/mm/pkeys.c
        :identifiers: pks_update_exception

.. kernel-doc:: arch/x86/mm/pkeys.c
        :identifiers: pks_available

Overriding Default Fault Behavior
---------------------------------

.. kernel-doc:: arch/x86/mm/pkeys.c
        :doc: DEFINE_PKS_FAULT_CALLBACK

MSR details
-----------

It should be noted that the underlying WRMSR(MSR_IA32_PKRS) is not serializing
but still maintains ordering properties similar to WRPKRU.  Thus it is safe to
immediately use a mapping when the pks_mk*() functions return.

Older versions of the SDM on PKRS may be wrong with regard to this
serialization.  The text should be the same as that of WRPKRU.  From the WRPKRU
text:

	WRPKRU will never execute transiently. Memory accesses
	affected by PKRU register will not execute (even transiently)
	until all prior executions of WRPKRU have completed execution
	and updated the PKRU register.

Testing
-------

Example code can be found in lib/pks/pks_test.c

.. kernel-doc:: lib/pks/pks_test.c
        :doc: PKS_TEST
