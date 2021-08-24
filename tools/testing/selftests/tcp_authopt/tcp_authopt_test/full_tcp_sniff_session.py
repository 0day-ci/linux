# SPDX-License-Identifier: GPL-2.0
import scapy.sessions
from scapy.layers.inet import TCP

from .utils import SimpleWaitEvent


class FullTCPSniffSession(scapy.sessions.DefaultSession):
    """Implementation of a scapy sniff session that can wait for a full TCP capture

    Allows another thread to wait for a complete FIN handshake without polling or sleep.
    """

    found_syn = False
    found_synack = False
    found_fin = False
    found_client_fin = False
    found_server_fin = False

    def __init__(self, server_port=None, **kw):
        super().__init__(**kw)
        self.server_port = server_port
        self._close_event = SimpleWaitEvent()

    def on_packet_received(self, p):
        super().on_packet_received(p)
        if not p or not TCP in p:
            return
        th = p[TCP]
        # logger.debug("sport=%d dport=%d flags=%s", th.sport, th.dport, th.flags)
        if th.flags.S and not th.flags.A:
            if th.dport == self.server_port or self.server_port is None:
                self.found_syn = True
        if th.flags.S and th.flags.A:
            if th.sport == self.server_port or self.server_port is None:
                self.found_synack = True
        if th.flags.F:
            if self.server_port is None:
                self.found_fin = True
                self._close_event.set()
            elif self.server_port == th.dport:
                self.found_client_fin = True
                self.found_fin = True
                if self.found_server_fin and self.found_client_fin:
                    self._close_event.set()
            elif self.server_port == th.sport:
                self.found_server_fin = True
                self.found_fin = True
                if self.found_server_fin and self.found_client_fin:
                    self._close_event.set()

    def wait_close(self, timeout=10):
        self._close_event.wait(timeout=timeout)
