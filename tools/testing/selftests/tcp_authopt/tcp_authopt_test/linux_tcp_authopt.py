# SPDX-License-Identifier: GPL-2.0
"""Python wrapper around linux TCP_AUTHOPT ABI"""

from dataclasses import dataclass
from ipaddress import IPv4Address, IPv6Address, ip_address
import socket
import errno
import logging
from .sockaddr import sockaddr_in, sockaddr_in6, sockaddr_storage, sockaddr_unpack
import typing
import struct

logger = logging.getLogger(__name__)


def BIT(x):
    return 1 << x


TCP_AUTHOPT = 38
TCP_AUTHOPT_KEY = 39

TCP_AUTHOPT_MAXKEYLEN = 80

TCP_AUTHOPT_FLAG_REJECT_UNEXPECTED = BIT(2)

TCP_AUTHOPT_KEY_DEL = BIT(0)
TCP_AUTHOPT_KEY_EXCLUDE_OPTS = BIT(1)
TCP_AUTHOPT_KEY_BIND_ADDR = BIT(2)

TCP_AUTHOPT_ALG_HMAC_SHA_1_96 = 1
TCP_AUTHOPT_ALG_AES_128_CMAC_96 = 2


@dataclass
class tcp_authopt:
    """Like linux struct tcp_authopt"""

    flags: int = 0
    sizeof = 4

    def pack(self) -> bytes:
        return struct.pack(
            "I",
            self.flags,
        )

    def __bytes__(self):
        return self.pack()

    @classmethod
    def unpack(cls, b: bytes):
        tup = struct.unpack("I", b)
        return cls(*tup)


def set_tcp_authopt(sock, opt: tcp_authopt):
    return sock.setsockopt(socket.IPPROTO_TCP, TCP_AUTHOPT, bytes(opt))


def get_tcp_authopt(sock: socket.socket) -> tcp_authopt:
    b = sock.getsockopt(socket.IPPROTO_TCP, TCP_AUTHOPT, tcp_authopt.sizeof)
    return tcp_authopt.unpack(b)


class tcp_authopt_key:
    """Like linux struct tcp_authopt_key"""

    def __init__(
        self,
        flags: int = 0,
        send_id: int = 0,
        recv_id: int = 0,
        alg=TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
        key: bytes = b"",
        addr: bytes = b"",
        include_options=None,
    ):
        self.flags = flags
        self.send_id = send_id
        self.recv_id = recv_id
        self.alg = alg
        self.key = key
        self.addr = addr
        if include_options is not None:
            self.include_options = include_options

    def pack(self):
        if len(self.key) > TCP_AUTHOPT_MAXKEYLEN:
            raise ValueError(f"Max key length is {TCP_AUTHOPT_MAXKEYLEN}")
        data = struct.pack(
            "IBBBB80s",
            self.flags,
            self.send_id,
            self.recv_id,
            self.alg,
            len(self.key),
            self.key,
        )
        data += bytes(self.addrbuf.ljust(sockaddr_storage.sizeof, b"\x00"))
        return data

    def __bytes__(self):
        return self.pack()

    @property
    def key(self) -> bytes:
        return self._key

    @key.setter
    def key(self, val: typing.Union[bytes, str]) -> bytes:
        if isinstance(val, str):
            val = val.encode("utf-8")
        if len(val) > TCP_AUTHOPT_MAXKEYLEN:
            raise ValueError(f"Max key length is {TCP_AUTHOPT_MAXKEYLEN}")
        self._key = val
        return val

    @property
    def addr(self):
        if not self.addrbuf:
            return None
        else:
            return sockaddr_unpack(bytes(self.addrbuf))

    @addr.setter
    def addr(self, val):
        if isinstance(val, bytes):
            if len(val) > sockaddr_storage.sizeof:
                raise ValueError(f"Must be up to {sockaddr_storage.sizeof}")
            self.addrbuf = val
        elif val is None:
            self.addrbuf = b""
        elif isinstance(val, str):
            self.addr = ip_address(val)
        elif isinstance(val, IPv4Address):
            self.addr = sockaddr_in(addr=val)
        elif isinstance(val, IPv6Address):
            self.addr = sockaddr_in6(addr=val)
        elif (
            isinstance(val, sockaddr_in)
            or isinstance(val, sockaddr_in6)
            or isinstance(val, sockaddr_storage)
        ):
            self.addr = bytes(val)
        else:
            raise TypeError(f"Can't handle addr {val}")
        return self.addr

    @property
    def include_options(self) -> bool:
        return (self.flags & TCP_AUTHOPT_KEY_EXCLUDE_OPTS) == 0

    @include_options.setter
    def include_options(self, value) -> bool:
        if value:
            self.flags &= ~TCP_AUTHOPT_KEY_EXCLUDE_OPTS
        else:
            self.flags |= TCP_AUTHOPT_KEY_EXCLUDE_OPTS

    @property
    def delete_flag(self) -> bool:
        return bool(self.flags & TCP_AUTHOPT_KEY_DEL)

    @delete_flag.setter
    def delete_flag(self, value) -> bool:
        if value:
            self.flags |= TCP_AUTHOPT_KEY_DEL
        else:
            self.flags &= ~TCP_AUTHOPT_KEY_DEL


def set_tcp_authopt_key(sock, key: tcp_authopt_key):
    return sock.setsockopt(socket.IPPROTO_TCP, TCP_AUTHOPT_KEY, bytes(key))


def has_tcp_authopt() -> bool:
    """Check is TCP_AUTHOPT is implemented by the OS"""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            optbuf = bytes(4)
            sock.setsockopt(socket.IPPROTO_TCP, TCP_AUTHOPT, optbuf)
            return True
        except OSError as e:
            if e.errno == errno.ENOPROTOOPT:
                return False
            else:
                raise
