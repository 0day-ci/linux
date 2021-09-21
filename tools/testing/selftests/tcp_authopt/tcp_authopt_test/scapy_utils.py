import typing
import struct
import socket
import threading
from dataclasses import dataclass
from ipaddress import IPv4Address, IPv6Address

from scapy.packet import Packet
from scapy.layers.inet import IP, TCP
from scapy.layers.inet6 import IPv6
from scapy.config import conf as scapy_conf
from scapy.sendrecv import AsyncSniffer

from .utils import netns_context

# TCPOPT numbers are apparently not available in scapy
TCPOPT_MD5SIG = 19
TCPOPT_AUTHOPT = 29

# Easy generic handling of IPv4/IPv6Address
IPvXAddress = typing.Union[IPv4Address, IPv6Address]


def get_packet_ipvx_src(p: Packet) -> IPvXAddress:
    if IP in p:
        return IPv4Address(p[IP].src)
    elif IPv6 in p:
        return IPv6Address(p[IPv6].src)
    else:
        raise Exception("Neither IP nor IPv6 found on packet")


def get_packet_ipvx_dst(p: Packet) -> IPvXAddress:
    if IP in p:
        return IPv4Address(p[IP].dst)
    elif IPv6 in p:
        return IPv6Address(p[IPv6].dst)
    else:
        raise Exception("Neither IP nor IPv6 found on packet")


def get_tcp_doff(th: TCP):
    """Get the TCP data offset, even if packet is not yet built"""
    doff = th.dataofs
    if doff is None:
        opt_len = len(th.get_field("options").i2m(th, th.options))
        doff = 5 + ((opt_len + 3) // 4)
    return doff


def get_tcp_v4_pseudoheader(tcp_packet: TCP) -> bytes:
    iph = tcp_packet.underlayer
    return struct.pack(
        "!4s4sHH",
        IPv4Address(iph.src).packed,
        IPv4Address(iph.dst).packed,
        socket.IPPROTO_TCP,
        get_tcp_doff(tcp_packet) * 4 + len(tcp_packet.payload),
    )


def get_tcp_v6_pseudoheader(tcp_packet: TCP) -> bytes:
    ipv6 = tcp_packet.underlayer
    return struct.pack(
        "!16s16sII",
        IPv6Address(ipv6.src).packed,
        IPv6Address(ipv6.dst).packed,
        get_tcp_doff(tcp_packet) * 4 + len(tcp_packet.payload),
        socket.IPPROTO_TCP,
    )


def get_tcp_pseudoheader(tcp_packet: TCP):
    if isinstance(tcp_packet.underlayer, IP):
        return get_tcp_v4_pseudoheader(tcp_packet)
    if isinstance(tcp_packet.underlayer, IPv6):
        return get_tcp_v6_pseudoheader(tcp_packet)
    raise ValueError("TCP underlayer is neither IP nor IPv6")


def tcp_seq_wrap(seq):
    return seq & 0xFFFFFFFF


@dataclass
class tcphdr_authopt:
    """Representation of a TCP auth option as it appears in a TCP packet"""

    keyid: int
    rnextkeyid: int
    mac: bytes

    @classmethod
    def unpack(cls, buf) -> "tcphdr_authopt":
        return cls(buf[0], buf[1], buf[2:])

    def __repr__(self):
        return f"tcphdr_authopt({self.keyid}, {self.rnextkeyid}, bytes.fromhex({self.mac.hex(' ')!r})"


def scapy_tcp_get_authopt_val(tcp) -> typing.Optional[tcphdr_authopt]:
    for optnum, optval in tcp.options:
        if optnum == TCPOPT_AUTHOPT:
            return tcphdr_authopt.unpack(optval)
    return None


def scapy_tcp_get_md5_sig(tcp) -> typing.Optional[bytes]:
    """Return the MD5 signature (as bytes) or None"""
    for optnum, optval in tcp.options:
        if optnum == TCPOPT_MD5SIG:
            return optval
    return None


def calc_tcp_md5_hash(p, key: bytes) -> bytes:
    """Calculate TCP-MD5 hash from packet and return a 16-byte string"""
    import hashlib

    h = hashlib.md5()
    tp = p[TCP]
    th_bytes = bytes(p[TCP])
    h.update(get_tcp_pseudoheader(tp))
    h.update(th_bytes[:16])
    h.update(b"\x00\x00")
    h.update(th_bytes[18:20])
    h.update(bytes(tp.payload))
    h.update(key)

    return h.digest()


def create_l2socket(ns: str = "", **kw):
    """Create a scapy L2socket inside a namespace"""

    with netns_context(ns):
        return scapy_conf.L2socket(**kw)


def create_capture_socket(ns: str = "", **kw):
    """Create a scapy L2listen socket inside a namespace"""
    from scapy.config import conf as scapy_conf

    with netns_context(ns):
        return scapy_conf.L2listen(**kw)


def scapy_sniffer_start_block(sniffer: AsyncSniffer, timeout=1):
    """Like AsyncSniffer.start except block until sniffing starts

    This ensures no lost packets and no delays
    """
    if sniffer.kwargs.get("started_callback"):
        raise ValueError("sniffer must not already have a started_callback")

    e = threading.Event()
    sniffer.kwargs["started_callback"] = e.set
    sniffer.start()
    e.wait(timeout=timeout)
    if not e.is_set():
        raise TimeoutError("Timed out waiting for sniffer to start")


def scapy_sniffer_stop(sniffer: AsyncSniffer):
    """Like AsyncSniffer.stop except no error is raising if not running"""
    if sniffer is not None and sniffer.running:
        sniffer.stop()


class AsyncSnifferContext(AsyncSniffer):
    def __enter__(self):
        scapy_sniffer_start_block(self)
        return self

    def __exit__(self, *a):
        scapy_sniffer_stop(self)
