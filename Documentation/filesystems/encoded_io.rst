===========
Encoded I/O
===========

Several filesystems (e.g., Btrfs) support transparent encoding (e.g.,
compression, encryption) of data on disk: written data is encoded by the kernel
before it is written to disk, and read data is decoded before being returned to
the user. In some cases, it is useful to skip this encoding step. For example,
the user may want to read the compressed contents of a file or write
pre-compressed data directly to a file. This is referred to as "encoded I/O".

User API
========

Encoded I/O is specified with the ``RWF_ENCODED`` flag to ``preadv2()`` and
``pwritev2()``. If ``RWF_ENCODED`` is specified, then ``iov[0].iov_base``
points to an ``encoded_iov`` structure, defined in ``<linux/encoded_io.h>``
as::

    struct encoded_iov {
            __aligned_u64 len;
            __aligned_u64 unencoded_len;
            __aligned_u64 unencoded_offset;
            __u32 compression;
            __u32 encryption;
    };

This may be extended in the future, so ``iov[0].iov_len`` must be set to
``sizeof(struct encoded_iov)`` for forward/backward compatibility. The
remaining buffers contain the encoded data.

``compression`` and ``encryption`` are the encoding fields. ``compression`` is
``ENCODED_IOV_COMPRESSION_NONE`` (zero) or a filesystem-specific
``ENCODED_IOV_COMPRESSION_*`` constant; see `Filesystem support`_ below.
``encryption`` is currently always ``ENCODED_IOV_ENCRYPTION_NONE`` (zero).

``unencoded_len`` is the length of the unencoded (i.e., decrypted and
decompressed) data. ``unencoded_offset`` is the offset from the first byte of
the unencoded data to the first byte of logical data in the file (less than or
equal to ``unencoded_len``). ``len`` is the length of the data in the file
(less than or equal to ``unencoded_len - unencoded_offset``). See `Extent
layout`_ below for some examples.

If the unencoded data is actually longer than ``unencoded_len``, then it is
truncated; if it is shorter, then it is extended with zeroes.

``pwritev2()`` uses the metadata specified in ``iov[0]``, writes the encoded
data from the remaining buffers, and returns the number of encoded bytes
written (that is, the sum of ``iov[n].iov_len for 1 <= n < iovcnt``; partial
writes will not occur). At least one encoding field must be non-zero. Note that
the encoded data is not validated when it is written; if it is not valid (e.g.,
it cannot be decompressed), then a subsequent read may return an error. If the
offset argument to ``pwritev2()`` is -1, then the file offset is incremented by
``len``. If ``iov[0].iov_len`` is less than ``sizeof(struct encoded_iov)`` in
the kernel, then any fields unknown to user space are treated as if they were
zero; if it is greater and any fields unknown to the kernel are non-zero, then
``pwritev2()`` returns -1 and sets errno to ``E2BIG``.

``preadv2()`` populates the metadata in ``iov[0]``, the encoded data in the
remaining buffers, and returns the number of encoded bytes read. This will only
return one extent per call. This can also read data which is not encoded; all
encoding fields will be zero in that case. If the offset argument to
``preadv2()`` is -1, then the file offset is incremented by ``len``. If
``iov[0].iov_len`` is less than ``sizeof(struct encoded_iov)`` in the kernel
and any fields unknown to user space are non-zero, then ``preadv2()`` returns
-1 and sets errno to ``E2BIG``; if it is greater, then any fields unknown to
the kernel are returned as zero. If the provided buffers are not large enough
to return an entire encoded extent, then ``preadv2()`` returns -1 and sets
errno to ``ENOBUFS``.

As the filesystem page cache typically contains decoded data, encoded I/O
bypasses the page cache.

Extent layout
-------------

By using ``len``, ``unencoded_len``, and ``unencoded_offset``, it is possible
to refer to a subset of an unencoded extent.

In the simplest case, ``len`` is equal to ``unencoded_len`` and
``unencoded_offset`` is zero. This means that the entire unencoded extent is
used.

However, suppose we read 50 bytes into a file which contains a single
compressed extent. The filesystem must still return the entire compressed
extent for us to be able to decompress it, so ``unencoded_len`` would be the
length of the entire decompressed extent. However, because the read was at
offset 50, the first 50 bytes should be ignored. Therefore,
``unencoded_offset`` would be 50, and ``len`` would accordingly be
``unencoded_len - 50``.

Additionally, suppose we want to create an encrypted file with length 500, but
the file is encrypted with a block cipher using a block size of 4096. The
unencoded data would therefore include the appropriate padding, and
``unencoded_len`` would be 4096. However, to represent the logical size of the
file, ``len`` would be 500 (and ``unencoded_offset`` would be 0).

Similar situations can arise in other cases:

* If the filesystem pads data to the filesystem block size before compressing,
  then compressed files with a size unaligned to the filesystem block size will
  end with an extent with ``len < unencoded_len``.

* Extents cloned from the middle of a larger encoded extent with
  ``FICLONERANGE`` may have a non-zero ``unencoded_offset`` and/or
  ``len < unencoded_len``.

* If the middle of an encoded extent is overwritten, the filesystem may create
  extents with a non-zero ``unencoded_offset`` and/or ``len < unencoded_len``
  for the parts that were not overwritten.

Security
--------

Encoded I/O creates the potential for some security issues:

* Encoded writes allow writing arbitrary data which the kernel will decode on a
  subsequent read. Decompression algorithms are complex and may have bugs that
  can be exploited by maliciously crafted data.
* Encoded reads may return data that is not logically present in the file (see
  the discussion of ``len`` vs ``unencoded_len`` above). It may not be intended
  for this data to be readable.

Therefore, encoded I/O requires privilege. Namely, the ``RWF_ENCODED`` flag may
only be used if the file description has the ``O_ALLOW_ENCODED`` file status
flag set, and the ``O_ALLOW_ENCODED`` flag may only be set by a thread with the
``CAP_SYS_ADMIN`` capability. The ``O_ALLOW_ENCODED`` flag can be set by
``open()`` or ``fcntl()``. It can also be cleared by ``fcntl()``; clearing it
does not require ``CAP_SYS_ADMIN``. Note that it is not cleared on ``fork()``
or ``execve()``. One may wish to use ``O_CLOEXEC`` with ``O_ALLOW_ENCODED``.

Filesystem support
------------------

Encoded I/O is supported on the following filesystems:

Btrfs (since Linux 5.13)
~~~~~~~~~~~~~~~~~~~~~~~~

Btrfs supports encoded reads and writes of compressed data. The data is encoded
as follows:

* If ``compression`` is ``ENCODED_IOV_COMPRESSION_BTRFS_ZLIB``, then the encoded
  data is a single zlib stream.
* If ``compression`` is ``ENCODED_IOV_COMPRESSION_BTRFS_ZSTD``, then the
  encoded data is a single zstd frame compressed with the windowLog compression
  parameter set to no more than 17.
* If ``compression`` is one of ``ENCODED_IOV_COMPRESSION_BTRFS_LZO_4K``,
  ``ENCODED_IOV_COMPRESSION_BTRFS_LZO_8K``,
  ``ENCODED_IOV_COMPRESSION_BTRFS_LZO_16K``,
  ``ENCODED_IOV_COMPRESSION_BTRFS_LZO_32K``, or
  ``ENCODED_IOV_COMPRESSION_BTRFS_LZO_64K``, then the encoded data is
  compressed page by page (using the page size indicated by the name of the
  constant) with LZO1X and wrapped in the format documented in the Linux kernel
  source file ``fs/btrfs/lzo.c``.

Additionally, there are some restrictions on ``pwritev2()``:

* ``offset`` (or the current file offset if ``offset`` is -1) must be aligned
  to the sector size of the filesystem.
* ``len`` must be aligned to the sector size of the filesystem unless the data
  ends at or beyond the current end of the file.
* ``unencoded_len`` and the length of the encoded data must each be no more
  than 128 KiB. This limit may increase in the future.
* The length of the encoded data must be less than or equal to
  ``unencoded_len.``
* If using LZO, the filesystem's page size must match the compression page
  size.

Implementation
==============

This section describes the requirements for filesystems implementing encoded
I/O.

First of all, a filesystem supporting encoded I/O must indicate this by setting
the ``FMODE_ENCODED_IO`` flag in its ``file_open`` file operation::

    static int foo_file_open(struct inode *inode, struct file *filp)
    {
            ...
            filep->f_mode |= FMODE_ENCODED_IO;
            ...
    }

Encoded I/O goes through ``read_iter`` and ``write_iter``, designated by the
``IOCB_ENCODED`` flag in ``kiocb->ki_flags``.

Reads
-----

Encoded ``read_iter`` should:

1. Call ``generic_encoded_read_checks()`` to validate the file and buffers
   provided by userspace.
2. Initialize the ``encoded_iov`` appropriately.
3. Copy it to the user with ``copy_encoded_iov_to_iter()``.
4. Copy the encoded data to the user.
5. Advance ``kiocb->ki_pos`` by ``encoded_iov->len``.
6. Return the size of the encoded data read, not including the ``encoded_iov``.

There are a few details to be aware of:

* Encoded ``read_iter`` should support reading unencoded data if the extent is
  not encoded.
* If the buffers provided by the user are not large enough to contain an entire
  encoded extent, then ``read_iter`` should return ``-ENOBUFS``. This is to
  avoid confusing userspace with truncated data that cannot be properly
  decoded.
* Reads in the middle of an encoded extent can be returned by setting
  ``encoded_iov->unencoded_offset`` to non-zero.
* Truncated unencoded data (e.g., because the file does not end on a block
  boundary) may be returned by setting ``encoded_iov->len`` to a value smaller
  value than ``encoded_iov->unencoded_len - encoded_iov->unencoded_offset``.

Writes
------

Encoded ``write_iter`` should (in addition to the usual accounting/checks done
by ``write_iter``):

1. Call ``copy_encoded_iov_from_iter()`` to get and validate the
   ``encoded_iov``.
2. Call ``generic_encoded_write_checks()`` instead of
   ``generic_write_checks()``.
3. Check that the provided encoding in ``encoded_iov`` is supported.
4. Advance ``kiocb->ki_pos`` by ``encoded_iov->len``.
5. Return the size of the encoded data written.

Again, there are a few details:

* Encoded ``write_iter`` doesn't need to support writing unencoded data.
* ``write_iter`` should either write all of the encoded data or none of it; it
  must not do partial writes.
* ``write_iter`` doesn't need to validate the encoded data; a subsequent read
  may return, e.g., ``-EIO`` if the data is not valid.
* The user may lie about the unencoded size of the data; a subsequent read
  should truncate or zero-extend the unencoded data rather than returning an
  error.
* Be careful of page cache coherency.
