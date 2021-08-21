=======
NAX LSM
=======

:Author: Igor Zhbanov

NAX (No Anonymous Execution) is a Linux Security Module that extends DAC
by making impossible to make anonymous and modified pages executable for
processes. The module intercepts anonymous executable pages created with
mmap() and mprotect() system calls.

To select it at boot time, add ``nax`` to ``security`` kernel command-line
parameter.

The following sysctl parameters are available:

* ``kernel.nax.check_all``:
 - 0: Check all processes.
 - 1: Check only privileged processes. The privileged process is a process
      for which any of the following is true:
      - ``uid  == 0``
      - ``euid == 0``
      - ``suid == 0``
      - ``cap_effective`` has any capability except for the ones allowed
        in ``kernel.nax.allowed_caps``
      - ``cap_permitted`` has any capability except for the ones allowed
        in ``kernel.nax.allowed_caps``

 Checking of uid/euid/suid is important because a process may call seteuid(0)
 to gain privileges (if SECURE_NO_SETUID_FIXUP secure bit is not set).

* ``kernel.nax.allowed_caps``:

 Hexadecimal number representing the set of capabilities a non-root
 process can possess without being considered "privileged" by NAX LSM.

 For the meaning of the capabilities bits and their value, please check
 ``include/uapi/linux/capability.h`` and ``capabilities(7)`` manual page.

 For example, ``CAP_SYS_PTRACE`` has a number 19. Therefore, to add it to
 allowed capabilities list, we need to set 19'th bit (2^19 or 1 << 19)
 or 80000 in hexadecimal form. Capabilities can be bitwise ORed.

* ``kernel.nax.mode``:

 - 0: Only log errors (when enabled by ``kernel.nax.quiet``) (default mode)
 - 1: Forbid unsafe pages mappings (and log when enabled)
 - 2: Kill the violating process (and log when enabled)

* ``kernel.nax.quiet``:

 - 0: Log violations (default)
 - 1: Be quiet

* ``kernel.nax.locked``:

 - 0: Changing of the module's sysctl parameters is allowed
 - 1: Further changing of the module's sysctl parameters is forbidden

 Setting this parameter to ``1`` after initial setup during the system boot
 will prevent the module disabling at the later time.

There are matching kernel command-line parameters (with the same values):

- ``nax_allowed_caps``
- ``nax_check_all``
- ``nax_mode``
- ``nax_quiet``
- ``nax_locked``

The ``nax_locked`` command-line parameter must be specified last to avoid
premature setting locking.
