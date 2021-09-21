# SPDX-License-Identifier: GPL-2.0
import threading
import scapy.sessions
from scapy.packet import Packet
import typing
import logging
from .scapy_conntrack import TCPConnectionTracker, TCPConnectionInfo

logger = logging.getLogger(__name__)


class FullTCPSniffSession(scapy.sessions.DefaultSession):
    """Implementation of a scapy sniff session that can wait for a full TCP capture

    Allows another thread to wait for a complete FIN handshake without polling or sleep.
    """

    #: Server port used to identify client and server
    server_port: int
    #: Connection tracker
    tracker: TCPConnectionTracker

    def __init__(self, server_port, **kw):
        super().__init__(**kw)
        self.server_port = server_port
        self.tracker = TCPConnectionTracker()
        self._close_event = threading.Event()
        self._init_isn_event = threading.Event()
        self._client_info = None
        self._server_info = None

    @property
    def client_info(self) -> TCPConnectionInfo:
        if not self._client_info:
            self._client_info = self.tracker.match_one(dport=self.server_port)
        return self._client_info

    @property
    def server_info(self) -> TCPConnectionInfo:
        if not self._server_info:
            self._server_info = self.tracker.match_one(sport=self.server_port)
        return self._server_info

    @property
    def client_isn(self):
        return self.client_info.sisn

    @property
    def server_isn(self):
        return self.server_info.sisn

    def on_packet_received(self, p: Packet):
        super().on_packet_received(p)
        self.tracker.handle_packet(p)

        # check events:
        if self.client_info.sisn is not None and self.client_info.disn is not None:
            assert (
                self.client_info.sisn == self.server_info.disn
                and self.server_info.sisn == self.client_info.disn
            )
            self._init_isn_event.set()
        if self.client_info.found_recv_finack and self.server_info.found_recv_finack:
            self._close_event.set()

    def wait_close(self, timeout=10):
        """Wait for a graceful close with FINs acked by both side"""
        self._close_event.wait(timeout=timeout)
        if not self._close_event.is_set():
            raise TimeoutError("Timed out waiting for graceful close")

    def wait_init_isn(self, timeout=10):
        """Wait for both client_isn and server_isn to be determined"""
        self._init_isn_event.wait(timeout=timeout)
        if not self._init_isn_event.is_set():
            raise TimeoutError("Timed out waiting for Initial Sequence Numbers")

    def get_client_server_isn(self, timeout=10) -> typing.Tuple[int, int]:
        """Return client/server ISN, blocking until they are captured"""
        self.wait_init_isn(timeout=timeout)
        return self.client_isn, self.server_isn
