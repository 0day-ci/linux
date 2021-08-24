# SPDX-License-Identifier: GPL-2.0
"""pack/unpack wrappers for sockaddr"""
import socket
import struct
from dataclasses import dataclass
from ipaddress import IPv4Address, IPv6Address


@dataclass
class sockaddr_in:
    port: int
    addr: IPv4Address
    sizeof = 8

    def __init__(self, port=0, addr=None):
        self.port = port
        if addr is None:
            addr = IPv4Address(0)
        self.addr = IPv4Address(addr)

    def pack(self):
        return struct.pack("HH4s", socket.AF_INET, self.port, self.addr.packed)

    @classmethod
    def unpack(cls, buffer):
        family, port, addr_packed = struct.unpack("HH4s", buffer[:8])
        if family != socket.AF_INET:
            raise ValueError(f"Must be AF_INET not {family}")
        return cls(port, addr_packed)

    def __bytes__(self):
        return self.pack()


@dataclass
class sockaddr_in6:
    """Like sockaddr_in6 but for python. Always contains scope_id"""

    port: int
    addr: IPv6Address
    flowinfo: int
    scope_id: int
    sizeof = 28

    def __init__(self, port=0, addr=None, flowinfo=0, scope_id=0):
        self.port = port
        if addr is None:
            addr = IPv6Address(0)
        self.addr = IPv6Address(addr)
        self.flowinfo = flowinfo
        self.scope_id = scope_id

    def pack(self):
        return struct.pack(
            "HHI16sI",
            socket.AF_INET6,
            self.port,
            self.flowinfo,
            self.addr.packed,
            self.scope_id,
        )

    @classmethod
    def unpack(cls, buffer):
        family, port, flowinfo, addr_packed, scope_id = struct.unpack(
            "HHI16sI", buffer[:28]
        )
        if family != socket.AF_INET6:
            raise ValueError(f"Must be AF_INET6 not {family}")
        return cls(port, addr_packed, flowinfo=flowinfo, scope_id=scope_id)

    def __bytes__(self):
        return self.pack()


@dataclass
class sockaddr_storage:
    family: int
    data: bytes
    sizeof = 128

    def pack(self):
        return struct.pack("H126s", self.family, self.data)

    def __bytes__(self):
        return self.pack()

    @classmethod
    def unpack(cls, buffer):
        return cls(*struct.unpack("H126s", buffer))


def sockaddr_unpack(buffer: bytes):
    """Unpack based on family"""
    family = struct.unpack("H", buffer[:2])[0]
    if family == socket.AF_INET:
        return sockaddr_in.unpack(buffer)
    elif family == socket.AF_INET6:
        return sockaddr_in6.unpack(buffer)
    else:
        return sockaddr_storage.unpack(buffer)
