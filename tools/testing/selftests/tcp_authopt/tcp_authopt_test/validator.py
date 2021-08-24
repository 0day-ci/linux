# SPDX-License-Identifier: GPL-2.0
from tcp_authopt_test.utils import scapy_tcp_get_authopt_val
import typing
import logging

from dataclasses import dataclass
from scapy.packet import Packet
from scapy.layers.inet import TCP
from . import tcp_authopt_alg
from .tcp_authopt_alg import IPvXAddress, TCPAuthContext
from .tcp_authopt_alg import get_scapy_ipvx_src
from .tcp_authopt_alg import get_scapy_ipvx_dst

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class TCPSocketPair:
    """TCP connection identifier"""

    saddr: IPvXAddress = None
    daddr: IPvXAddress = None
    sport: int = 0
    dport: int = 0

    def rev(self) -> "TCPSocketPair":
        return TCPSocketPair(self.daddr, self.saddr, self.dport, self.sport)


@dataclass
class TcpAuthValidatorKey:
    key: bytes
    alg_name: str
    include_options: bool = True
    keyid: typing.Optional[int] = None
    sport: typing.Optional[int] = None
    dport: typing.Optional[int] = None

    def match_packet(self, p: Packet):
        if not TCP in p:
            return False
        authopt = scapy_tcp_get_authopt_val(p[TCP])
        if authopt is None:
            return False
        if self.keyid is not None and authopt.keyid != self.keyid:
            return False
        if self.sport is not None and p[TCP].sport != self.sport:
            return False
        if self.dport is not None and p[TCP].dport != self.dport:
            return False
        return True

    def get_alg_imp(self):
        return tcp_authopt_alg.get_alg(self.alg_name)


def is_init_syn(p: Packet) -> bool:
    return p[TCP].flags.S and not p[TCP].flags.A


class TcpAuthValidator:
    """Validate TCP auth sessions inside a capture"""

    keys: typing.List[TcpAuthValidatorKey]
    conn_dict: typing.Dict[TCPSocketPair, TCPAuthContext]
    any_incomplete: bool = False
    any_unsigned: bool = False
    any_fail: bool = False

    def __init__(self, keys=None):
        self.keys = keys or []
        self.conn_dict = {}

    def get_key_for_packet(self, p):
        for k in self.keys:
            if k.match_packet(p):
                return k
        return None

    def handle_packet(self, p: Packet):
        if TCP not in p:
            logger.debug("skip non-TCP packet")
            return
        key = self.get_key_for_packet(p)
        if not key:
            self.any_unsigned = True
            logger.debug("skip packet not matching any known keys: %r", p)
            return
        authopt = scapy_tcp_get_authopt_val(p[TCP])
        if not authopt:
            self.any_unsigned = True
            logger.debug("skip packet without tcp authopt: %r", p)
            return
        captured_mac = authopt.mac

        saddr = get_scapy_ipvx_src(p)
        daddr = get_scapy_ipvx_dst(p)

        conn_key = TCPSocketPair(saddr, daddr, p[TCP].sport, p[TCP].dport)
        if p[TCP].flags.S:
            conn = self.conn_dict.get(conn_key, None)
            if conn is not None:
                logger.warning("overwrite %r", conn)
                self.any_incomplete = True
            conn = TCPAuthContext()
            conn.saddr = saddr
            conn.daddr = daddr
            conn.sport = p[TCP].sport
            conn.dport = p[TCP].dport
            self.conn_dict[conn_key] = conn

            if p[TCP].flags.A == False:
                # SYN
                conn.sisn = p[TCP].seq
                conn.disn = 0
                logger.info("Initialized for SYN: %r", conn)
            else:
                # SYN/ACK
                conn.sisn = p[TCP].seq
                conn.disn = p[TCP].ack - 1
                logger.info("Initialized for SYNACK: %r", conn)

                # Update opposite connection with dst_isn
                rconn_key = conn_key.rev()
                rconn = self.conn_dict.get(rconn_key, None)
                if rconn is None:
                    logger.warning("missing SYN for SYNACK: %s", rconn_key)
                    self.any_incomplete = True
                else:
                    assert rconn.sisn == conn.disn
                    assert rconn.disn == 0 or rconn.disn == conn.sisn
                    rconn.disn = conn.sisn
                    rconn.update_from_synack_packet(p)
                    logger.info("Updated peer for SYNACK: %r", rconn)
        else:
            conn = self.conn_dict.get(conn_key, None)
            if conn is None:
                logger.warning("missing TCP syn for %r", conn_key)
                self.any_incomplete = True
                return
        # logger.debug("conn %r found for packet %r", conn, p)

        context_bytes = conn.pack(syn=is_init_syn(p))
        alg = key.get_alg_imp()
        traffic_key = alg.kdf(key.key, context_bytes)
        message_bytes = tcp_authopt_alg.build_message_from_scapy(
            p, include_options=key.include_options
        )
        computed_mac = alg.mac(traffic_key, message_bytes)
        if computed_mac == captured_mac:
            logger.debug("ok - mac %s", computed_mac.hex())
        else:
            self.any_fail = True
            logger.error(
                "not ok - captured %s computed %s",
                captured_mac.hex(),
                computed_mac.hex(),
            )
