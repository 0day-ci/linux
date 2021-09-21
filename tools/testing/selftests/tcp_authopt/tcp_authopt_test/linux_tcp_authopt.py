# SPDX-License-Identifier: GPL-2.0
"""Python wrapper around linux TCP_AUTHOPT ABI"""

from dataclasses import dataclass
from ipaddress import IPv4Address, IPv6Address, ip_address
import socket
from enum import IntEnum, IntFlag
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


class TCP_AUTHOPT_FLAG(IntFlag):
    LOCK_KEYID = BIT(0)
    LOCK_RNEXTKEYID = BIT(1)
    REJECT_UNEXPECTED = BIT(2)


class TCP_AUTHOPT_KEY_FLAG(IntFlag):
    DEL = BIT(0)
    EXCLUDE_OPTS = BIT(1)
    BIND_ADDR = BIT(2)


class TCP_AUTHOPT_ALG(IntEnum):
    HMAC_SHA_1_96 = 1
    AES_128_CMAC_96 = 2


@dataclass
class tcp_authopt:
    """Like linux struct tcp_authopt"""

    flags: int = 0
    send_keyid: int = 0
    send_rnextkeyid: int = 0
    recv_keyid: int = 0
    recv_rnextkeyid: int = 0
    sizeof = 8

    def pack(self) -> bytes:
        return struct.pack(
            "IBBBB",
            self.flags,
            self.send_keyid,
            self.send_rnextkeyid,
            self.recv_keyid,
            self.recv_rnextkeyid,
        )

    def __bytes__(self):
        return self.pack()

    @classmethod
    def unpack(cls, b: bytes):
        tup = struct.unpack("IBBBB", b)
        return cls(*tup)


def set_tcp_authopt(sock, opt: tcp_authopt):
    return sock.setsockopt(socket.SOL_TCP, TCP_AUTHOPT, bytes(opt))


def get_tcp_authopt(sock: socket.socket) -> tcp_authopt:
    b = sock.getsockopt(socket.SOL_TCP, TCP_AUTHOPT, tcp_authopt.sizeof)
    return tcp_authopt.unpack(b)


class tcp_authopt_key:
    """Like linux struct tcp_authopt_key"""

    def __init__(
        self,
        flags: int = 0,
        send_id: int = 0,
        recv_id: int = 0,
        alg=TCP_AUTHOPT_ALG.HMAC_SHA_1_96,
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
        return (self.flags & TCP_AUTHOPT_KEY.EXCLUDE_OPTS) == 0

    @include_options.setter
    def include_options(self, value) -> bool:
        if value:
            self.flags &= ~TCP_AUTHOPT_KEY_FLAG.EXCLUDE_OPTS
        else:
            self.flags |= TCP_AUTHOPT_KEY_FLAG.EXCLUDE_OPTS

    @property
    def delete_flag(self) -> bool:
        return bool(self.flags & TCP_AUTHOPT_KEY_FLAG.DEL)

    @delete_flag.setter
    def delete_flag(self, value) -> bool:
        if value:
            self.flags |= TCP_AUTHOPT_KEY_FLAG.DEL
        else:
            self.flags &= ~TCP_AUTHOPT_KEY_FLAG.DEL


def set_tcp_authopt_key(sock, key: tcp_authopt_key):
    return sock.setsockopt(socket.SOL_TCP, TCP_AUTHOPT_KEY, bytes(key))


def del_tcp_authopt_key(sock, key: tcp_authopt_key) -> bool:
    """Try to delete an authopt key

    :return: True if a key was deleted, False if it was not present
    """
    import copy

    key = copy.copy(key)
    key.delete_flag = True
    try:
        sock.setsockopt(socket.SOL_TCP, TCP_AUTHOPT_KEY, bytes(key))
        return True
    except OSError as e:
        if e.errno == errno.ENOENT:
            return False
        raise


def get_sysctl_tcp_authopt() -> bool:
    from pathlib import Path

    path = Path("/proc/sys/net/ipv4/tcp_authopt")
    if path.exists():
        return path.read_text().strip() != "0"


def enable_sysctl_tcp_authopt() -> bool:
    from pathlib import Path

    path = Path("/proc/sys/net/ipv4/tcp_authopt")
    try:
        if path.read_text().strip() == "0":
            path.write_text("1")
    except:
        raise Exception("Failed to enable /proc/sys/net/ipv4/tcp_authopt")


def has_tcp_authopt() -> bool:
    """Check is TCP_AUTHOPT is implemented by the OS

    Returns True if implemented but disabled by sysctl
    Returns False if disabled at compile time
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            optbuf = bytes(4)
            sock.setsockopt(socket.SOL_TCP, TCP_AUTHOPT, optbuf)
            return True
        except OSError as e:
            if e.errno == errno.ENOPROTOOPT:
                return False
            elif e.errno == errno.EPERM and get_sysctl_tcp_authopt() is False:
                return True
            else:
                raise
