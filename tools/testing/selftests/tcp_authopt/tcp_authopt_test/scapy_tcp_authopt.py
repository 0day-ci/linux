# SPDX-License-Identifier: GPL-2.0
"""Packet-processing utilities implementing RFC5925 and RFC2926"""

import logging
from scapy.layers.inet import TCP
from scapy.packet import Packet
from .scapy_utils import TCPOPT_AUTHOPT, IPvXAddress, get_packet_ipvx_src, get_packet_ipvx_dst, get_tcp_pseudoheader, get_tcp_doff
import struct
import hmac

logger = logging.getLogger(__name__)


def _cmac_aes_digest(key: bytes, msg: bytes) -> bytes:
    from cryptography.hazmat.primitives import cmac
    from cryptography.hazmat.primitives.ciphers import algorithms
    from cryptography.hazmat.backends import default_backend

    backend = default_backend()
    c = cmac.CMAC(algorithms.AES(key), backend=backend)
    c.update(bytes(msg))
    return c.finalize()


class TcpAuthOptAlg:
    @classmethod
    def kdf(cls, master_key: bytes, context: bytes) -> bytes:
        raise NotImplementedError()

    @classmethod
    def mac(cls, traffic_key: bytes, message: bytes) -> bytes:
        raise NotImplementedError()

    maclen = -1


class TcpAuthOptAlg_HMAC_SHA1(TcpAuthOptAlg):
    @classmethod
    def kdf(cls, master_key: bytes, context: bytes) -> bytes:
        input = b"\x01" + b"TCP-AO" + context + b"\x00\xa0"
        return hmac.digest(master_key, input, "SHA1")

    @classmethod
    def mac(cls, traffic_key: bytes, message: bytes) -> bytes:
        return hmac.digest(traffic_key, message, "SHA1")[:12]

    maclen = 12


class TcpAuthOptAlg_CMAC_AES(TcpAuthOptAlg):
    @classmethod
    def kdf(self, master_key: bytes, context: bytes) -> bytes:
        if len(master_key) == 16:
            key = master_key
        else:
            key = _cmac_aes_digest(b"\x00" * 16, master_key)
        return _cmac_aes_digest(key, b"\x01" + b"TCP-AO" + context + b"\x00\x80")

    @classmethod
    def mac(self, traffic_key: bytes, message: bytes) -> bytes:
        return _cmac_aes_digest(traffic_key, message)[:12]

    maclen = 12


def get_alg(name: str) -> TcpAuthOptAlg:
    if name.upper() == "HMAC-SHA-1-96":
        return TcpAuthOptAlg_HMAC_SHA1()
    elif name.upper() == "AES-128-CMAC-96":
        return TcpAuthOptAlg_CMAC_AES()
    else:
        raise ValueError(f"Bad TCP AuthOpt algorithms {name}")


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


def build_context_from_packet(p: Packet, src_isn: int, dst_isn: int) -> bytes:
    """Build context based on a scapy Packet and src/dst initial-sequence numbers"""
    return build_context(
        get_packet_ipvx_src(p),
        get_packet_ipvx_dst(p),
        p[TCP].sport,
        p[TCP].dport,
        src_isn,
        dst_isn,
    )


def build_message_from_packet(p: Packet, include_options=True, sne=0) -> bytearray:
    """Build message bytes as described by RFC5925 section 5.1"""
    result = bytearray()
    result += struct.pack("!I", sne)
    th = p[TCP]

    # ip pseudo-header:
    result += get_tcp_pseudoheader(th)

    # tcp header with checksum set to zero
    th_bytes = bytes(p[TCP])
    result += th_bytes[:16]
    result += b"\x00\x00"
    result += th_bytes[18:20]

    # Even if include_options=False the TCP-AO option itself is still included
    # with the MAC set to all-zeros. This means we need to parse TCP options.
    pos = 20
    tcphdr_optend = get_tcp_doff(th) * 4
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
        if optnum == TCPOPT_AUTHOPT:
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


def check_tcp_authopt_signature(
    p: Packet, alg: TcpAuthOptAlg, master_key, sisn, disn, include_options=True, sne=0
):
    from .scapy_utils import scapy_tcp_get_authopt_val

    ao = scapy_tcp_get_authopt_val(p[TCP])
    if ao is None:
        return None

    context_bytes = build_context_from_packet(p, sisn, disn)
    traffic_key = alg.kdf(master_key, context_bytes)
    message_bytes = build_message_from_packet(
        p, include_options=include_options, sne=sne
    )
    mac = alg.mac(traffic_key, message_bytes)
    return mac == ao.mac


def add_tcp_authopt_signature(
    p: Packet,
    alg: TcpAuthOptAlg,
    master_key,
    sisn,
    disn,
    keyid=0,
    rnextkeyid=0,
    include_options=True,
    sne=0,
):
    """Sign a packet"""
    th = p[TCP]
    keyids = struct.pack("BB", keyid, rnextkeyid)
    th.options = th.options + [(TCPOPT_AUTHOPT, keyids + alg.maclen * b"\x00")]

    context_bytes = build_context_from_packet(p, sisn, disn)
    traffic_key = alg.kdf(master_key, context_bytes)
    message_bytes = build_message_from_packet(
        p, include_options=include_options, sne=sne
    )
    mac = alg.mac(traffic_key, message_bytes)
    th.options[-1] = (TCPOPT_AUTHOPT, keyids + mac)


def break_tcp_authopt_signature(packet: Packet):
    """Invalidate TCP-AO signature inside a packet

    The packet must already be signed and it gets modified in-place.
    """
    opt = packet[TCP].options[-1]
    if opt[0] != TCPOPT_AUTHOPT:
        raise ValueError("TCP option list must end with TCP_AUTHOPT")
    opt_mac = bytearray(opt[1])
    opt_mac[-1] ^= 0xFF
    packet[TCP].options[-1] = (opt[0], bytes(opt_mac))
