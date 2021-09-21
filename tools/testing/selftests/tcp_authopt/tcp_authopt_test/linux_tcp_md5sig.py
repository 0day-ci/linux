# SPDX-License-Identifier: GPL-2.0
"""Python wrapper around linux TCP_MD5SIG ABI"""

from enum import IntFlag
import socket
import struct
from dataclasses import dataclass
from .sockaddr import sockaddr_unpack


TCP_MD5SIG = 14
TCP_MD5SIG_EXT = 32
TCP_MD5SIG_MAXKEYLEN = 80


class TCP_MD5SIG_FLAG(IntFlag):
    PREFIX = 0x1
    IFINDEX = 0x2


@dataclass
class tcp_md5sig:
    """Like linux struct tcp_md5sig"""

    addr = None
    flags: int
    prefixlen: int
    keylen: int
    ifindex: int
    key: bytes

    sizeof = 128 + 88

    def __init__(
        self, addr=None, flags=0, prefixlen=0, keylen=None, ifindex=0, key=bytes()
    ):
        self.addr = addr
        self.flags = flags
        self.prefixlen = prefixlen
        self.ifindex = ifindex
        self.key = key
        if keylen is None:
            self.keylen = len(key)
        else:
            self.keylen = keylen

    def get_addr_bytes(self) -> bytes:
        if self.addr is None:
            return b"\0" * 128
        if self.addr is bytes:
            assert len(self.addr) == 128
            return self.addr
        return self.addr.pack()

    def pack(self) -> bytes:
        return struct.pack(
            "128sBBHi80s",
            self.get_addr_bytes(),
            self.flags,
            self.prefixlen,
            self.keylen,
            self.ifindex,
            self.key,
        )

    def __bytes__(self):
        return self.pack()

    @classmethod
    def unpack(cls, buffer: bytes) -> "tcp_md5sig":
        tup = struct.unpack("128sBBHi80s", buffer)
        addr = sockaddr_unpack(tup[0])
        return cls(addr, *tup[1:])

    def set_ipv4_addr_all(self):
        from .sockaddr import sockaddr_in

        self.addr = sockaddr_in()
        self.prefixlen = 0
        self.flags |= TCP_MD5SIG_FLAG.PREFIX

    def set_ipv6_addr_all(self):
        from .sockaddr import sockaddr_in6

        self.addr = sockaddr_in6()
        self.prefixlen = 0
        self.flags |= TCP_MD5SIG_FLAG.PREFIX


def setsockopt_md5sig(sock, opt: tcp_md5sig):
    if opt.flags != 0:
        optname = TCP_MD5SIG_EXT
    else:
        optname = TCP_MD5SIG
    return sock.setsockopt(socket.SOL_TCP, optname, bytes(opt))
