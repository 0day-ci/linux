.. SPDX-License-Identifier: GPL-2.0

Integrity Policy Enforcement (IPE) - Design Documents
=====================================================

.. NOTE::

   This is the documentation for kernel developers and other individuals
   who want to understand the reason behind why IPE is designed the way it
   is, as well as a tour of the implementation. If you're looking for
   documentation on the usage of IPE, please see
   :ref:`Documentation/admin-guide/LSM/ipe.rst`

Role and Scope
--------------

IPE originally started with a simple goal: create a system that can
ensure that only trusted usermode binaries are allowed to be executed.

During the design phase it was apparent that there are multiple systems
within the Linux kernel that can provide some level of integrity
verification, and by association, trust for its content:

  1. DM-Verity
  2. FS-Verity
  3. IMA + EVM

However, of those systems only the third option has the ability to enforce
trust requirements on the whole system. Its architecture, however is centered
around its own form of verifications, and a multitude of actions surrounding
those verifications with various purposes, the most prominent being measurement
and verification (appraisal). This makes it unsuitable from a layering and
architectural purpose, as IPE's goal is limited to ensure just trusted usermode
binaries are executed, with the intentional goal of supporting multiple methods
from a higher subsystem layer (i.e. fs, block, or super_block).

The two other options, dm-verity and fs-verity are missing a crucial component
to accomplish the goal of IPE: a policy to indicate the requirements of
answering the question "What is Trusted?" and a system-wide level of enforcing
those requirements.

Therefore, IPE was designed around:

  1. Easy configuration of trust mechanisms
  2. Ease of integration with other layers
  3. Ease of use for platform administrators.

Design Decisions
----------------

Policy
~~~~~~

Plain Text
^^^^^^^^^^

Unlike other LSMs, IPE's policy is plain-text. This introduces slightly larger
policy files than other LSMs, but solves two major problems that occurs with
other trust-based access control systems.

The first issue is one of code maintenance and duplication. To author policies,
the policy has to be some form of string representation (be it structured,
through XMl, JSON, YAML, etcetera), to allow the policy author to understand
what is being written. In a hypothetical binary policy design, that a serializer
must be written to write said binary form, for a *majority* of humans to be
able to utilize it properly.

Additionally, a deserializer will eventually be needed to transform the binary
back into text with as much information preserved. Without a deserializer, a
user of this access control system will have to keep a lookup table of either
a checksum, or the file itself to try to understand what policies have been
deployed on this system and what policies have not. For a single user, this
may be alright, as old policies can be discarded almost immediately after
the update takes hold.

For users that manage fleets in the thousands, if not hundreds of thousands,
this quickly becomes an issue, as stale policies from years ago may be present,
quickly resulting in the need to recover the policy or fund extensive
infrastructure to track what each policy contains.

Secondly, a serializer is still needed with a plain-text policy (as the plain
text policy still has to be serialized to a data structure in the kernel), so
not much is saved.

The second issue is one of transparency. As IPE controls access based on trust,
it's policy must also be trusted to be changed. This is done through signatures,
chaining to the SYSTEM_TRUSTED_KEYRING. The confidence of signing a plain-text
policy in which you can see every aspect of what is being signed is a step higher
than signing an opaque binary blob.

Boot Policy
~~~~~~~~~~~

Additionally, IPE shouldn't have any obvious gaps in its enforcement story.
That means, a policy that configures trust requirements, if specified, must
be enforced as soon as the kernel starts up. That can be accomplished one
of three ways:

  1. The policy file(s) live on disk and the kernel loads the policy prior
     to an code path that would result in an enforcement decision.
  2. The policy file(s) are passed by the bootloader to the kernel, who
     parses the policy.
  3. There is a policy file that is compiled into the kernel that is
     parsed and enforced on initialization.

The first option has problems: the kernel reading files from userspace
is typically discouraged and very uncommon in the kernel.

The second option also has problems: Linux supports a variety of bootloaders
across its entire ecosystem - every bootloader would have to support this
new methodology or there must be an independent source. Additionally, it
would likely result in more drastic changes to the kernel startup than
necessary.

The third option is the best but it's important to be aware that the policy
will take disk space against the kernel it's compiled in. It's important to
keep this policy generalized enough that userspace can load a new, more
complicated policy, but restrictive enough that it will not overauthorize
and cause security issues.

The initramfs, provides a way that this bootup path can be established. The
kernel starts with a minimal policy, that just trusts the initramfs. Inside
the initramfs, when the real rootfs is mounted, but not yet transferred to,
it deploys and activates a policy that trusts the new root filesystem(s).
This prevents overauthorization at any step, and keeps the kernel policy
to a minimal size.

Startup
^^^^^^^

Not every system, however starts with an initramfs, so the startup policy
compiled into the kernel will need some flexibility to express how trust
is established for the next phase of the bootup. To this end, if we just
make the compiled-in policy a full IPE policy, it allows system builders
to express the first stage bootup requirements appropriately.

Updatable, Rebootless Policy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As time goes on, trust requirements are changed (vulnerabilities are found in
previously trusted applcations, keys roll, etcetera). Updating a kernel to
change the trust requirements is not always a suitable option, as updates
are not always risk-free and without consequence. This means IPE requires
a policy that can be completely updated from a source external to the kernel.

Additionally, since the kernel is relatively stateless between invocations,
and we've established that reading policy files off the disk from kernel
space is a *bad idea*, then the policy updates have to be done rebootlessly.

To allow an update from an external source, it could be potentially malicious,
so this policy needs to have a way to be identified as trusted. This will be
done via a signature, chained to a trust source in the kernel. Arbitrarily,
this will be the ``SYSTEM_TRUSTED_KEYRING``, a keyring that is initially
populated at kernel compile-time, as this matches the expectation that the
author of the compiled-in policy described above is the same entity that can
deploy policy updates.

Anti-Rollback / Anti-Replay
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Over time, vulnerabilities are found and trusted resources may not be
trusted anymore. IPE's policy has no exception to this. There can be
instances where a mistaken policy author deploys an insecure policy,
before correcting it with a secure policy.

Assuming that as soon as the insecure policy was signed, an attacker
can acquire the insecure policy, IPE needs a way to prevent rollback
from the secure policy update, to the insecure policy update.

Initially, IPE's policy can have a policy_version that states the
minimum required version across all policies that can be active on
the system. This will prevent rollback while the system is live.

.. WARNING::

  However, since the kernel is stateless across boots, this policy
  version will be reset to 0.0.0 on the next boot. System builders
  need to be aware of this, and ensure the new secure policies are
  deployed ASAP after a boot to ensure that the window of
  opportunity is minimal for an attacker to deploy the insecure policy[#]_.

Implementation
--------------

Context
~~~~~~~

An ``ipe_context`` structure represent a context in which IPE can be enforced.
It contains all the typical values that one would expect are global:

  1. Enforce/Permissive State
  2. Active Policy
  3. List of Policies
  4. Success Auditing State

A context is created at boot time and attached to the ``task_struct`` as a
security blob. All new ``task_struct`` will inherit the original ``ipe_context``
that the system boots with. This structure is reference counted.

Initially, a system will only ever have one context; for ``init``, and since
all userspace processes are descendents of ``init``, all of usermode will have
this execution context.

This architecture has some advantages - namely, it allows for a natural
extension for IPE to create new contexts - such as applying a different
policy for trust for a privledged container from that of its host.

Anonymous Memory
~~~~~~~~~~~~~~~~

Anonymous memory isn't treated any differently than any other access in IPE.
When anonymous memory is mapped with ``+X``, it still comes into the ``file_mmap``
hook, but with a ``NULL`` file object. This is submitted to the evaluation, like
any other file, however, all trust mechanisms will return false as there is
nothing to evaluate. This means anonymous memory execution is subject to
whatever the ``DEFAULT`` is for ``EXECUTE``.

.. WARNING::

  This also occurs with the ``kernel_load_data`` hook, which is used by signed
  and compressed kernel modules. Using this with IPE will result in the
  ``DEFAULT`` for ``KMODULE`` being taken.

Policy Parser
~~~~~~~~~~~~~

The policy parser is the staple of IPE's functionality, providing a
modular way to introduce new integrations. As such, it's functionality
is divided into 4 passes. This gives the benefit of clearly defined pre
and post-condition states after each pass, giving debugging benefits
when something goes wrong.

In pass1, the policy is transformed into a 2D, jagged, array of tokens,
where a token is defined as a "key=value" pair, or a singular token,
for example, "DEFAULT". Quoted values are parsed as a single value-pair,
which is why ``<linux/parser.h>`` parser is insufficient - it does not
understand quoted values.

In pass2, the jagged array produced in pass1 is partially ingested,
creating a partially populated policy, where no rules have been parsed
yet, but metadata and references are created that can be now used in
pass3.

Examples of parsing that would be done in pass2::

  policy_name="my-policy" policy_version=0.0.0
  DEFAULT action=DENY

As these lines are not rules in of themselves, but effect the policy
itself.

In pass3, the remaining lines in the jagged array produced in pass1 and
partially-consumed in pass2 is consumed completely, parsing all the
rules in IPE policy. This can leverage the data used in pass2.
Example lines parsed in pass3::

  op=EXECUTE dmverity_signature=TRUE action=DENY

A rule is strictly defined as starts with the op token and ends with
the action token.

After this pass, a policy is deemed fully constructed but not yet valid,
as there could be missing elements (such as a required DEFAULT for all
actions, missing a policy_name), etc.

Additionally, as IPE policy supports operation aliases (an operation
that maps to two or more other operations), support is added here.

The purpose in the division of pass2 and pass3 is to allow for
declarations in IPE's syntax. For example, in the future, if we were
to introduce this syntax::

  CERTIFICATE=FakeCert thumbprint=DEADBEEF CN="Contoso"

And use it like so::

  op=EXECUTE dmverity_signature=FakeCert action=ALLOW

The ``CERTIFICATE`` lines can be grouped together at any place in the policy.

After pass3, an IPE policy can still be technically invalid for use, as
a policy can be lacking required elements to eliminated the possibility
of undefined or unknown behavior.

A concrete example is when a policy does not define a default action for
all possibilities::

  DEFAULT op=EXECUTE action=ALLOW

At this point, while a technically syntactically and semantically valid
policy, it does not contain enough information to determine what should
be done for an operation other than "EXECUTE". As IPE's design
explicitly prohibits the implicit setting of a DEFAULT, it is important
for cases like these are prevented from occurring.

To resolve all these cases, a final check on the policy is done to ensure
it valid for use.

In all cases, the parser is the number one bottleneck when it comes to
IPE's performance, but has the benefit of happening rarely, and as a
direct consequence of user-input.

Module vs Parser
~~~~~~~~~~~~~~~~

A "module", "trust provider", or "property" as defined in IPE's code and
commits is an integration with an external subsystem that provides a way
to identify a resource as trusted. It's the code that powers the key=value
pairs in between the ``op`` token and the ``action`` token. These are called
in pass3 when parsing a policy (via the ``parse`` method), and during
evaluation when evaluating a access attempt (via the ``eval`` method). These
discrete modules are single files in ``security/ipe/modules`` and are
versioned independently. The documentation in the admin guide and be used
to cross reference what version supports what syntax.

A "parser", on the other hand is a discrete unit of code that is *only*
used when parsing a policy in pass2. The intention is to make it easy
to introduce statements, like the ``DEFAULT`` statement::

  DEFAULT op=EXECUTE action=ALLOW
  DEFAULT action=ALLOW

or, the policy header::

  policy_name="MyPolicy" policy_version=0.0.0

These individual fragments of code, as such, gain access to manipulating
IPE's policy structure directly, as opposed to the opaque ``void *`` that
modules get.

.. [#] This is something we're interested in solving, using some
       persistent storage

Tests
~~~~~

IPE initially has KUnit Tests, testing primarily the parser and the context
structures. A majority of these are table-based testing, please contribute
to them, especially when adding new properties.
