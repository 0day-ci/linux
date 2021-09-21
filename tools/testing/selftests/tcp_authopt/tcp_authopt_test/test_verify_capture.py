# SPDX-License-Identifier: GPL-2.0
"""Capture packets with TCP-AO and verify signatures"""

import logging
import os
import socket
import subprocess
from contextlib import ExitStack, nullcontext

import pytest
import waiting
from scapy.layers.inet import TCP

from .conftest import skipif_cant_capture, skipif_missing_tcp_authopt
from .full_tcp_sniff_session import FullTCPSniffSession
from .linux_tcp_authopt import (
    TCP_AUTHOPT_ALG,
    TCP_AUTHOPT_KEY_FLAG,
    set_tcp_authopt_key,
    tcp_authopt_key,
)
from .netns_fixture import NamespaceFixture
from .scapy_tcp_authopt import (
    TcpAuthOptAlg_HMAC_SHA1,
    add_tcp_authopt_signature,
    break_tcp_authopt_signature,
)
from .scapy_utils import (
    AsyncSnifferContext,
    scapy_sniffer_stop,
    scapy_tcp_get_authopt_val,
    scapy_tcp_get_md5_sig,
    tcp_seq_wrap,
)
from .server import SimpleServerThread
from .tcp_connection_fixture import TCPConnectionFixture
from .utils import (
    DEFAULT_TCP_SERVER_PORT,
    check_socket_echo,
    create_client_socket,
    create_listen_socket,
    nstat_json,
    socket_set_linger,
)
from .validator import TcpAuthValidator, TcpAuthValidatorKey

logger = logging.getLogger(__name__)
pytestmark = [skipif_missing_tcp_authopt, skipif_cant_capture]
DEFAULT_TCP_AUTHOPT_KEY = tcp_authopt_key(
    alg=TCP_AUTHOPT_ALG.HMAC_SHA_1_96,
    key=b"hello",
)


def get_alg_id(alg_name) -> int:
    if alg_name == "HMAC-SHA-1-96":
        return TCP_AUTHOPT_ALG.HMAC_SHA_1_96
    elif alg_name == "AES-128-CMAC-96":
        return TCP_AUTHOPT_ALG.AES_128_CMAC_96
    else:
        raise ValueError()


@pytest.mark.parametrize(
    "address_family,alg_name,include_options,transfer_data",
    [
        (socket.AF_INET, "HMAC-SHA-1-96", True, True),
        (socket.AF_INET, "AES-128-CMAC-96", True, True),
        (socket.AF_INET, "AES-128-CMAC-96", False, True),
        (socket.AF_INET6, "HMAC-SHA-1-96", True, True),
        (socket.AF_INET6, "HMAC-SHA-1-96", False, True),
        (socket.AF_INET6, "AES-128-CMAC-96", True, True),
        (socket.AF_INET, "HMAC-SHA-1-96", True, False),
        (socket.AF_INET6, "AES-128-CMAC-96", False, False),
    ],
)
def test_verify_capture(
    exit_stack, address_family, alg_name, include_options, transfer_data
):
    master_key = b"testvector"
    alg_id = get_alg_id(alg_name)

    session = FullTCPSniffSession(server_port=DEFAULT_TCP_SERVER_PORT)
    sniffer = exit_stack.enter_context(
        AsyncSnifferContext(
            filter=f"inbound and tcp port {DEFAULT_TCP_SERVER_PORT}",
            iface="lo",
            session=session,
        )
    )

    listen_socket = create_listen_socket(family=address_family)
    listen_socket = exit_stack.enter_context(listen_socket)
    exit_stack.enter_context(SimpleServerThread(listen_socket, mode="echo"))

    client_socket = socket.socket(address_family, socket.SOCK_STREAM)
    client_socket = exit_stack.push(client_socket)

    key = tcp_authopt_key(alg=alg_id, key=master_key, include_options=include_options)
    set_tcp_authopt_key(listen_socket, key)
    set_tcp_authopt_key(client_socket, key)

    # even if one signature is incorrect keep processing the capture
    old_nstat = nstat_json()
    valkey = TcpAuthValidatorKey(
        key=master_key, alg_name=alg_name, include_options=include_options
    )
    validator = TcpAuthValidator(keys=[valkey])

    try:
        client_socket.settimeout(1.0)
        client_socket.connect(("localhost", DEFAULT_TCP_SERVER_PORT))
        if transfer_data:
            for _ in range(5):
                check_socket_echo(client_socket)
        client_socket.close()
        session.wait_close()
    except socket.timeout:
        # If invalid packets are sent let the validator run
        logger.warning("socket timeout", exc_info=True)
        pass

    sniffer.stop()

    logger.info("capture: %r", sniffer.results)
    for p in sniffer.results:
        validator.handle_packet(p)
    validator.raise_errors()

    new_nstat = nstat_json()
    assert old_nstat["TcpExtTCPAuthOptFailure"] == new_nstat["TcpExtTCPAuthOptFailure"]


@pytest.mark.parametrize(
    "address_family,use_tcp_authopt,use_tcp_md5sig",
    [
        (socket.AF_INET, 0, 0),
        (socket.AF_INET, 1, 0),
        (socket.AF_INET, 0, 1),
        (socket.AF_INET6, 0, 0),
        (socket.AF_INET6, 1, 0),
        (socket.AF_INET6, 0, 1),
        (socket.AF_INET, 1, 1),
        (socket.AF_INET6, 1, 1),
    ],
)
def test_both_authopt_md5(exit_stack, address_family, use_tcp_authopt, use_tcp_md5sig):
    """Basic test for interaction between TCP_AUTHOPT and TCP_MD5SIG

    Configuring both on same socket is allowed but RFC5925 doesn't allow both on the
    same packet or same connection.

    The naive handling of inserting or validation both options is incorrect.
    """
    con = TCPConnectionFixture(address_family=address_family)
    if use_tcp_authopt:
        con.tcp_authopt_key = DEFAULT_TCP_AUTHOPT_KEY
    if use_tcp_md5sig:
        con.tcp_md5_key = b"hello"
    exit_stack.enter_context(con)

    con.client_socket.connect((str(con.server_addr), con.server_port))
    check_socket_echo(con.client_socket)
    check_socket_echo(con.client_socket)
    check_socket_echo(con.client_socket)
    con.client_socket.close()

    scapy_sniffer_stop(con.sniffer)
    fail = False
    for p in con.sniffer.results:
        has_tcp_authopt = scapy_tcp_get_authopt_val(p[TCP]) is not None
        has_tcp_md5sig = scapy_tcp_get_md5_sig(p[TCP]) is not None

        if has_tcp_authopt and has_tcp_md5sig:
            logger.error("Packet has both AO and MD5: %r", p)
            fail = False

        if use_tcp_authopt:
            if not has_tcp_authopt:
                logger.error("missing AO: %r", p)
                fail = True
        elif use_tcp_md5sig:
            if not has_tcp_md5sig:
                logger.error("missing MD5: %r", p)
                fail = True
        else:
            if has_tcp_md5sig or has_tcp_authopt:
                logger.error("unexpected MD5 or AO: %r", p)
                fail = True

    assert not fail


@pytest.mark.parametrize("mode", ["none", "ao", "ao-addrbind", "md5"])
def test_v4mapv6(exit_stack, mode: str):
    """Test ipv4 client and ipv6 server with and without TCP-AO

    By default any IPv6 server will also receive packets from IPv4 clients. This
    is not currently supported by TCP_AUTHOPT but it should fail in an orderly
    manner.
    """
    nsfixture = NamespaceFixture()
    exit_stack.enter_context(nsfixture)
    server_ipv4_addr = nsfixture.get_addr(socket.AF_INET, 1)

    listen_socket = create_listen_socket(
        ns=nsfixture.server_netns_name, family=socket.AF_INET6
    )
    listen_socket = exit_stack.enter_context(listen_socket)

    server_thread = SimpleServerThread(listen_socket, mode="echo")
    exit_stack.enter_context(server_thread)

    client_socket = create_client_socket(
        ns=nsfixture.client_netns_name,
        family=socket.AF_INET,
    )
    client_socket = exit_stack.push(client_socket)

    if mode == "ao":
        alg = TCP_AUTHOPT_ALG.HMAC_SHA_1_96
        key = tcp_authopt_key(alg=alg, key="hello")
        set_tcp_authopt_key(listen_socket, key)
        set_tcp_authopt_key(client_socket, key)

    if mode == "ao-addrbind":
        alg = TCP_AUTHOPT_ALG.HMAC_SHA_1_96
        client_ipv6_addr = nsfixture.get_addr(socket.AF_INET6, 2)
        server_key = tcp_authopt_key(alg=alg, key="hello", addr=client_ipv6_addr)
        server_key.flags = TCP_AUTHOPT_KEY_FLAG.BIND_ADDR
        set_tcp_authopt_key(listen_socket, server_key)

        client_key = tcp_authopt_key(alg=alg, key="hello")
        set_tcp_authopt_key(client_socket, client_key)

    if mode == "md5":
        from . import linux_tcp_md5sig

        server_key = linux_tcp_md5sig.tcp_md5sig(key=b"hello")
        server_key.set_ipv6_addr_all()
        linux_tcp_md5sig.setsockopt_md5sig(listen_socket, server_key)
        client_key = linux_tcp_md5sig.tcp_md5sig(key=b"hellx")
        client_key.set_ipv4_addr_all()
        linux_tcp_md5sig.setsockopt_md5sig(client_socket, client_key)

    with pytest.raises(socket.timeout) if mode != "none" else nullcontext():
        client_socket.connect((str(server_ipv4_addr), DEFAULT_TCP_SERVER_PORT))
        check_socket_echo(client_socket)
    client_socket.close()


@pytest.mark.parametrize(
    "address_family,signed",
    [
        (socket.AF_INET, True),
        (socket.AF_INET, False),
        (socket.AF_INET6, True),
        (socket.AF_INET6, False),
    ],
)
def test_rst(exit_stack: ExitStack, address_family, signed: bool):
    """Check that an unsigned RST breaks a normal connection but not one protected by TCP-AO"""

    con = TCPConnectionFixture(address_family=address_family)
    if signed:
        con.tcp_authopt_key = DEFAULT_TCP_AUTHOPT_KEY
    exit_stack.enter_context(con)

    # connect
    con.client_socket.connect((str(con.server_addr), con.server_port))
    check_socket_echo(con.client_socket)

    client_isn, server_isn = con.sniffer_session.get_client_server_isn()
    p = con.create_client2server_packet()
    p[TCP].flags = "R"
    p[TCP].seq = tcp_seq_wrap(client_isn + 1001)
    p[TCP].ack = tcp_seq_wrap(server_isn + 1001)
    con.client_l2socket.send(p)

    if signed:
        # When protected by TCP-AO unsigned RSTs are ignored.
        check_socket_echo(con.client_socket)
    else:
        # By default an RST that guesses seq can kill the connection.
        with pytest.raises(ConnectionResetError):
            check_socket_echo(con.client_socket)


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_rst_signed_manually(exit_stack: ExitStack, address_family):
    """Check that an manually signed RST breaks a connection protected by TCP-AO"""

    con = TCPConnectionFixture(address_family=address_family)
    con.tcp_authopt_key = key = DEFAULT_TCP_AUTHOPT_KEY
    exit_stack.enter_context(con)

    # connect
    con.client_socket.connect((str(con.server_addr), con.server_port))
    check_socket_echo(con.client_socket)

    client_isn, server_isn = con.sniffer_session.get_client_server_isn()
    p = con.create_client2server_packet()
    p[TCP].flags = "R"
    p[TCP].seq = tcp_seq_wrap(client_isn + 1001)
    p[TCP].ack = tcp_seq_wrap(server_isn + 1001)

    add_tcp_authopt_signature(
        p, TcpAuthOptAlg_HMAC_SHA1(), key.key, client_isn, server_isn
    )
    con.client_l2socket.send(p)

    # The server socket will close in response to RST without a TIME-WAIT
    # Attempting to send additional packets will result in a timeout because
    # the signature can't be validated.
    with pytest.raises(socket.timeout):
        check_socket_echo(con.client_socket)


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_tw_ack(exit_stack: ExitStack, address_family):
    """Manually sent a duplicate ACK after FIN and check TWSK signs replies correctly

    Kernel has a custom code path for this
    """

    con = TCPConnectionFixture(address_family=address_family)
    con.tcp_authopt_key = key = DEFAULT_TCP_AUTHOPT_KEY
    exit_stack.enter_context(con)

    # connect and close nicely
    con.client_socket.connect((str(con.server_addr), con.server_port))
    check_socket_echo(con.client_socket)
    assert con.get_client_tcp_state() == "ESTAB"
    assert con.get_server_tcp_state() == "ESTAB"
    con.client_socket.close()
    con.sniffer_session.wait_close()

    assert con.get_client_tcp_state() == "TIME-WAIT"
    assert con.get_server_tcp_state() is None

    # Sent a duplicate FIN/ACK
    client_isn, server_isn = con.sniffer_session.get_client_server_isn()
    p = con.create_server2client_packet()
    p[TCP].flags = "FA"
    p[TCP].seq = tcp_seq_wrap(server_isn + 1001)
    p[TCP].ack = tcp_seq_wrap(client_isn + 1002)
    add_tcp_authopt_signature(
        p, TcpAuthOptAlg_HMAC_SHA1(), key.key, server_isn, client_isn
    )
    pr = con.server_l2socket.sr1(p)
    assert pr[TCP].ack == tcp_seq_wrap(server_isn + 1001)
    assert pr[TCP].seq == tcp_seq_wrap(client_isn + 1001)
    assert pr[TCP].flags == "A"

    scapy_sniffer_stop(con.sniffer)

    val = TcpAuthValidator()
    val.keys.append(TcpAuthValidatorKey(key=b"hello", alg_name="HMAC-SHA-1-96"))
    for p in con.sniffer.results:
        val.handle_packet(p)
    val.raise_errors()

    # The server does not have enough state to validate the ACK from TIME-WAIT
    # so it reports a failure.
    assert con.server_nstat_json()["TcpExtTCPAuthOptFailure"] == 1
    assert con.client_nstat_json()["TcpExtTCPAuthOptFailure"] == 0


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_tw_rst(exit_stack: ExitStack, address_family):
    """Manually sent a signed invalid packet after FIN and check TWSK signs RST correctly

    Kernel has a custom code path for this
    """
    key = DEFAULT_TCP_AUTHOPT_KEY
    con = TCPConnectionFixture(
        address_family=address_family,
        tcp_authopt_key=key,
    )
    con.server_thread.keep_half_open = True
    exit_stack.enter_context(con)

    # connect, transfer data and close client nicely
    con.client_socket.connect((str(con.server_addr), con.server_port))
    check_socket_echo(con.client_socket)
    con.client_socket.close()

    # since server keeps connection open client goes to FIN-WAIT-2
    def check_socket_states():
        client_tcp_state_name = con.get_client_tcp_state()
        server_tcp_state_name = con.get_server_tcp_state()
        logger.info("%s %s", client_tcp_state_name, server_tcp_state_name)
        return (
            client_tcp_state_name == "FIN-WAIT-2"
            and server_tcp_state_name == "CLOSE-WAIT"
        )

    waiting.wait(check_socket_states)

    # sending a FIN-ACK with incorrect seq makes
    # tcp_timewait_state_process return a TCP_TW_RST
    client_isn, server_isn = con.sniffer_session.get_client_server_isn()
    p = con.create_server2client_packet()
    p[TCP].flags = "FA"
    p[TCP].seq = tcp_seq_wrap(server_isn + 1001 + 1)
    p[TCP].ack = tcp_seq_wrap(client_isn + 1002)
    add_tcp_authopt_signature(
        p, TcpAuthOptAlg_HMAC_SHA1(), key.key, server_isn, client_isn
    )
    con.server_l2socket.send(p)

    # remove delay by scapy trick?
    import time

    time.sleep(1)
    scapy_sniffer_stop(con.sniffer)

    # Check client socket moved from FIN-WAIT-2 to CLOSED
    assert con.get_client_tcp_state() is None

    # Check some RST was seen
    def is_tcp_rst(p):
        return TCP in p and p[TCP].flags.R

    assert any(is_tcp_rst(p) for p in con.sniffer.results)

    # Check everything was valid
    val = TcpAuthValidator()
    val.keys.append(TcpAuthValidatorKey(key=b"hello", alg_name="HMAC-SHA-1-96"))
    for p in con.sniffer.results:
        val.handle_packet(p)
    val.raise_errors()

    # Check no snmp failures
    con.assert_no_snmp_output_failures()


def test_rst_linger(exit_stack: ExitStack):
    """Test RST sent deliberately via SO_LINGER is valid"""
    con = TCPConnectionFixture(
        sniffer_kwargs=dict(count=8), tcp_authopt_key=DEFAULT_TCP_AUTHOPT_KEY
    )
    exit_stack.enter_context(con)

    con.client_socket.connect((str(con.server_addr), con.server_port))
    check_socket_echo(con.client_socket)
    socket_set_linger(con.client_socket, 1, 0)
    con.client_socket.close()

    con.sniffer.join(timeout=3)

    val = TcpAuthValidator()
    val.keys.append(TcpAuthValidatorKey(key=b"hello", alg_name="HMAC-SHA-1-96"))
    for p in con.sniffer.results:
        val.handle_packet(p)
    val.raise_errors()

    def is_tcp_rst(p):
        return TCP in p and p[TCP].flags.R

    assert any(is_tcp_rst(p) for p in con.sniffer.results)


@pytest.mark.parametrize(
    "address_family,mode",
    [
        (socket.AF_INET, "goodsign"),
        (socket.AF_INET, "fakesign"),
        (socket.AF_INET, "unsigned"),
        (socket.AF_INET6, "goodsign"),
        (socket.AF_INET6, "fakesign"),
        (socket.AF_INET6, "unsigned"),
    ],
)
def test_badack_to_synack(exit_stack, address_family, mode: str):
    """Test bad ack in reponse to server to syn/ack.

    This is handled by a minisocket in the TCP_SYN_RECV state on a separate code path
    """
    con = TCPConnectionFixture(address_family=address_family)
    if mode != "unsigned":
        con.tcp_authopt_key = tcp_authopt_key(
            alg=TCP_AUTHOPT_ALG.HMAC_SHA_1_96,
            key=b"hello",
        )
    exit_stack.enter_context(con)

    client_l2socket = con.client_l2socket
    client_isn = 1000
    server_isn = 0

    def sign(packet):
        if mode == "unsigned":
            return
        add_tcp_authopt_signature(
            packet,
            TcpAuthOptAlg_HMAC_SHA1(),
            con.tcp_authopt_key.key,
            client_isn,
            server_isn,
        )

    # Prevent TCP in client namespace from sending RST
    # Do this by removing the client address and insert a static ARP on server side
    client_prefix_length = con.nsfixture.get_prefix_length(address_family)
    subprocess.run(
        f"""\
set -e
ip netns exec {con.nsfixture.client_netns_name} ip addr del {con.client_addr}/{client_prefix_length} dev veth0
ip netns exec {con.nsfixture.server_netns_name} ip neigh add {con.client_addr} lladdr {con.nsfixture.client_mac_addr} dev veth0
""",
        shell=True,
        check=True,
    )

    p1 = con.create_client2server_packet()
    p1[TCP].flags = "S"
    p1[TCP].seq = client_isn
    p1[TCP].ack = 0
    sign(p1)

    p2 = client_l2socket.sr1(p1, timeout=1)
    server_isn = p2[TCP].seq
    assert p2[TCP].ack == client_isn + 1
    assert p2[TCP].flags == "SA"

    p3 = con.create_client2server_packet()
    p3[TCP].flags = "A"
    p3[TCP].seq = client_isn + 1
    p3[TCP].ack = server_isn + 1
    sign(p3)
    if mode == "fakesign":
        break_tcp_authopt_signature(p3)

    assert con.server_nstat_json()["TcpExtTCPAuthOptFailure"] == 0
    client_l2socket.send(p3)

    def confirm_good():
        return len(con.server_thread.server_socket) > 0

    def confirm_fail():
        return con.server_nstat_json()["TcpExtTCPAuthOptFailure"] == 1

    def wait_good():
        assert not confirm_fail()
        return confirm_good()

    def wait_fail():
        assert not confirm_good()
        return confirm_fail()

    if mode == "fakesign":
        waiting.wait(wait_fail, timeout_seconds=5, sleep_seconds=0.1)
    else:
        waiting.wait(wait_good, timeout_seconds=5, sleep_seconds=0.1)
