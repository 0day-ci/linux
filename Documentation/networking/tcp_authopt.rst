.. SPDX-License-Identifier: GPL-2.0

=========================
TCP Authentication Option
=========================

The TCP Authentication option specified by RFC5925 replaces the TCP MD5
Signature option. It similar in goals but not compatible in either wire formats
or ABI.

Interface
=========

Individual keys can be added to or removed through an TCP socket by using
TCP_AUTHOPT_KEY setsockopt and a struct tcp_authopt_key. There is no
support for reading back keys and updates always replace the old key. These
structures represent "Master Key Tuples (MKTs)" as described by the RFC.

Per-socket options can set or read using the TCP_AUTHOPT sockopt and a struct
tcp_authopt. This is optional: doing setsockopt TCP_AUTHOPT_KEY is sufficient to
enable the feature.

Configuration associated with TCP Authentication is global for each network
namespace, this means that all sockets for which TCP_AUTHOPT is enabled will
be affected by the same set of keys.

Manipulating keys requires ``CAP_NET_ADMIN``.

Key binding
-----------

Keys can be bound to remote addresses in a way that is somewhat similar to
``TCP_MD5SIG``. By default a key matches all connections but matching criteria can
be specified as fields inside struct tcp_authopt_key together with matching
flags in tcp_authopt_key.flags. The sort of these "matching criteria" can
expand over time by increasing the size of `struct tcp_authopt_key` and adding
new flags.

 * Address binding is optional, by default keys match all addresses
 * Local address is ignored, matching is done by remote address
 * Ports are ignored

RFC5925 requires that key ids do not overlap when tcp identifiers (addr/port)
overlap. This is not enforced by linux, configuring ambiguous keys will result
in packet drops and lost connections.

ABI Reference
=============

.. kernel-doc:: include/uapi/linux/tcp.h
   :identifiers: tcp_authopt tcp_authopt_flag tcp_authopt_key tcp_authopt_key_flag tcp_authopt_alg
