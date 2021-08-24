# SPDX-License-Identifier: GPL-2.0
"""Capture packets with TCP-AO and verify signatures"""

import logging
import os
import socket

import pytest

from . import linux_tcp_authopt, tcp_authopt_alg
from .full_tcp_sniff_session import FullTCPSniffSession
from .linux_tcp_authopt import (
    set_tcp_authopt_key,
    tcp_authopt_key,
)
from .server import SimpleServerThread
from .utils import (
    DEFAULT_TCP_SERVER_PORT,
    AsyncSnifferContext,
    check_socket_echo,
    create_listen_socket,
    nstat_json,
)
from .validator import TcpAuthValidator, TcpAuthValidatorKey
from .conftest import skipif_missing_tcp_authopt

logger = logging.getLogger(__name__)
pytestmark = skipif_missing_tcp_authopt


def can_capture():
    # This is too restrictive:
    return os.geteuid() == 0


skipif_cant_capture = pytest.mark.skipif(
    not can_capture(), reason="run as root to capture packets"
)


def get_alg_id(alg_name) -> int:
    if alg_name == "HMAC-SHA-1-96":
        return linux_tcp_authopt.TCP_AUTHOPT_ALG_HMAC_SHA_1_96
    elif alg_name == "AES-128-CMAC-96":
        return linux_tcp_authopt.TCP_AUTHOPT_ALG_AES_128_CMAC_96
    else:
        raise ValueError()


@skipif_cant_capture
@pytest.mark.parametrize(
    "address_family,alg_name,include_options",
    [
        (socket.AF_INET, "HMAC-SHA-1-96", True),
        (socket.AF_INET, "AES-128-CMAC-96", True),
        (socket.AF_INET, "AES-128-CMAC-96", False),
        (socket.AF_INET6, "HMAC-SHA-1-96", True),
        (socket.AF_INET6, "HMAC-SHA-1-96", False),
        (socket.AF_INET6, "AES-128-CMAC-96", True),
    ],
)
def test_verify_capture(exit_stack, address_family, alg_name, include_options):
    master_key = b"testvector"
    alg_id = get_alg_id(alg_name)

    session = FullTCPSniffSession(server_port=DEFAULT_TCP_SERVER_PORT)
    sniffer = exit_stack.enter_context(
        AsyncSnifferContext(
            filter=f"tcp port {DEFAULT_TCP_SERVER_PORT}",
            iface="lo",
            session=session,
        )
    )

    listen_socket = create_listen_socket(family=address_family)
    listen_socket = exit_stack.enter_context(listen_socket)
    exit_stack.enter_context(SimpleServerThread(listen_socket, mode="echo"))

    client_socket = socket.socket(address_family, socket.SOCK_STREAM)
    client_socket = exit_stack.push(client_socket)

    set_tcp_authopt_key(
        listen_socket,
        tcp_authopt_key(alg=alg_id, key=master_key, include_options=include_options),
    )
    set_tcp_authopt_key(
        client_socket,
        tcp_authopt_key(alg=alg_id, key=master_key, include_options=include_options),
    )

    # even if one signature is incorrect keep processing the capture
    old_nstat = nstat_json()
    valkey = TcpAuthValidatorKey(
        key=master_key, alg_name=alg_name, include_options=include_options
    )
    validator = TcpAuthValidator(keys=[valkey])

    try:
        client_socket.settimeout(1.0)
        client_socket.connect(("localhost", DEFAULT_TCP_SERVER_PORT))
        for _ in range(5):
            check_socket_echo(client_socket)
    except socket.timeout:
        logger.warning("socket timeout", exc_info=True)
        pass
    client_socket.close()
    session.wait_close()
    sniffer.stop()

    logger.info("capture: %r", sniffer.results)
    for p in sniffer.results:
        validator.handle_packet(p)

    assert not validator.any_fail
    assert not validator.any_unsigned
    # Fails because of duplicate packets:
    # assert not validator.any_incomplete
    new_nstat = nstat_json()
    assert (
        0
        == new_nstat["kernel"]["TcpExtTCPAuthOptFailure"]
        - old_nstat["kernel"]["TcpExtTCPAuthOptFailure"]
    )
