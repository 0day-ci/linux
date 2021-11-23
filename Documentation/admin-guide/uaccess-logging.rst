.. SPDX-License-Identifier: GPL-2.0

===============
Uaccess Logging
===============

Background
----------

Userspace tools such as sanitizers (ASan, MSan, HWASan) and tools
making use of the ARM Memory Tagging Extension (MTE) need to
monitor all memory accesses in a program so that they can detect
memory errors. Furthermore, fuzzing tools such as syzkaller need to
monitor all memory accesses so that they know which parts of memory
to fuzz. For accesses made purely in userspace, this is achieved
via compiler instrumentation, or for MTE, via direct hardware
support. However, accesses made by the kernel on behalf of the user
program via syscalls (i.e. uaccesses) are normally invisible to
these tools.

Traditionally, the sanitizers have handled this by interposing the libc
syscall stubs with a wrapper that checks the memory based on what we
believe the uaccesses will be. However, this creates a maintenance
burden: each syscall must be annotated with its uaccesses in order
to be recognized by the sanitizer, and these annotations must be
continuously updated as the kernel changes.

The kernel's uaccess logging feature provides userspace tools with
the address and size of each userspace access, thereby allowing these
tools to report memory errors involving these accesses without needing
annotations for every syscall.

By relying on the kernel's actual uaccesses, rather than a
reimplementation of them, the userspace memory safety tools may
play a dual role of verifying the validity of kernel accesses. Even
a sanitizer whose syscall wrappers have complete knowledge of the
kernel's intended API may vary from the kernel's actual uaccesses due
to kernel bugs. A sanitizer with knowledge of the kernel's actual
uaccesses may produce more accurate error reports that reveal such
bugs. For example, a kernel that accesses more memory than expected
by the userspace program could indicate that either userspace or the
kernel has the wrong idea about which kernel functionality is being
requested -- either way, there is a bug.

Interface
---------

The feature may be used via the following prctl:

.. code-block:: c

  uint64_t addr = 0; /* Generally will be a TLS slot or equivalent */
  prctl(PR_SET_UACCESS_DESCRIPTOR_ADDR_ADDR, &addr, 0, 0, 0);

Supplying a non-zero address as the second argument to ``prctl``
will cause the kernel to read an address (referred to as the *uaccess
descriptor address*) from that address on each kernel entry.

When entering the kernel with a non-zero uaccess descriptor address
to handle a syscall, the kernel will read a data structure of type
``struct uaccess_descriptor`` from the uaccess descriptor address,
which is defined as follows:

.. code-block:: c

  struct uaccess_descriptor {
    uint64_t addr, size;
  };

This data structure contains the address and size (in array elements)
of a *uaccess buffer*, which is an array of data structures of type
``struct uaccess_buffer_entry``. Before returning to userspace, the
kernel will log information about uaccesses to sequential entries
in the uaccess buffer. It will also store ``NULL`` to the uaccess
descriptor address, and store the address and size of the unused
portion of the uaccess buffer to the uaccess descriptor.

The format of a uaccess buffer entry is defined as follows:

.. code-block:: c

  struct uaccess_buffer_entry {
    uint64_t addr, size, flags;
  };

The meaning of ``addr`` and ``size`` should be obvious. On arm64,
tag bits are preserved in the ``addr`` field. There is currently
one flag bit assignment for the ``flags`` field:

.. code-block:: c

  #define UACCESS_BUFFER_FLAG_WRITE 1

This flag is set if the access was a write, or clear if it was a
read. The meaning of all other flag bits is reserved.

When entering the kernel with a non-zero uaccess descriptor
address for a reason other than a syscall (for example, when
IPI'd due to an incoming asynchronous signal), any signals other
than ``SIGKILL`` and ``SIGSTOP`` are masked as if by calling
``sigprocmask(SIG_SETMASK, set, NULL)`` where ``set`` has been
initialized with ``sigfillset(set)``. This is to prevent incoming
signals from interfering with uaccess logging.

Example
-------

Here is an example of a code snippet that will enumerate the accesses
performed by a ``uname(2)`` syscall:

.. code-block:: c

  struct uaccess_buffer_entry entries[64];
  struct uaccess_descriptor desc;
  uint64_t desc_addr = 0;
  prctl(PR_SET_UACCESS_DESCRIPTOR_ADDR_ADDR, &desc_addr, 0, 0, 0);

  desc.addr = (uint64_t)&entries;
  desc.size = 64;
  desc_addr = (uint64_t)&desc;

  struct utsname un;
  uname(&un);

  struct uaccess_buffer_entry* entries_end = (struct uaccess_buffer_entry*)desc.addr;
  for (struct uaccess_buffer_entry* entry = entries; entry != entries_end; ++entry) {
    printf("%s at 0x%lx size 0x%lx\n", entry->flags & UACCESS_BUFFER_FLAG_WRITE ? "WRITE" : "READ",
           (unsigned long)entry->addr, (unsigned long)entry->size);
  }

Limitations
-----------

This feature is currently only supported on the arm64, s390 and x86
architectures.

Uaccess buffers are a "best-effort" mechanism for logging uaccesses. Of
course, not all of the accesses may fit in the buffer, but aside from
that, not all internal kernel APIs that access userspace memory are
covered. Therefore, userspace programs should tolerate unreported
accesses.

On the other hand, the kernel guarantees that it will not
(intentionally) report accessing more data than it is specified
to read. For example, if the kernel implements a syscall that is
specified to read a data structure of size ``N`` bytes by first
reading a page's worth of data and then only using the first ``N``
bytes from it, the kernel will either report reading ``N`` bytes or
not report the access at all.
