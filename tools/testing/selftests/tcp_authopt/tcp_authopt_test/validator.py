# SPDX-License-Identifier: GPL-2.0
import logging
import typing
from dataclasses import dataclass

from scapy.layers.inet import TCP
from scapy.packet import Packet

from . import scapy_tcp_authopt
from .scapy_conntrack import TCPConnectionTracker, get_packet_tcp_connection_key
from .scapy_utils import scapy_tcp_get_authopt_val

logger = logging.getLogger(__name__)


@dataclass
class TcpAuthValidatorKey:
    """Representation of a TCP Authentication Option key for the validator

    The matching rules are independent.
    """

    key: bytes
    alg_name: str
    include_options: bool = True
    keyid: typing.Optional[int] = None
    sport: typing.Optional[int] = None
    dport: typing.Optional[int] = None

    def match_packet(self, p: Packet) -> bool:
        """Determine if this key matches a specific packet"""
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
        return scapy_tcp_authopt.get_alg(self.alg_name)


class TcpAuthValidator:
    """Validate TCP Authentication Option signatures inside a capture

    This can track multiple connections, determine their initial sequence numbers
    and verify their signatues independently.

    Keys are provided as a collection of `.TcpAuthValidatorKey`
    """

    keys: typing.List[TcpAuthValidatorKey]
    tracker: TCPConnectionTracker
    any_incomplete: bool = False
    any_unsigned: bool = False
    any_fail: bool = False

    def __init__(self, keys=None):
        self.keys = keys or []
        self.tracker = TCPConnectionTracker()
        self.conn_dict = {}

    def get_key_for_packet(self, p):
        for k in self.keys:
            if k.match_packet(p):
                return k
        return None

    def handle_packet(self, p: Packet):
        if not TCP in p:
            return
        self.tracker.handle_packet(p)
        authopt = scapy_tcp_get_authopt_val(p[TCP])
        if not authopt:
            self.any_unsigned = True
            logger.debug("skip packet without tcp authopt: %r", p)
            return
        key = self.get_key_for_packet(p)
        if not key:
            self.any_unsigned = True
            logger.debug("skip packet not matching any known keys: %r", p)
            return
        tcp_track_key = get_packet_tcp_connection_key(p)
        conn = self.tracker.get(tcp_track_key)

        if not conn.found_syn:
            logger.warning("missing SYN for %s", p)
            self.any_incomplete = True
            return
        if not conn.found_synack and not p[TCP].flags.S:
            logger.warning("missing SYNACK for %s", p)
            self.any_incomplete = True
            return

        alg = key.get_alg_imp()
        context_bytes = scapy_tcp_authopt.build_context_from_packet(
            p, conn.sisn or 0, conn.disn or 0
        )
        traffic_key = alg.kdf(key.key, context_bytes)
        message_bytes = scapy_tcp_authopt.build_message_from_packet(
            p, include_options=key.include_options
        )
        computed_mac = alg.mac(traffic_key, message_bytes)
        captured_mac = authopt.mac
        if computed_mac == captured_mac:
            logger.debug("ok - mac %s", computed_mac.hex())
        else:
            self.any_fail = True
            logger.error(
                "not ok - captured %s computed %s",
                captured_mac.hex(),
                computed_mac.hex(),
            )

    def raise_errors(self, allow_unsigned=False, allow_incomplete=False):
        if self.any_fail:
            raise Exception("Found failed signatures")
        if self.any_incomplete and not allow_incomplete:
            raise Exception("Incomplete capture missing SYN/ACK")
        if self.any_unsigned and not allow_unsigned:
            raise Exception("Found unsigned packets")
