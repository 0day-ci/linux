# SPDX-License-Identifier: GPL-2.0
"""Validate SNE implementation for TCP-AO"""

import logging
import socket
from contextlib import ExitStack
from ipaddress import ip_address

import pytest

from .linux_tcp_authopt import set_tcp_authopt_key_kwargs
from .linux_tcp_repair import get_tcp_repair_recv_send_queue_seq, tcp_repair_toggle
from .netns_fixture import NamespaceFixture
from .scapy_conntrack import TCPConnectionKey, TCPConnectionTracker
from .scapy_utils import AsyncSnifferContext, create_capture_socket, tcp_seq_wrap
from .server import SimpleServerThread
from .utils import (
    DEFAULT_TCP_SERVER_PORT,
    check_socket_echo,
    create_client_socket,
    create_listen_socket,
    socket_set_linger,
)
from .validator import TcpAuthValidator, TcpAuthValidatorKey

logger = logging.getLogger(__name__)


def add_connection_info(
    tracker: TCPConnectionTracker,
    saddr,
    daddr,
    sport,
    dport,
    sisn,
    disn,
):
    client2server_key = TCPConnectionKey(
        saddr=saddr,
        daddr=daddr,
        sport=sport,
        dport=dport,
    )
    client2server_conn = tracker.get_or_create(client2server_key)
    client2server_conn.sisn = sisn
    client2server_conn.disn = disn
    client2server_conn.snd_sne.reset(sisn)
    client2server_conn.rcv_sne.reset(disn)
    client2server_conn.found_syn = True
    client2server_conn.found_synack = True
    server2client_conn = tracker.get_or_create(client2server_key.rev())
    server2client_conn.sisn = disn
    server2client_conn.disn = sisn
    server2client_conn.snd_sne.reset(disn)
    server2client_conn.rcv_sne.reset(sisn)
    server2client_conn.found_syn = True
    server2client_conn.found_synack = True


@pytest.mark.parametrize("signed", [False, True])
def test_high_seq_rollover(exit_stack: ExitStack, signed: bool):
    """Test SNE by rolling over from a high seq/ack value

    Create many connections until a very high seq/ack is found and then transfer
    enough for those values to roll over.

    A side effect of this approach is that this stresses connection
    establishment.
    """
    overflow = 0x200000
    bufsize = 0x10000
    secret_key = b"12345"
    mode = "echo"
    validator_enabled = True

    nsfixture = exit_stack.enter_context(NamespaceFixture())
    server_addr = nsfixture.get_addr(socket.AF_INET, 1)
    client_addr = nsfixture.get_addr(socket.AF_INET, 2)
    server_addr_port = (str(server_addr), DEFAULT_TCP_SERVER_PORT)
    listen_socket = create_listen_socket(
        ns=nsfixture.server_netns_name,
        bind_addr=server_addr,
        listen_depth=1024,
    )
    exit_stack.enter_context(listen_socket)
    if signed:
        set_tcp_authopt_key_kwargs(listen_socket, key=secret_key)
    server_thread = SimpleServerThread(listen_socket, mode=mode, bufsize=bufsize)
    exit_stack.enter_context(server_thread)

    found = False
    client_socket = None
    for iternum in range(50000):
        try:
            # Manually assign increasing client ports
            #
            # Sometimes linux kills timewait sockets (TCPTimeWaitOverflow) and
            # then attempts to reuse the port. The stricter validation
            # requirements of TCP-AO mean the other side of the socket survives
            # and rejects packets coming from the reused port.
            #
            # This issue is not related to SNE so a workaround is acceptable.
            client_socket = create_client_socket(
                ns=nsfixture.client_netns_name,
                bind_addr=client_addr,
                bind_port=10000 + iternum,
            )
            if signed:
                set_tcp_authopt_key_kwargs(client_socket, key=secret_key)
            try:
                client_socket.connect(server_addr_port)
            except:
                logger.error("failed connect on iteration %d", iternum, exc_info=True)
                raise

            recv_seq, send_seq = get_tcp_repair_recv_send_queue_seq(client_socket)
            if (recv_seq + overflow > 0x100000000 and mode == "echo") or (
                send_seq + overflow > 0x100000000
            ):
                found = True
                break
            # Wait for graceful close to avoid swamping server listen queue.
            # This makes the test work even with a server listen_depth=1 but set
            # a very high value anyway.
            socket_set_linger(client_socket, 1, 1)
            client_socket.close()
            client_socket = None
        finally:
            if not found and client_socket:
                client_socket.close()
    assert found
    assert client_socket is not None

    logger.debug("setup recv_seq %08x send_seq %08x", recv_seq, send_seq)

    # Init validator
    if signed and validator_enabled:
        capture_filter = f"tcp port {DEFAULT_TCP_SERVER_PORT}"
        capture_socket = create_capture_socket(
            ns=nsfixture.client_netns_name,
            iface="veth0",
            filter=capture_filter,
        )
        sniffer = exit_stack.enter_context(
            AsyncSnifferContext(opened_socket=capture_socket)
        )
        validator = TcpAuthValidator()
        validator.keys.append(
            TcpAuthValidatorKey(key=secret_key, alg_name="HMAC-SHA-1-96")
        )

        # SYN+SYNACK is not captured so initialize connection info manually
        add_connection_info(
            validator.tracker,
            saddr=ip_address(client_addr),
            daddr=ip_address(server_addr),
            dport=client_socket.getpeername()[1],
            sport=client_socket.getsockname()[1],
            sisn=tcp_seq_wrap(send_seq - 1),
            disn=tcp_seq_wrap(recv_seq - 1),
        )

    logger.info("transfer %d bytes", 2 * overflow)
    fail_transfer = False
    for iternum in range(2 * overflow // bufsize):
        try:
            if mode == "recv":
                from .utils import randbytes

                send_buf = randbytes(bufsize)
                client_socket.sendall(send_buf)
            else:
                check_socket_echo(client_socket, bufsize)
        except:
            logger.error("failed traffic on iteration %d", iternum, exc_info=True)
            fail_transfer = True
            break

    new_recv_seq, new_send_seq = get_tcp_repair_recv_send_queue_seq(client_socket)
    logger.debug("final recv_seq %08x send_seq %08x", new_recv_seq, new_send_seq)
    assert new_recv_seq < recv_seq or new_send_seq < send_seq

    # Validate capture
    if signed and validator_enabled:
        sniffer.stop()
        for p in sniffer.results:
            validator.handle_packet(p)
        # Allow incomplete connections from FIN/ACK of connections dropped
        # because of low seq/ack
        validator.raise_errors(allow_incomplete=True)
        client_scappy_key = TCPConnectionKey(
            saddr=ip_address(client_addr),
            daddr=ip_address(server_addr),
            dport=client_socket.getpeername()[1],
            sport=client_socket.getsockname()[1],
        )
        client_scappy_conn = validator.tracker.get(client_scappy_key)
        snd_sne_rollover = client_scappy_conn.snd_sne.sne != 0
        rcv_sne_rollover = client_scappy_conn.rcv_sne.sne != 0
        assert snd_sne_rollover or rcv_sne_rollover

    assert not fail_transfer
