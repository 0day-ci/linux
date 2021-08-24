# SPDX-License-Identifier: GPL-2.0
"""Test TCP-AO keys can be bound to specific remote addresses"""
from contextlib import ExitStack
import socket
import pytest
from .netns_fixture import NamespaceFixture
from .utils import create_listen_socket
from .server import SimpleServerThread
from . import linux_tcp_authopt
from .linux_tcp_authopt import (
    tcp_authopt,
    set_tcp_authopt,
    set_tcp_authopt_key,
    tcp_authopt_key,
)
from .utils import netns_context, DEFAULT_TCP_SERVER_PORT, check_socket_echo
from .conftest import skipif_missing_tcp_authopt

pytestmark = skipif_missing_tcp_authopt


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_addr_server_bind(exit_stack: ExitStack, address_family):
    """ "Server only accept client2, check client1 fails"""
    nsfixture = exit_stack.enter_context(NamespaceFixture())
    server_addr = str(nsfixture.get_addr(address_family, 1, 1))
    client_addr = str(nsfixture.get_addr(address_family, 2, 1))
    client_addr2 = str(nsfixture.get_addr(address_family, 2, 2))

    # create server:
    listen_socket = exit_stack.push(
        create_listen_socket(family=address_family, ns=nsfixture.ns1_name)
    )
    exit_stack.enter_context(SimpleServerThread(listen_socket, mode="echo"))

    # set keys:
    server_key = tcp_authopt_key(
        alg=linux_tcp_authopt.TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
        key="hello",
        flags=linux_tcp_authopt.TCP_AUTHOPT_KEY_BIND_ADDR,
        addr=client_addr2,
    )
    set_tcp_authopt(
        listen_socket,
        tcp_authopt(flags=linux_tcp_authopt.TCP_AUTHOPT_FLAG_REJECT_UNEXPECTED),
    )
    set_tcp_authopt_key(listen_socket, server_key)

    # create client socket:
    def create_client_socket():
        with netns_context(nsfixture.ns2_name):
            client_socket = socket.socket(address_family, socket.SOCK_STREAM)
        client_key = tcp_authopt_key(
            alg=linux_tcp_authopt.TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
            key="hello",
        )
        set_tcp_authopt_key(client_socket, client_key)
        return client_socket

    # addr match:
    # with create_client_socket() as client_socket2:
    #    client_socket2.bind((client_addr2, 0))
    #    client_socket2.settimeout(1.0)
    #    client_socket2.connect((server_addr, TCP_SERVER_PORT))

    # addr mismatch:
    with create_client_socket() as client_socket1:
        client_socket1.bind((client_addr, 0))
        with pytest.raises(socket.timeout):
            client_socket1.settimeout(1.0)
            client_socket1.connect((server_addr, DEFAULT_TCP_SERVER_PORT))


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_addr_client_bind(exit_stack: ExitStack, address_family):
    """ "Client configures different keys with same id but different addresses"""
    nsfixture = exit_stack.enter_context(NamespaceFixture())
    server_addr1 = str(nsfixture.get_addr(address_family, 1, 1))
    server_addr2 = str(nsfixture.get_addr(address_family, 1, 2))
    client_addr = str(nsfixture.get_addr(address_family, 2, 1))

    # create servers:
    listen_socket1 = exit_stack.enter_context(
        create_listen_socket(
            family=address_family, ns=nsfixture.ns1_name, bind_addr=server_addr1
        )
    )
    listen_socket2 = exit_stack.enter_context(
        create_listen_socket(
            family=address_family, ns=nsfixture.ns1_name, bind_addr=server_addr2
        )
    )
    exit_stack.enter_context(SimpleServerThread(listen_socket1, mode="echo"))
    exit_stack.enter_context(SimpleServerThread(listen_socket2, mode="echo"))

    # set keys:
    set_tcp_authopt_key(
        listen_socket1,
        tcp_authopt_key(
            alg=linux_tcp_authopt.TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
            key="11111",
        ),
    )
    set_tcp_authopt_key(
        listen_socket2,
        tcp_authopt_key(
            alg=linux_tcp_authopt.TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
            key="22222",
        ),
    )

    # create client socket:
    def create_client_socket():
        with netns_context(nsfixture.ns2_name):
            client_socket = socket.socket(address_family, socket.SOCK_STREAM)
        set_tcp_authopt_key(
            client_socket,
            tcp_authopt_key(
                alg=linux_tcp_authopt.TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
                key="11111",
                flags=linux_tcp_authopt.TCP_AUTHOPT_KEY_BIND_ADDR,
                addr=server_addr1,
            ),
        )
        set_tcp_authopt_key(
            client_socket,
            tcp_authopt_key(
                alg=linux_tcp_authopt.TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
                key="22222",
                flags=linux_tcp_authopt.TCP_AUTHOPT_KEY_BIND_ADDR,
                addr=server_addr2,
            ),
        )
        client_socket.settimeout(1.0)
        client_socket.bind((client_addr, 0))
        return client_socket

    with create_client_socket() as client_socket1:
        client_socket1.connect((server_addr1, DEFAULT_TCP_SERVER_PORT))
        check_socket_echo(client_socket1)
    with create_client_socket() as client_socket2:
        client_socket2.connect((server_addr2, DEFAULT_TCP_SERVER_PORT))
        check_socket_echo(client_socket2)
