# SPDX-License-Identifier: GPL-2.0
"""Packet-processing utilities implementing RFC5925 and RFC2926"""

import logging
from dataclasses import dataclass
from ipaddress import IPv4Address, IPv6Address
from scapy.layers.inet import IP, TCP
from scapy.layers.inet6 import IPv6
from scapy.packet import Packet
import socket
import struct
import typing
import hmac

logger = logging.getLogger(__name__)


def kdf_sha1(master_key: bytes, context: bytes) -> bytes:
    """RFC5926 section 3.1.1.1"""
    input = b"\x01" + b"TCP-AO" + context + b"\x00\xa0"
    return hmac.digest(master_key, input, "SHA1")


def mac_sha1(traffic_key: bytes, message: bytes) -> bytes:
    """RFC5926 section 3.2.1"""
    return hmac.digest(traffic_key, message, "SHA1")[:12]


def cmac_aes_digest(key: bytes, msg: bytes) -> bytes:
    from cryptography.hazmat.primitives import cmac
    from cryptography.hazmat.primitives.ciphers import algorithms
    from cryptography.hazmat.backends import default_backend

    backend = default_backend()
    c = cmac.CMAC(algorithms.AES(key), backend=backend)
    c.update(bytes(msg))
    return c.finalize()


def kdf_cmac_aes(master_key: bytes, context: bytes) -> bytes:
    if len(master_key) == 16:
        key = master_key
    else:
        key = cmac_aes_digest(b"\x00" * 16, master_key)
    return cmac_aes_digest(key, b"\x01" + b"TCP-AO" + context + b"\x00\x80")


def mac_cmac_aes(traffic_key: bytes, message: bytes) -> bytes:
    return cmac_aes_digest(traffic_key, message)[:12]


class TcpAuthOptAlg:
    def kdf(self, master_key: bytes, context: bytes) -> bytes:
        raise NotImplementedError()

    def mac(self, traffic_key: bytes, message: bytes) -> bytes:
        raise NotImplementedError()


class TcpAuthOptAlg_HMAC_SHA1(TcpAuthOptAlg):
    def kdf(self, master_key: bytes, context: bytes) -> bytes:
        return kdf_sha1(master_key, context)

    def mac(self, traffic_key: bytes, message: bytes) -> bytes:
        return mac_sha1(traffic_key, message)


class TcpAuthOptAlg_CMAC_AES(TcpAuthOptAlg):
    def kdf(self, master_key: bytes, context: bytes) -> bytes:
        return kdf_cmac_aes(master_key, context)

    def mac(self, traffic_key: bytes, message: bytes) -> bytes:
        return mac_cmac_aes(traffic_key, message)


def get_alg(name) -> TcpAuthOptAlg:
    if name.upper() == "HMAC-SHA-1-96":
        return TcpAuthOptAlg_HMAC_SHA1()
    elif name.upper() == "AES-128-CMAC-96":
        return TcpAuthOptAlg_CMAC_AES()
    else:
        raise ValueError(f"Bad TCP AuthOpt algorithms {name}")


IPvXAddress = typing.Union[IPv4Address, IPv6Address]


def get_scapy_ipvx_src(p: Packet) -> IPvXAddress:
    if IP in p:
        return IPv4Address(p[IP].src)
    elif IPv6 in p:
        return IPv6Address(p[IPv6].src)
    else:
        raise Exception("Neither IP nor IPv6 found on packet")


def get_scapy_ipvx_dst(p: Packet) -> IPvXAddress:
    if IP in p:
        return IPv4Address(p[IP].dst)
    elif IPv6 in p:
        return IPv6Address(p[IPv6].dst)
    else:
        raise Exception("Neither IP nor IPv6 found on packet")


def build_context(
    saddr: IPvXAddress, daddr: IPvXAddress, sport, dport, src_isn, dst_isn
) -> bytes:
    """Build context bytes as specified by RFC5925 section 5.2"""
    return (
        saddr.packed
        + daddr.packed
        + struct.pack(
            "!HHII",
            sport,
            dport,
            src_isn,
            dst_isn,
        )
    )


def build_context_from_scapy(p: Packet, src_isn: int, dst_isn: int) -> bytes:
    """Build context based on a scapy Packet and src/dst initial-sequence numbers"""
    return build_context(
        get_scapy_ipvx_src(p),
        get_scapy_ipvx_dst(p),
        p[TCP].sport,
        p[TCP].dport,
        src_isn,
        dst_isn,
    )


def build_context_from_scapy_syn(p: Packet) -> bytes:
    """Build context for a scapy SYN packet"""
    return build_context_from_scapy(p, p[TCP].seq, 0)


def build_context_from_scapy_synack(p: Packet) -> bytes:
    """Build context for a scapy SYN/ACK packet"""
    return build_context_from_scapy(p, p[TCP].seq, p[TCP].ack - 1)


def build_message_from_scapy(p: Packet, include_options=True, sne=0) -> bytearray:
    """Build message bytes as described by RFC5925 section 5.1"""
    result = bytearray()
    result += struct.pack("!I", sne)
    # ip pseudo-header:
    if IP in p:
        result += struct.pack(
            "!4s4sHH",
            IPv4Address(p[IP].src).packed,
            IPv4Address(p[IP].dst).packed,
            socket.IPPROTO_TCP,
            p[TCP].dataofs * 4 + len(p[TCP].payload),
        )
        assert p[TCP].dataofs * 4 + len(p[TCP].payload) + p[IP].ihl * 4 == p[IP].len
    elif IPv6 in p:
        result += struct.pack(
            "!16s16sII",
            IPv6Address(p[IPv6].src).packed,
            IPv6Address(p[IPv6].dst).packed,
            p[IPv6].plen,
            socket.IPPROTO_TCP,
        )
        assert p[TCP].dataofs * 4 + len(p[TCP].payload) == p[IPv6].plen
    else:
        raise Exception("Neither IP nor IPv6 found on packet")

    # tcp header with checksum set to zero
    th_bytes = bytes(p[TCP])
    result += th_bytes[:16]
    result += b"\x00\x00"
    result += th_bytes[18:20]

    # Even if include_options=False the TCP-AO option itself is still included
    # with the MAC set to all-zeros. This means we need to parse TCP options.
    pos = 20
    tcphdr_optend = p[TCP].dataofs * 4
    # logger.info("th_bytes: %s", th_bytes.hex(' '))
    assert len(th_bytes) >= tcphdr_optend
    while pos < tcphdr_optend:
        optnum = th_bytes[pos]
        pos += 1
        if optnum == 0 or optnum == 1:
            if include_options:
                result += bytes([optnum])
            continue

        optlen = th_bytes[pos]
        pos += 1
        if pos + optlen - 2 > tcphdr_optend:
            logger.info(
                "bad tcp option %d optlen %d beyond end-of-header", optnum, optlen
            )
            break
        if optlen < 2:
            logger.info("bad tcp option %d optlen %d less than two", optnum, optlen)
            break
        if optnum == 29:
            if optlen < 4:
                logger.info("bad tcp option %d optlen %d", optnum, optlen)
                break
            result += bytes([optnum, optlen])
            result += th_bytes[pos : pos + 2]
            result += (optlen - 4) * b"\x00"
        elif include_options:
            result += bytes([optnum, optlen])
            result += th_bytes[pos : pos + optlen - 2]
        pos += optlen - 2
    result += bytes(p[TCP].payload)
    return result


@dataclass
class TCPAuthContext:
    """Context used to TCP Authentication option as defined in RFC5925 5.2"""

    saddr: IPvXAddress = None
    daddr: IPvXAddress = None
    sport: int = 0
    dport: int = 0
    sisn: int = 0
    disn: int = 0

    def pack(self, syn=False, rev=False) -> bytes:
        if rev:
            return build_context(
                self.daddr,
                self.saddr,
                self.dport,
                self.sport,
                self.disn if not syn else 0,
                self.sisn,
            )
        else:
            return build_context(
                self.saddr,
                self.daddr,
                self.sport,
                self.dport,
                self.sisn,
                self.disn if not syn else 0,
            )

    def rev(self) -> "TCPAuthContext":
        """Reverse"""
        return TCPAuthContext(
            saddr=self.daddr,
            daddr=self.saddr,
            sport=self.dport,
            dport=self.sport,
            sisn=self.disn,
            disn=self.sisn,
        )

    def init_from_syn_packet(self, p):
        """Init from a SYN packet (and set dist to zero)"""
        assert p[TCP].flags.S and not p[TCP].flags.A and p[TCP].ack == 0
        self.saddr = get_scapy_ipvx_src(p)
        self.daddr = get_scapy_ipvx_dst(p)
        self.sport = p[TCP].sport
        self.dport = p[TCP].dport
        self.sisn = p[TCP].seq
        self.disn = 0

    def update_from_synack_packet(self, p):
        """Update disn and check everything else matches"""
        assert p[TCP].flags.S and p[TCP].flags.A
        assert self.saddr == get_scapy_ipvx_dst(p)
        assert self.daddr == get_scapy_ipvx_src(p)
        assert self.sport == p[TCP].dport
        assert self.dport == p[TCP].sport
        assert self.sisn == p[TCP].ack - 1
        self.disn = p[TCP].seq
