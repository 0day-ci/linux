.. SPDX-License-Identifier: GPL-2.0

===================
Huawei Digest Lists
===================

Introduction
============

Huawei Digest Lists is a simple in kernel database for storing file and
metadata digests and for reporting to its users (e.g. Integrity Measurement
Architecture or IMA) whether the digests are in the database or not. The
choice of placing it in the kernel and not in a user space process is
explained later in the Security Assumptions section.

The database is populated by directly uploading the so called digest lists,
a set of digests concatenated together and preceded by a header including
information about them (e.g. whether the file or metadata with a given
digest is immutable or not). Digest lists are stored in the kernel memory
as they are, the kernel just builds indexes to easily lookup digests.

The kernel supports only one digest list format called ``compact``. However,
alternative formats (e.g. the RPM header) can be supported through a user
space parser that, by invoking the appropriate library (more can be added),
converts the original digest list to the compact format and uploads it to
the kernel.

The database keeps track of whether digest lists have been processed in
some way (e.g. measured or appraised by IMA). This is important for example
for remote attestation, so that remote verifiers understand what has been
loaded in the database.

It is a transactional database, i.e. it has the ability to roll back to the
beginning of the transaction if an error occurred during the addition of a
digest list (the deletion operation always succeeds). This capability has
been tested with an ad-hoc fault injection mechanism capable of simulating
failures during the operations.

Finally, the database exposes to user space, through securityfs, the digest
lists currently loaded, the number of digests added, a query interface and
an interface to set digest list labels.


Binary Integrity
----------------

Integrity is a fundamental security property in information systems.
Integrity could be described as the condition in which a generic
component is just after it has been released by the entity that created it.

One way to check whether a component is in this condition (called binary
integrity) is to calculate its digest and to compare it with a reference
value (i.e. the digest calculated in controlled conditions, when the
component is released).

IMA, a software part of the integrity subsystem, can perform such
evaluation and execute different actions:

- store the digest in an integrity-protected measurement list, so that it
  can be sent to a remote verifier for analysis;
- compare the calculated digest with a reference value (usually protected
  with a signature) and deny operations if the file is found corrupted;
- store the digest in the system log.


Contribution
------------

Huawei Digest Lists facilitates the provisioning of reference values for
system and application files from software vendors, to the kernel.

Possible sources for digest lists are:

- RPM headers;
- Debian repository metadata.

These sources are already usable without any modification required for
Linux vendors.

If digest list sources are signed (usually they are, like the ones above),
remote verifiers could identify their provenance, or Linux vendors could
prevent the loading of unsigned ones or those signed with an untrusted key.


Possible Usages
---------------

Provisioned reference values can be used (e.g. by IMA) to make
integrity-related decisions (allow list or deny list).

Possible usages for IMA are:

- avoid recording measurement of files whose digest is found in the
  pre-provisioned reference values:

  - reduces the number of TPM operations (PCR extend);
  - could make a TPM PCR predictable, as the PCR would not be affected by
    the temporal sequence of executions if binaries are known
    (no measurement);

- exclusively grant access to files whose digest is found in the
  pre-provisioned reference values:

  - faster verification time (fewer signature verifications);
  - no need to generate a signature for every file.


Security Assumptions
--------------------

Since digest lists are stored in the kernel memory, they are no susceptible
to attacks by user space processes.

A locked-down kernel, that accepts only verified kernel modules, will allow
digest lists to be added or deleted only though a well-defined and
monitored interface. In this situation, the root user is assumed to be
untrusted, i.e. it cannot subvert without being detected the mandatory
policy stating which files are accessible by the system.

Adoption
--------

A former version of Huawei Digest Lists is used in the following OSes:

- openEuler 20.09
  https://github.com/openeuler-mirror/kernel/tree/openEuler-20.09

- openEuler 21.03
  https://github.com/openeuler-mirror/kernel/tree/openEuler-21.03

Originally, Huawei Digest Lists was part of IMA. In this version,
it has been redesigned as a standalone module with an API that makes
its functionality accessible by IMA and, eventually, other subsystems.

User Space Support
------------------

Digest lists can be generated and managed with ``digest-list-tools``:

https://github.com/openeuler-mirror/digest-list-tools

It includes two main applications:

- ``gen_digest_lists``: generates digest lists from files in the
  filesystem or from the RPM database (more digest list sources can be
  supported);
- ``manage_digest_lists``: converts and uploads digest lists to the
  kernel.


Simple Usage Example
--------------------

1. Digest list generation (RPM headers and their signature are copied to
   the specified directory):

.. code-block:: bash

 # mkdir /etc/digest_lists
 # gen_digest_lists -t file -f rpm+db -d /etc/digest_lists -o add


2. Digest list upload with the user space parser:

.. code-block:: bash

 # manage_digest_lists -p add-digest -d /etc/digest_lists

3. First digest list query:

.. code-block:: bash

 # echo sha256-$(sha256sum /bin/cat) > /sys/kernel/security/integrity/digest_lists/digest_query
 # cat /sys/kernel/security/integrity/digest_lists/digest_query
   sha256-[...]-0-file_list-rpm-coreutils-8.32-18.fc33.x86_64 (actions: 0): version: 1, algo: sha256, type: 2, modifiers: 1, count: 106, datalen: 3392

4. Second digest list query:

.. code-block:: bash

 # echo sha256-$(sha256sum /bin/zip) > /sys/kernel/security/integrity/digest_lists/digest_query
 # cat /sys/kernel/security/integrity/digest_lists/digest_query
   sha256-[...]-0-file_list-rpm-zip-3.0-27.fc33.x86_64 (actions: 0): version: 1, algo: sha256, type: 2, modifiers: 1, count: 4, datalen: 128


Architecture
============

This section introduces the high level architecture.

::

 5. add/delete from hash table and add refs to digest list
        +---------------------------------------------+
        |                            +-----+   +-------------+         +--+
        |                            | key |-->| digest refs |-->...-->|  |
        V                            +-----+   +-------------+         +--+
 +-------------+                     +-----+   +-------------+
 | digest list |                     | key |-->| digest refs |
 |  (compact)  |                     +-----+   +-------------+
 +-------------+                     +-----+   +-------------+
        ^ 4. copy to                 | key |-->| digest refs |
        |    kernel memory           +-----+   +-------------+ kernel space
 --------------------------------------------------------------------------
        ^                                          ^             user space
        |<----------------+       3b. upload       |
 +-------------+   +------------+                  | 6. query digest
 | digest list |   | user space | 2b. convert
 |  (compact)  |   |   parser   |
 +-------------+   +------------+
 1a. upload               ^       1b. read
                          |
                   +------------+
                   | RPM header |
                   +------------+


As mentioned before, digest lists can be uploaded directly if they are in
the compact format (step 1a) or can be uploaded indirectly by the user
space parser if they are in an alternative format (steps 1b-3b).

During upload, the kernel makes a copy of the digest list to the kernel
memory (step 4), and creates the necessary structures to index the digests
(hash table and an array of digest list references to locate the digests in
the digest list) (step 5).

Finally, digests can be searched from user space through a securityfs file
(step 6) or by the kernel itself.
