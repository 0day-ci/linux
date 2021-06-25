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


Implementation
==============

This section describes the implementation of Huawei Digest Lists.


Basic Definitions
-----------------

This section introduces the basic definitions required to use digest lists.


Compact Digest List Format
~~~~~~~~~~~~~~~~~~~~~~~~~~

Compact digest lists consist of one or multiple headers defined as:

::

	struct compact_list_hdr {
		__u8 version;
		__u8 _reserved;
		__le16 type;
		__le16 modifiers;
		__le16 algo;
		__le32 count;
		__le32 datalen;
	} __packed;

which characterize the subsequent block of concatenated digests.

The ``algo`` field specifies the algorithm used to calculate the digest.

The ``count`` field specifies how many digests are stored in the subsequent
block of digests.

The ``datalen`` field specifies the length of the subsequent block of
digests (it is redundant, it is the same as
``hash_digest_size[algo] * count``).


Compact Types
.............

Digests can be of different types:

- ``COMPACT_PARSER``: digests of executables which are given the ability to
  parse digest lists not in the compact format and to upload to the kernel
  the digest list converted to the compact format;
- ``COMPACT_FILE``: digests of regular files;
- ``COMPACT_METADATA``: digests of file metadata (e.g. the digest
  calculated by EVM to verify a portable signature);
- ``COMPACT_DIGEST_LIST``: digests of digest lists (only used internally by
  the kernel).

Different users of Huawei Digest Lists might query digests with different
compact types. For example, IMA would be interested in COMPACT_FILE, as it
deals with regular files, while EVM would be interested in
COMPACT_METADATA, as it verifies file metadata.


Compact Modifiers
.................

Digests can also have specific attributes called modifiers (bit position):

- ``COMPACT_MOD_IMMUTABLE``: file content or metadata should not be
  modifiable.

IMA might use this information to deny open for writing, or EVM to deny
setxattr operations.


Actions
.......

This section defines a set of possible actions that have been executed on
the digest lists (bit position):

- ``COMPACT_ACTION_IMA_MEASURED``: the digest list has been measured by
  IMA;
- ``COMPACT_ACTION_IMA_APPRAISED``: the digest list has been successfully
  appraised by IMA;
- ``COMPACT_ACTION_IMA_APPRAISED_DIGSIG``: the digest list has been
  successfully appraised by IMA by verifying a digital signature.

This information might help users of Huawei Digest Lists to decide whether
to use the result of a queried digest.

For example, if a digest belongs to a digest list that was not measured
before, IMA should ignore the result of the query as the measurement list
sent to remote verifiers would lack how the database was populated.


Compact Digest List Example
...........................

::

 version: 1, type: 2, modifiers: 0 algo: 4, count: 3, datalen: 96
 <SHA256 digest1><SHA256 digest2><SHA256 digest3>
 version: 1, type: 3, modifiers: 1 algo: 6, count: 2, datalen: 128
 <SHA512 digest1><SHA512 digest2>

This digest list consists of two blocks. The first block contains three
SHA256 digests of regular files. The second block contains two SHA512
digests of immutable metadata.


Compact Digest List Operations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Finally, this section defines the possible operations that can be performed
with digest lists:

- ``DIGEST_LIST_ADD``: the digest list is being added;
- ``DIGEST_LIST_DEL``: the digest list is being deleted.


Objects
-------

This section defines the objects to manage digest lists:

- ``digest_list_item``: represents a digest list;
- ``digest_list_item_ref``: represents a reference to a digest list,
  i.e. the location at which a digest within a digest list can be accessed;
- ``digest_item``: represents a unique digest.

They are represented in the following class diagram:

::

 digest_offset,-----------+
 hdr_offset               |
                          |
 +------------------+     |     +----------------------+
 | digest_list_item |--- N:1 ---| digest_list_item_ref |
 +------------------+           +----------------------+
                                           |
                                          1:N
                                           |
                                    +-------------+
                                    | digest_item |
                                    +-------------+

A ``digest_list_item`` is associated to one or multiple
``digest_list_item_ref``, one for each digest it contains. However,
a ``digest_list_item_ref`` is associated to only one ``digest_list_item``,
as it represents a single location within a specific digest list.

Given that a ``digest_list_item_ref`` represents a single location, it is
associated to only one ``digest_item``. However, a ``digest_item`` can have
multiple references (as it might appears multiple times within the same
digest list or in different digest lists, if it is duplicated).


A ``digest_list_item`` is defined as:

::

	struct digest_list_item {
		loff_t size;
		u8 *buf;
		u8 actions;
		u8 digest[64];
		enum hash_algo algo;
		const char *label;
	};

- ``size``: size of the digest list buffer;
- ``buf``: digest list buffer;
- ``actions``: actions performed on the digest list;
- ``digest``: digest of the digest list;
- ``algo``: digest algorithm;
- ``label``: label used to identify the digest list (e.g. file name).

A ``digest_list_item_ref`` is defined as:

::

	struct digest_list_item_ref {
		struct digest_list_item *digest_list;
		loff_t digest_offset;
		loff_t hdr_offset;
	};

- ``digest_list``: pointer to a ``digest_list_item`` structure;
- ``digest_offset``: offset of the digest related to the digest list
  buffer;
- ``hdr_offset``: offset of the header of the digest block containing the
  digest.

A ``digest_item`` is defined as:

::

	struct digest_item {
		struct hlist_node hnext;
		struct digest_list_item_ref *refs;
	};

- ``hnext``: pointers of the hash table;
- ``refs``: array of ``digest_list_item_ref`` structures including a
  terminator (protected by RCU).

All digest list references are stored for a given digest, so that a query
result can include the OR of the modifiers and actions of each referenced
digest list.

The relationship between the described objects can be graphically
represented as:

::

 Hash table            +-------------+         +-------------+
 PARSER      +-----+   | digest_item |         | digest_item |
 FILE        | key |-->|             |-->...-->|             |
 METADATA    +-----+   |ref0|...|refN|         |ref0|...|refN|
                       +-------------+         +-------------+
            ref0:         |                               | refN:
            digest_offset | +-----------------------------+ digest_offset
            hdr_offset    | |                               hdr_offset
                          V V
                     +--------------------+
                     |  digest_list_item  |
                     |                    |
                     | size, buf, actions |
                     +--------------------+
                          ^
                          |
 Hash table            +-------------+         +-------------+
 DIGEST_LIST +-----+   |ref0         |         |ref0         |
             | key |-->|             |-->...-->|             |
             +-----+   | digest_item |         | digest_item |
                       +-------------+         +-------------+

The reference for the digest of the digest list differs from the references
for the other digest types. ``digest_offset`` and ``hdr_offset`` are set to
zero, so that the digest of the digest list is retrieved from the
``digest_list_item`` structure directly (see ``get_digest()`` below).

Finally, this section defines useful helpers to access a digest or the
header the digest belongs to. For example:

::

 static inline struct compact_list_hdr *get_hdr(
                                      struct digest_list_item *digest_list,
                                      loff_t hdr_offset)
 {
         return (struct compact_list_hdr *)(digest_list->buf + hdr_offset);
 }

the header can be obtained by summing the address of the digest list buffer
in the ``digest_list_item`` structure with ``hdr_offset``.

Similarly:

::

 static inline u8 *get_digest(struct digest_list_item *digest_list,
                              loff_t digest_offset, loff_t hdr_offset)
 {
         /* Digest list digest is stored in a different place. */
         if (!digest_offset)
                 return digest_list->digest;
         return digest_list->buf + digest_offset;
 }

the digest can be obtained by summing the address of the digest list buffer
with ``digest_offset`` (except for the digest lists, where the digest is
stored in the ``digest`` field of the ``digest_list_item`` structure).
