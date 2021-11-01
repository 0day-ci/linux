# SPDX-License-Identifier: GPL-2.0
"""Python wrapper around linux TCP_MD5SIG ABI"""

import socket
import struct
import typing
from dataclasses import dataclass
from enum import IntFlag

from .sockaddr import sockaddr_convert, sockaddr_unpack

TCP_MD5SIG = 14
TCP_MD5SIG_EXT = 32
TCP_MD5SIG_MAXKEYLEN = 80


class TCP_MD5SIG_FLAG(IntFlag):
    PREFIX = 0x1
    IFINDEX = 0x2


@dataclass
class tcp_md5sig:
    """Like linux struct tcp_md5sig"""

    addr: typing.Any
    flags: typing.Optional[int]
    prefixlen: typing.Optional[int]
    keylen: typing.Optional[int]
    ifindex: typing.Optional[int]
    key: bytes

    sizeof = 128 + 88

    def __init__(
        self, addr=None, flags=None, prefixlen=None, keylen=None, ifindex=0, key=bytes()
    ):
        self.addr = addr
        self.flags = flags
        self.prefixlen = prefixlen
        self.ifindex = ifindex
        self.key = key
        self.keylen = keylen

    def get_auto_flags(self):
        return (TCP_MD5SIG_FLAG.PREFIX if self.prefixlen is not None else 0) | (
            TCP_MD5SIG_FLAG.IFINDEX if self.ifindex else 0
        )

    def get_real_flags(self):
        if self.flags is None:
            return self.get_auto_flags()
        else:
            return self.flags

    def get_addr_bytes(self) -> bytes:
        if self.addr is None:
            return b"\0" * 128
        if self.addr is bytes:
            assert len(self.addr) == 128
            return self.addr
        return sockaddr_convert(self.addr).pack()

    def pack(self) -> bytes:
        return struct.pack(
            "128sBBHi80s",
            self.get_addr_bytes(),
            self.get_real_flags(),
            self.prefixlen if self.prefixlen is not None else 0,
            self.keylen if self.keylen is not None else len(self.key),
            self.ifindex if self.ifindex is not None else 0,
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

    def set_ipv6_addr_all(self):
        from .sockaddr import sockaddr_in6

        self.addr = sockaddr_in6()
        self.prefixlen = 0


def setsockopt_md5sig(sock, opt: tcp_md5sig):
    if opt.flags != 0:
        optname = TCP_MD5SIG_EXT
    else:
        optname = TCP_MD5SIG
    return sock.setsockopt(socket.SOL_TCP, optname, bytes(opt))


def setsockopt_md5sig_kwargs(sock, opt: tcp_md5sig = None, **kw):
    if opt is None:
        opt = tcp_md5sig()
    for k, v in kw.items():
        setattr(opt, k, v)
    return setsockopt_md5sig(sock, opt)
