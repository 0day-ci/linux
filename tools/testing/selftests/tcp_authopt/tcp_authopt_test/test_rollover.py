# SPDX-License-Identifier: GPL-2.0
import typing
import socket
from .server import SimpleServerThread
from .linux_tcp_authopt import (
    TCP_AUTHOPT_FLAG,
    set_tcp_authopt_key,
    tcp_authopt,
    tcp_authopt_key,
    set_tcp_authopt,
    get_tcp_authopt,
)
from .utils import DEFAULT_TCP_SERVER_PORT, create_listen_socket, check_socket_echo
from contextlib import ExitStack, contextmanager
from .conftest import skipif_missing_tcp_authopt

pytestmark = skipif_missing_tcp_authopt


@contextmanager
def make_tcp_authopt_socket_pair(
    server_addr="127.0.0.1",
    server_authopt: tcp_authopt = None,
    server_key_list: typing.Iterable[tcp_authopt_key] = [],
    client_authopt: tcp_authopt = None,
    client_key_list: typing.Iterable[tcp_authopt_key] = [],
) -> typing.Tuple[socket.socket, socket.socket]:
    """Make a pair for connected sockets for key switching tests

    Server runs in a background thread implementing echo protocol"""
    with ExitStack() as exit_stack:
        listen_socket = exit_stack.enter_context(
            create_listen_socket(bind_addr=server_addr)
        )
        server_thread = exit_stack.enter_context(
            SimpleServerThread(listen_socket, mode="echo")
        )
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.settimeout(1.0)

        if server_authopt:
            set_tcp_authopt(listen_socket, server_authopt)
        for k in server_key_list:
            set_tcp_authopt_key(listen_socket, k)
        if client_authopt:
            set_tcp_authopt(client_socket, client_authopt)
        for k in client_key_list:
            set_tcp_authopt_key(client_socket, k)

        client_socket.connect((server_addr, DEFAULT_TCP_SERVER_PORT))
        check_socket_echo(client_socket)
        server_socket = server_thread.server_socket[0]

        yield client_socket, server_socket


def test_get_keyids(exit_stack: ExitStack):
    """Check reading key ids"""
    sk1 = tcp_authopt_key(send_id=11, recv_id=12, key="111")
    sk2 = tcp_authopt_key(send_id=21, recv_id=22, key="222")
    ck1 = tcp_authopt_key(send_id=12, recv_id=11, key="111")
    client_socket, server_socket = exit_stack.enter_context(
        make_tcp_authopt_socket_pair(
            server_key_list=[sk1, sk2],
            client_key_list=[ck1],
        )
    )

    check_socket_echo(client_socket)
    client_tcp_authopt = get_tcp_authopt(client_socket)
    server_tcp_authopt = get_tcp_authopt(server_socket)
    assert server_tcp_authopt.send_keyid == 11
    assert server_tcp_authopt.send_rnextkeyid == 12
    assert server_tcp_authopt.recv_keyid == 12
    assert server_tcp_authopt.recv_rnextkeyid == 11
    assert client_tcp_authopt.send_keyid == 12
    assert client_tcp_authopt.send_rnextkeyid == 11
    assert client_tcp_authopt.recv_keyid == 11
    assert client_tcp_authopt.recv_rnextkeyid == 12


def test_rollover_send_keyid(exit_stack: ExitStack):
    """Check reading key ids"""
    sk1 = tcp_authopt_key(send_id=11, recv_id=12, key="111")
    sk2 = tcp_authopt_key(send_id=21, recv_id=22, key="222")
    ck1 = tcp_authopt_key(send_id=12, recv_id=11, key="111")
    ck2 = tcp_authopt_key(send_id=22, recv_id=21, key="222")
    client_socket, server_socket = exit_stack.enter_context(
        make_tcp_authopt_socket_pair(
            server_key_list=[sk1, sk2],
            client_key_list=[ck1, ck2],
            client_authopt=tcp_authopt(
                send_keyid=12, flags=TCP_AUTHOPT_FLAG.LOCK_KEYID
            ),
        )
    )

    check_socket_echo(client_socket)
    assert get_tcp_authopt(client_socket).recv_keyid == 11
    assert get_tcp_authopt(server_socket).recv_keyid == 12

    # Explicit request for key2
    set_tcp_authopt(
        client_socket, tcp_authopt(send_keyid=22, flags=TCP_AUTHOPT_FLAG.LOCK_KEYID)
    )
    check_socket_echo(client_socket)
    assert get_tcp_authopt(client_socket).recv_keyid == 21
    assert get_tcp_authopt(server_socket).recv_keyid == 22


def test_rollover_rnextkeyid(exit_stack: ExitStack):
    """Check reading key ids"""
    sk1 = tcp_authopt_key(send_id=11, recv_id=12, key="111")
    sk2 = tcp_authopt_key(send_id=21, recv_id=22, key="222")
    ck1 = tcp_authopt_key(send_id=12, recv_id=11, key="111")
    ck2 = tcp_authopt_key(send_id=22, recv_id=21, key="222")
    client_socket, server_socket = exit_stack.enter_context(
        make_tcp_authopt_socket_pair(
            server_key_list=[sk1],
            client_key_list=[ck1, ck2],
            client_authopt=tcp_authopt(
                send_keyid=12, flags=TCP_AUTHOPT_FLAG.LOCK_KEYID
            ),
        )
    )

    check_socket_echo(client_socket)
    assert get_tcp_authopt(server_socket).recv_rnextkeyid == 11

    # request rnextkeyd=22 but server does not have it
    set_tcp_authopt(
        client_socket,
        tcp_authopt(send_rnextkeyid=21, flags=TCP_AUTHOPT_FLAG.LOCK_RNEXTKEYID),
    )
    check_socket_echo(client_socket)
    check_socket_echo(client_socket)
    assert get_tcp_authopt(server_socket).recv_rnextkeyid == 21
    assert get_tcp_authopt(server_socket).send_keyid == 11

    # after adding k2 on server the key is switched
    set_tcp_authopt_key(server_socket, sk2)
    check_socket_echo(client_socket)
    check_socket_echo(client_socket)
    assert get_tcp_authopt(server_socket).send_keyid == 21


def test_rollover_delkey(exit_stack: ExitStack):
    sk1 = tcp_authopt_key(send_id=11, recv_id=12, key="111")
    sk2 = tcp_authopt_key(send_id=21, recv_id=22, key="222")
    ck1 = tcp_authopt_key(send_id=12, recv_id=11, key="111")
    ck2 = tcp_authopt_key(send_id=22, recv_id=21, key="222")
    client_socket, server_socket = exit_stack.enter_context(
        make_tcp_authopt_socket_pair(
            server_key_list=[sk1, sk2],
            client_key_list=[ck1, ck2],
            client_authopt=tcp_authopt(
                send_keyid=12, flags=TCP_AUTHOPT_FLAG.LOCK_KEYID
            ),
        )
    )

    check_socket_echo(client_socket)
    assert get_tcp_authopt(server_socket).recv_keyid == 12

    # invalid send_keyid is just ignored
    set_tcp_authopt(client_socket, tcp_authopt(send_keyid=7))
    check_socket_echo(client_socket)
    assert get_tcp_authopt(client_socket).send_keyid == 12
    assert get_tcp_authopt(server_socket).recv_keyid == 12
    assert get_tcp_authopt(client_socket).recv_keyid == 11

    # If a key is removed it is replaced by anything that matches
    ck1.delete_flag = True
    set_tcp_authopt_key(client_socket, ck1)
    check_socket_echo(client_socket)
    check_socket_echo(client_socket)
    assert get_tcp_authopt(client_socket).send_keyid == 22
    assert get_tcp_authopt(server_socket).send_keyid == 21
    assert get_tcp_authopt(server_socket).recv_keyid == 22
    assert get_tcp_authopt(client_socket).recv_keyid == 21
