.. SPDX-License-Identifier: GPL-2.0

Integrity Policy Enforcement (IPE)
==================================

.. NOTE::

   This is the documentation for admins, system builders, or individuals
   attempting to use IPE, without understanding all of its internal systems.
   If you're looking for documentation to extend IPE, understand the design
   decisions behind IPE, or are just curious about the internals, please
   see :ref:`Documentation/security/ipe.rst`

Overview
--------

IPE is a Linux Security Module which imposes a complimentary model
of mandatory access control to other LSMs. Whereas the existing LSMs
impose access control based on labels or paths, IPE imposes access
control based on the trust of the resource. Simply put, IPE
or restricts access to a resource based on the trust of said resource.

Trust requirements are established via IPE's policy, sourcing multiple
different implementations within the kernel to build a cohesive trust
model, based on how the system was built.

Trust vs Integrity
------------------

Trust, with respect to computing, is a concept that designates a set
of entities who will endorse a set of resources as non-malicious.
Traditionally, this is done via signatures, which is the act of endorsing
a resource. Integrity, on the other hand, is the concept of ensuring that a
resource has not been modified since a point of time. This is typically done
through cryptography or signatures.

Trust and integrity are very closely tied together concepts, as integrity
is the way you can prove trust for a resource; otherwise it could have
been modified by an entity who is untrusted.

IPE provides a way for a user to express trust of resources, by using
pre-existing systems which provide the integrity half of the equation.

Use Cases
---------

IPE works best in fixed-function devices: Devices in which their purpose
is clearly defined and not supposed to be changed (e.g. network firewall
device in a data center, an IoT device, etcetera), where all software and
configuration is built and provisioned by the system owner.

IPE is a long-way off for use in general-purpose computing:
the Linux community as a whole tends to follow a decentralized trust
model, known as the Web of Trust, which IPE has no support for as of yet.
Instead, IPE supports the PKI Trust Model, which generally designates a
set of entities that provide a measure absolute trust.

Additionally, while most packages are signed today, the files inside
the packages (for instance, the executables), tend to be unsigned. This
makes it difficult to utilize IPE in systems where a package manager is
expected to be functional, without major changes to the package manager
and ecosystem behind it.

For the highest level of security, platform firmware should verify the
the kernel and optionally the root filesystem (for example, via U-Boot
verified boot). This forms a chain of trust from the hardware, ensuring
that every stage of the system is trusted.

Known Gaps
----------

IPE cannot verify the integrity of anonymous executable memory, such as
the trampolines created by gcc closures and libffi (<3.4.2), or JIT'd code.
Unfortunately, as this is dynamically generated code, there is no way
for IPE to ensure the integrity of this code to form a trust basis. In all
cases, the return result for these operations will be whatever the admin
configures the DEFAULT action for "EXECUTE".

IPE cannot verify the integrity of interpreted languages' programs when
these scripts invoked via ``<interpreter> <file>``. This is because the
way interpreters execute these files, the scripts themselves are not
evaluated as executable code through one of IPE's hooks. Interpreters
can be enlightened to the usage of IPE by trying to mmap a file into
executable memory (+X), after opening the file and responding to the
error code appropriately. This also applies to included files, or high
value files, such as configuration files of critical system components [#]_.

.. [#] Mickaël Salaün's `trusted_for patchset <https://lore.kernel.org/all/20211008104840.1733385-1-mic@digikod.net/>`_
   can be used to leverage this.

Threat Model
------------

The threat type addressed by IPE is tampering of executable user-land
code beyond the initially booted kernel, and the initial verification of
kernel modules that are loaded in userland through ``modprobe`` or
``insmod``.

Tampering violates integrity, and being unable to verify the integrity,
results in a lack of trust. IPE's role in mitigating this threat is to
verify the integrity (and authenticity) of all executable code and to
deny their use if they cannot be trusted (as integrity verification fails).
IPE generates audit logs which may be utilized to detect failures resulting
from failure to pass policy.

Tampering threat scenarios include modification or replacement of
executable code by a range of actors including:

-  Actors with physical access to the hardware
-  Actors with local network access to the system
-  Actors with access to the deployment system
-  Compromised internal systems under external control
-  Malicious end users of the system
-  Compromised end users of the system
-  Remote (external) compromise of the system

IPE does not mitigate threats arising from malicious authorized
developers, or compromised developer tools used by authorized
developers. Additionally, IPE draws hard security boundary between user
mode and kernel mode. As a result, IPE does not provide any protections
against a kernel level exploit, and a kernel-level exploit can disable
or tamper with IPE's protections.

Policy
------

IPE policy is a plain-text [#]_ policy composed of multiple statements
over several lines. There is one required line, at the top of the
policy, indicating the policy name, and the policy version, for
instance::

   policy_name="Ex Policy" policy_version=0.0.0

The policy name is a unique key identifying this policy in a human
readable name. This is used to create nodes under securityfs as well as
uniquely identify policies to deploy new policies vs update existing
policies.

The policy version indicates the current version of the policy (NOT the
policy syntax version). This is used to prevent rollback of policy to
potentially insecure previous versions of the policy.

The next portion of IPE policy, are rules. Rules are formed by key=value
pairs, known as properties. IPE rules require two properties: "action",
which determines what IPE does when it encounters a match against the
rule, and "op", which determines when that rule should be evaluated.
The ordering is significant, a rule must start with "op", and end with
"action". Thus, a minimal rule is::

   op=EXECUTE action=ALLOW

This example will allow any execution. Additional properties are used to
restrict attributes about the files being evaluated. These properties
are intended to be descriptions of systems within the kernel, that can
provide a measure of integrity verification, such that IPE can determine
the trust of the resource based on the "value" half of the property.

Rules are evaluated top-to-bottom. As a result, any revocation rules,
or denies should be placed early in the file to ensure that these rules
are evaluated before as rule with "action=ALLOW" is hit.

IPE policy is designed to be only forward compatible. Userspace can read
what the parser's current configuration (supported statements, properties,
etcetera) via reading the securityfs entry, 'ipe/config'

IPE policy supports comments. The character '#' will function as a
comment, ignoring all characters to the right of '#' until the newline.

The default behavior of IPE evaluations can also be expressed in policy,
through the ``DEFAULT`` statement. This can be done at a global level,
or a per-operation level::

   # Global
   DEFAULT action=ALLOW

   # Operation Specific
   DEFAULT op=EXECUTE action=ALLOW

A default must be set for all known operations in IPE. If you want to
preserve older policies being compatible with newer kernels that can introduce
new operations, please set a global default of 'ALLOW', and override the
defaults on a per-operation basis.

With configurable policy-based LSMs, there's several issues with
enforcing the configurable policies at startup, around reading and
parsing the policy:

1. The kernel *should* not read files from userland, so directly reading
   the policy file is prohibited.
2. The kernel command line has a character limit, and one kernel module
   should not reserve the entire character limit for its own
   configuration.
3. There are various boot loaders in the kernel ecosystem, so handing
   off a memory block would be costly to maintain.

As a result, IPE has addressed this problem through a concept of a "boot
policy". A boot policy is a minimal policy, compiled into the kernel.
This policy is intended to get the system to a state where userland is
setup and ready to receive commands, at which point a more complex
policy ("user policies") can be deployed via securityfs. The boot policy
can be specified via the Kconfig, ``SECURITY_IPE_BOOT_POLICY``, which
accepts a path to a plain-text version of the IPE policy to apply. This
policy will be compiled into the kernel. If not specified, IPE will be
disabled until a policy is deployed and activated through securityfs.

.. [#] Please see the :ref:`Documentation/security/ipe.rst` for more on this
   topic.

Deploying Policies
~~~~~~~~~~~~~~~~~~

User policies as explained above, are policies that are deployed from
userland, through securityfs. These policies are signed to enforce some
level of authorization of the policies (prohibiting an attacker from
gaining root, and deploying an "allow all" policy), through the PKCS#7
enveloped data format. These policies must be signed by a certificate
that chains to the ``SYSTEM_TRUSTED_KEYRING``. Through openssl, the
signing can be done via::

   openssl smime -sign -in "$MY_POLICY" -signer "$MY_CERTIFICATE" \
     -inkey "$MY_PRIVATE_KEY" -binary -outform der -noattr -nodetach \
     -out "$MY_POLICY.p7s"

Deploying the policies is done through securityfs, through the
``new_policy`` node. To deploy a policy, simply cat the file into the
securityfs node::

   cat "$MY_POLICY.p7s" > /sys/kernel/security/ipe/new_policy

Upon success, this will create one subdirectory under
``/sys/kernel/security/ipe/policies/``. The subdirectory will be the
``policy_name`` field of the policy deployed, so for the example above,
the directory will be ``/sys/kernel/security/ipe/policies/Ex\ Policy``.
Within this directory, there will be five files: ``pkcs7``, ``policy``,
``active``, ``update``, and ``delete``.

The ``pkcs7`` file is rw, reading will provide the raw PKCS#7 data that
was provided to the kernel, representing the policy. Writing, will
deploy an in-place policy update - if this policy is the currently
running policy, the new updated policy will replace it immediately upon
success. If the policy being read is the boot policy, when read, this
will return ENOENT.

The ``policy`` file is read only. Reading will provide the PKCS#7 inner
content of the policy, which will be the plain text policy.

The ``active`` file is used to set a policy as the currently active policy.
This file is rw, and accepts a value of ``"1"`` to set the policy as active.
Since only a single policy can be active at one time, all other policies
will be marked inactive. The policy being marked active must have a policy
version greater or equal to the currently-running version.

The ``update`` file is used to update a policy that is already present in
the kernel. This file is write-only and accepts a PKCS#7 signed policy.
One check will be performed on this policy: the policy_names must match
with the updated version and the existing version. If the policy being
updated is the active policy, the updated policy must have a policy version
greater or equal to the currently-running version.

The ``delete`` file is used to remove a policy that is no longer needed.
This file is write-only and accepts a value of ``"1"`` to delete the policy.
On deletion, the securityfs node representing the policy will be removed.
The policy that is currently active, cannot be deleted.

Similarly, the writes to both ``update`` and ``new_policy`` above will
result in an error upon syntactically invalid or untrusted policies.
It will also error if a policy already exists with the same ``policy_name``,
in the case of ``new_policy``.

Deploying these policies will *not* cause IPE to start enforcing this
policy. Once deployment is successful, a policy can be marked as active,
via ``/sys/kernel/security/ipe/$policy_name/active``. IPE will enforce
whatever policy is marked as active. For our example, we can activate
the ``Ex Policy`` via::

   echo "1" > "/sys/kernel/security/ipe/Ex Policy/active"

At which point, ``Ex Policy`` will now be the enforced policy on the
system.

IPE also provides a way to delete policies. This can be done via the
``delete`` securityfs node, ``/sys/kernel/security/ipe/$policy_name/delete``.
Writing ``1`` to that file will delete that node::

   echo "1" > "/sys/kernel/security/ipe/$policy_name/delete"

There is only one requirement to delete a policy:

1. The policy being deleted must not be the active policy.

.. NOTE::

   If a traditional MAC system is enabled (SELinux, apparmor, smack), all
   writes to ipe's securityfs nodes require ``CAP_MAC_ADMIN``.

Modes
~~~~~

IPE supports two modes of operation: permissive (similar to SELinux's
permissive mode) and enforce. Permissive mode performs the same checks
as enforce mode, and logs policy violations, but will not enforce the
policy. This allows users to test policies before enforcing them.

The default mode is enforce, and can be changed via the kernel command
line parameter ``ipe.enforce=(0|1)``, or the securityfs node
``/sys/kernel/security/ipe/enforce``.

.. NOTE::

   If a traditional MAC system is enabled (SELinux, apparmor, smack, etcetera),
   all writes to ipe's securityfs nodes require ``CAP_MAC_ADMIN``.

Audit Events
~~~~~~~~~~~~

Success Auditing
^^^^^^^^^^^^^^^^

IPE supports success auditing. When enabled, all events that pass IPE
policy and are not blocked will emit an audit event. This is disabled by
default, and can be enabled via the kernel command line
``ipe.success_audit=(0|1)`` or the securityfs node,
``/sys/kernel/security/ipe/success_audit``.

This is very noisy, as IPE will check every user-mode binary on the
system, but is useful for debugging policies.

.. NOTE::

   If a traditional MAC system is enabled (SELinux, apparmor, smack, etcetera),
   all writes to ipe's securityfs nodes require ``CAP_MAC_ADMIN``.

Properties
--------------

As explained above, IPE properties are ``key=value`` pairs expressed in
IPE policy. Two properties are built-into the policy parser: 'op' and
'action'. The other properties are determinstic attributes to express
across files. Currently those properties are: 'boot_verified',
'dmverity_signature', 'dmverity_roothash', 'fsverity_signature',
'fsverity_digest'. A description of all properties supported by IPE
are listed below:

op
~~

Indicates the operation for a rule to apply to. Must be in every rule,
as the first token. IPE supports the following operations:

Version 1
^^^^^^^^^

``EXECUTE``

   Pertains to any file attempting to be executed, or loaded as an
   executable.

``FIRMWARE``:

   Pertains to firmware being loaded via the firmware_class interface.
   This covers both the preallocated buffer and the firmware file
   itself.

``KMODULE``:

   Pertains to loading kernel modules via ``modprobe`` or ``insmod``.

``KEXEC_IMAGE``:

   Pertains to kernel images loading via ``kexec``.

``KEXEC_INITRAMFS``

   Pertains to initrd images loading via ``kexec --initrd``.

``POLICY``:

   Controls loading IMA policies through the
   ``/sys/kernel/security/ima/policy`` securityfs entry.

``X509_CERT``:

   Controls loading IMA certificates through the Kconfigs,
   ``CONFIG_IMA_X509_PATH`` and ``CONFIG_EVM_X509_PATH``.

``KERNEL_READ``:

   Short hand for all of the following: ``FIRMWARE``, ``KMODULE``,
   ``KEXEC_IMAGE``, ``KEXEC_INITRAMFS``, ``POLICY``, and ``X509_CERT``.

action
~~~~~~

Version 1
^^^^^^^^^

Determines what IPE should do when a rule matches. Must be in every
rule, as the final clause. Can be one of:

``ALLOW``:

   If the rule matches, explicitly allow access to the resource to proceed
   without executing any more rules.

``DENY``:

   If the rule matches, explicitly prohibit access to the resource to
   proceed without executing any more rules.

boot_verified
~~~~~~~~~~~~~

Version 1
^^^^^^^^^

This property can be utilized for authorization of the first super-block
that executes a file. This is almost always init. Typically this is used
for systems with an initramfs or other initial disk, where this is unmounted
before the system becomes available, and is not covered by any other property.
This property is controlled by the Kconfig, ``CONFIG_IPE_PROP_BOOT_VERIFIED``.
The format of this property is::

       boot_verified=(TRUE|FALSE)


.. WARNING::

  This property will trust any disk where the first execution evaluation
  occurs. If you do *NOT* have a startup disk that is unpacked and unmounted
  (like initramfs), then it will automatically trust the root filesystem and
  potentially overauthorize the entire disk.

dmverity_roothash
~~~~~~~~~~~~~~~~~

Version 1
^^^^^^^^^

This property can be utilized for authorization or revocation of
specific dm-verity volumes, identified via root hash. It has a
dependency on the DM_VERITY module. This property is controlled by the
Kconfig ``CONFIG_IPE_PROP_DM_VERITY_ROOTHASH``. The format of this property
is::

   dmverity_roothash=HashHexDigest

dmverity_signature
~~~~~~~~~~~~~~~~~~

Version 1
^^^^^^^^^

This property can be utilized for authorization of all dm-verity volumes
that have a signed roothash that chains to a keyring specified by dm-verity's
configuration, either the system trusted keyring, or the secondary keyring.
It has an additional dependency on the ``DM_VERITY_VERIFY_ROOTHASH_SIG``
Kconfig. This property is controlled by the Kconfig
``CONFIG_IPE_PROP_DM_VERITY_SIGNATURE``. The format of this property is::

   dmverity_signature=(TRUE|FALSE)

fsverity_digest
~~~~~~~~~~~~~~~

Version 1
^^^^^^^^^
This property can be utilized for authorization or revocation of
specific fsverity enabled file, identified via its fsverity digest,
which is the hash of a struct contains the file's roothash and hashing
parameters. It has a dependency on the FS_VERITY module.
This property is controlled by the Kconfig
``CONFIG_IPE_PROP_FS_VERITY_DIGEST``. The format of this property is::

   fsverity_digest=HashHexDigest

fsverity_signature
~~~~~~~~~~~~~~~~~~

Version 1
^^^^^^^^^

This property can be utilized for authorization of all fsverity enabled
files that is verified by fsverity. The keyring that is verifies against
is subject to fsverity's configuration, which is typically the fsverity
keyring. It has a dependency on the ``CONFIG_FS_VERITY_BUILTIN_SIGNATURES``
Kconfig. This property is controlled by the Kconfig
``CONFIG_IPE_PROP_FS_VERITY_SIGNATURE``. The format of this property is::

   fsverity_signature=(TRUE|FALSE)

Policy Examples
---------------

Allow all
~~~~~~~~~

::

   policy_name="Allow All" policy_version=0.0.0
   DEFAULT action=ALLOW

Allow only initial superblock
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name="Allow All Initial SB" policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE boot_verified=TRUE action=ALLOW

Allow any signed dm-verity volume and the initial superblock
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name="AllowSignedAndInitial" policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE boot_verified=TRUE action=ALLOW
   op=EXECUTE dmverity_signature=TRUE action=ALLOW

Prohibit execution from a specific dm-verity volume
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name="AllowSignedAndInitial" policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE dmverity_roothash=401fcec5944823ae12f62726e8184407a5fa9599783f030dec146938 action=DENY
   op=EXECUTE boot_verified=TRUE action=ALLOW
   op=EXECUTE dmverity_signature=TRUE action=ALLOW

Allow only a specific dm-verity volume
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name="AllowSignedAndInitial" policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE dmverity_roothash=401fcec5944823ae12f62726e8184407a5fa9599783f030dec146938 action=ALLOW

Allow any signed fs-verity file
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name="AllowSignedFSVerity" policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE fsverity_signature=TRUE action=ALLOW

Prohibit execution of a specific fs-verity file
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name="ProhibitSpecificFSVF" policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE fsverity_digest=fd88f2b8824e197f850bf4c5109bea5cf0ee38104f710843bb72da796ba5af9e action=DENY
   op=EXECUTE boot_verified=TRUE action=ALLOW
   op=EXECUTE dmverity_signature=TRUE action=ALLOW

Additional Information
----------------------

- `Github Repository <https://github.com/microsoft/ipe>`_
- `Design Documentation </security/ipe>`_

FAQ
---

:Q: What's the difference between other LSMs which provide trust-based
   access control, for instance, IMA?

:A: IMA is a fantastic option when needing measurement in addition to the
   trust-based access model. All of IMA is centered around their measurement
   hashes, so you save time when doing both actions. IPE, on the other hand,
   is a highly performant system that does not rely (and explicitly prohibits),
   generating its own integrity mechanisms - separating measurement and access
   control. Simply put, IPE provides only the enforcement of trust, while other
   subsystems provide the integrity guarantee that IPE needs to determine the
   trust of a resource. IMA provides both the integrity guarantee, and the
   enforcement of trust.
