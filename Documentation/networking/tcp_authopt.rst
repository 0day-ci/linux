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
 * It is possible to match a specific VRF by l3index (default is to ignore)
 * It is possible to match with a fixed prefixlen (default is full address)

RFC5925 requires that key ids do not overlap when tcp identifiers (addr/port)
overlap. This is not enforced by linux, configuring ambiguous keys will result
in packet drops and lost connections.

Key selection
-------------

On getsockopt(TCP_AUTHOPT) information is provided about keyid/rnextkeyid in
the last send packet and about the keyid/rnextkeyd in the last valid received
packet.

By default the sending keyid is selected to match the rnextkeyid value sent by
the remote side. If that keyid is not available (or for new connections) a
random matching key is selected.

If the ``TCP_AUTHOPT_LOCK_KEYID`` flag is set then the sending key is selected
by the `tcp_authopt.send_local_id` field and recv_rnextkeyid is ignored. If no
key with local_id == send_local_id is configured then a random matching key is
selected.

The current sending key is cached in the socket and will not change unless
requested by remote rnextkeyid or by setsockopt.

The rnextkeyid value sent on the wire is usually the recv_id of the current
key used for sending. If the TCP_AUTHOPT_LOCK_RNEXTKEY flag is set in
`tcp_authopt.flags` the value of `tcp_authopt.send_rnextkeyid` is send
instead.  This can be used to implement smooth rollover: the peer will switch
its keyid to the received rnextkeyid when it is available.

ABI Reference
=============

.. kernel-doc:: include/uapi/linux/tcp.h
   :identifiers: tcp_authopt tcp_authopt_flag tcp_authopt_key tcp_authopt_key_flag tcp_authopt_alg
