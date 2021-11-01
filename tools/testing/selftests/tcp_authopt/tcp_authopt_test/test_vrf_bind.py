# SPDX-License-Identifier: GPL-2.0
"""Test VRF overlap behavior

With tcp_l3mdev_accept single server should be able to differentiate multiple
clients with same IP coming from different VRFs.
"""
import errno
import logging
import socket
from contextlib import ExitStack

import pytest

from . import linux_tcp_md5sig
from .conftest import parametrize_product, skipif_missing_tcp_authopt
from .linux_tcp_authopt import (
    set_tcp_authopt_key,
    set_tcp_authopt_key_kwargs,
    tcp_authopt_key,
)
from .server import SimpleServerThread
from .utils import (
    DEFAULT_TCP_SERVER_PORT,
    check_socket_echo,
    create_client_socket,
    create_listen_socket,
)
from .vrf_netns_fixture import VrfNamespaceFixture

logger = logging.getLogger(__name__)


class VrfFixture:
    """Fixture for VRF testing

    Single server has two interfaces with same IP addr: one inside VRF and one
    outside. Two clients two namespaces have same client IP, one connected to
    VRF and one outside.
    """

    def __init__(
        self,
        address_family=socket.AF_INET,
        tcp_l3mdev_accept=1,
        init_default_listen_socket=True,
    ):
        self.address_family = address_family
        self.tcp_l3mdev_accept = tcp_l3mdev_accept
        self.init_default_listen_socket = init_default_listen_socket

    @property
    def server_addr(self):
        if self.address_family == socket.AF_INET:
            return self.nsfixture.server_ipv4_addr
        else:
            return self.nsfixture.server_ipv6_addr

    @property
    def client_addr(self):
        if self.address_family == socket.AF_INET:
            return self.nsfixture.client_ipv4_addr
        else:
            return self.nsfixture.client_ipv6_addr

    @property
    def server_addr_port(self):
        return (str(self.server_addr), DEFAULT_TCP_SERVER_PORT)

    @property
    def vrf1_ifindex(self):
        return self.nsfixture.server_vrf1_ifindex

    @property
    def vrf2_ifindex(self):
        return self.nsfixture.server_vrf2_ifindex

    def create_listen_socket(self, **kw):
        result = create_listen_socket(
            family=self.address_family,
            ns=self.nsfixture.server_netns_name,
            bind_addr=self.server_addr,
            **kw
        )
        self.exit_stack.enter_context(result)
        return result

    def create_client_socket(self, ns):
        result = create_client_socket(
            ns=ns, family=self.address_family, bind_addr=self.client_addr
        )
        self.exit_stack.enter_context(result)
        return result

    def __enter__(self):
        self.exit_stack = ExitStack()
        self.exit_stack.__enter__()
        self.nsfixture = self.exit_stack.enter_context(
            VrfNamespaceFixture(tcp_l3mdev_accept=self.tcp_l3mdev_accept)
        )

        self.server_thread = SimpleServerThread(mode="echo")
        if self.init_default_listen_socket:
            self.listen_socket = self.create_listen_socket()
            self.server_thread.add_listen_socket(self.listen_socket)
        self.exit_stack.enter_context(self.server_thread)
        return self

    def __exit__(self, *args):
        self.exit_stack.__exit__(*args)


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_vrf_overlap_unsigned(exit_stack: ExitStack, address_family):
    """Test without any signature support"""
    fix = VrfFixture(address_family)
    exit_stack.enter_context(fix)

    client_socket0 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    client_socket2 = fix.create_client_socket(fix.nsfixture.client2_netns_name)

    client_socket2.connect(fix.server_addr_port)
    client_socket1.connect(fix.server_addr_port)
    client_socket0.connect(fix.server_addr_port)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket2)
    check_socket_echo(client_socket0)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket2)
    check_socket_echo(client_socket0)
    check_socket_echo(client_socket2)


KEY0 = b"00000"
KEY1 = b"1"
KEY2 = b"22"


def set_server_md5(fix, key=KEY0, **kw):
    linux_tcp_md5sig.setsockopt_md5sig_kwargs(
        fix.listen_socket, key=key, addr=fix.client_addr, **kw
    )


def set_server_md5_key0(fix, key=KEY0):
    return set_server_md5(fix, key=key)


def set_server_md5_key1(fix, key=KEY1):
    return set_server_md5(fix, key=key, ifindex=fix.vrf1_ifindex)


def set_server_md5_key2(fix, key=KEY2):
    return set_server_md5(fix, key=key, ifindex=fix.vrf2_ifindex)


def set_client_md5_key(fix, client_socket, key):
    linux_tcp_md5sig.setsockopt_md5sig_kwargs(
        client_socket, key=key, addr=fix.server_addr
    )


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_vrf_overlap_md5_samekey(exit_stack: ExitStack, address_family):
    """Test overlapping keys that are identical"""
    fix = VrfFixture(address_family)
    exit_stack.enter_context(fix)
    set_server_md5_key0(fix, b"same")
    set_server_md5_key1(fix, b"same")
    set_server_md5_key2(fix, b"same")
    client_socket0 = fix.create_client_socket(fix.nsfixture.client0_netns_name)
    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    client_socket2 = fix.create_client_socket(fix.nsfixture.client2_netns_name)
    set_client_md5_key(fix, client_socket0, b"same")
    set_client_md5_key(fix, client_socket1, b"same")
    set_client_md5_key(fix, client_socket2, b"same")
    client_socket0.connect(fix.server_addr_port)
    client_socket1.connect(fix.server_addr_port)
    client_socket2.connect(fix.server_addr_port)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket2)
    check_socket_echo(client_socket0)


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_vrf_overlap12_md5(exit_stack: ExitStack, address_family):
    """Test overlapping keys between vrfs"""
    fix = VrfFixture(address_family)
    exit_stack.enter_context(fix)
    set_server_md5_key1(fix)
    set_server_md5_key2(fix)
    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    client_socket2 = fix.create_client_socket(fix.nsfixture.client2_netns_name)
    set_client_md5_key(fix, client_socket1, KEY1)
    set_client_md5_key(fix, client_socket2, KEY2)
    client_socket1.connect(fix.server_addr_port)
    client_socket2.connect(fix.server_addr_port)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket2)


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_vrf_overlap01_md5(exit_stack: ExitStack, address_family):
    """Test overlapping keys inside and outside vrf, VRF key added second"""
    fix = VrfFixture(address_family)
    exit_stack.enter_context(fix)
    set_server_md5_key0(fix)
    set_server_md5_key1(fix)
    client_socket0 = fix.create_client_socket(fix.nsfixture.client0_netns_name)
    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    set_client_md5_key(fix, client_socket0, KEY0)
    set_client_md5_key(fix, client_socket1, KEY1)
    client_socket1.connect(fix.server_addr_port)
    client_socket0.connect(fix.server_addr_port)
    check_socket_echo(client_socket0)
    check_socket_echo(client_socket1)


@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_vrf_overlap10_md5(exit_stack: ExitStack, address_family):
    """Test overlapping keys inside and outside vrf, VRF key added first"""
    fix = VrfFixture(address_family)
    exit_stack.enter_context(fix)
    set_server_md5_key1(fix)
    set_server_md5_key0(fix)
    client_socket0 = fix.create_client_socket(fix.nsfixture.client0_netns_name)
    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    set_client_md5_key(fix, client_socket0, KEY0)
    set_client_md5_key(fix, client_socket1, KEY1)
    client_socket1.connect(fix.server_addr_port)
    client_socket0.connect(fix.server_addr_port)
    check_socket_echo(client_socket0)
    check_socket_echo(client_socket1)


@pytest.mark.parametrize("address_family", [socket.AF_INET])
def test_vrf_overlap_md5_prefix(exit_stack: ExitStack, address_family):
    """VRF keys should take precedence even if prefixlen is low"""
    fix = VrfFixture(address_family)
    exit_stack.enter_context(fix)
    set_server_md5(fix, key=b"fail", prefixlen=16)
    set_server_md5(
        fix, key=b"pass", ifindex=fix.nsfixture.server_vrf1_ifindex, prefixlen=1
    )
    set_server_md5(fix, key=b"fail", prefixlen=24)

    # connect via VRF
    client_socket = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    set_client_md5_key(fix, client_socket, b"pass")
    client_socket.connect(fix.server_addr_port)


class TestVRFOverlapAOBoundKeyPrecedence:
    """Keys bound to VRF should take precedence over unbound keys.

    KEY0 is unbound (accepts all vrfs)
    KEY1 is bound to vrf1
    """

    fix: VrfFixture

    @pytest.fixture(
        autouse=True,
        scope="class",
        params=[socket.AF_INET, socket.AF_INET6],
    )
    def init(self, request: pytest.FixtureRequest):
        address_family = request.param
        logger.info("init address_family=%s", address_family)
        with ExitStack() as exit_stack:
            fix = exit_stack.enter_context(VrfFixture(address_family))
            set_tcp_authopt_key_kwargs(
                fix.listen_socket,
                key=KEY0,
                ifindex=None,
            )
            set_tcp_authopt_key_kwargs(
                fix.listen_socket,
                key=KEY1,
                ifindex=fix.vrf1_ifindex,
            )
            self.__class__.fix = fix
            yield
        logger.info("done address_family=%s", address_family)

    def test_vrf1_key0(self):
        client_socket = self.fix.create_client_socket(
            self.fix.nsfixture.client1_netns_name
        )
        set_tcp_authopt_key_kwargs(client_socket, key=KEY0)
        with pytest.raises(socket.timeout):
            client_socket.connect(self.fix.server_addr_port)

    def test_vrf1_key1(self):
        client_socket = self.fix.create_client_socket(
            self.fix.nsfixture.client1_netns_name
        )
        set_tcp_authopt_key_kwargs(client_socket, key=KEY1)
        client_socket.connect(self.fix.server_addr_port)

    def test_vrf2_key0(self):
        client_socket = self.fix.create_client_socket(
            self.fix.nsfixture.client2_netns_name
        )
        set_tcp_authopt_key_kwargs(client_socket, key=KEY0)
        client_socket.connect(self.fix.server_addr_port)

    def test_vrf2_key1(self):
        client_socket = self.fix.create_client_socket(
            self.fix.nsfixture.client2_netns_name
        )
        set_tcp_authopt_key_kwargs(client_socket, key=KEY1)
        with pytest.raises(socket.timeout):
            client_socket.connect(self.fix.server_addr_port)


def assert_raises_enoent(func):
    with pytest.raises(OSError) as e:
        func()
    assert e.value.errno == errno.ENOENT


def test_vrf_overlap_md5_del_0110():
    """Removing keys should not raise ENOENT because they are distinct"""
    with VrfFixture() as fix:
        set_server_md5(fix, key=KEY0)
        set_server_md5(fix, key=KEY1, ifindex=fix.vrf1_ifindex)
        set_server_md5(fix, key=b"", ifindex=fix.vrf1_ifindex)
        set_server_md5(fix, key=b"")
        assert_raises_enoent(lambda: set_server_md5(fix, key=b""))


def test_vrf_overlap_md5_del_1001():
    """Removing keys should not raise ENOENT because they are distinct"""
    with VrfFixture() as fix:
        set_server_md5(fix, key=KEY1, ifindex=fix.vrf1_ifindex)
        set_server_md5(fix, key=KEY0)
        set_server_md5(fix, key=b"")
        set_server_md5(fix, key=b"", ifindex=fix.vrf1_ifindex)
        assert_raises_enoent(lambda: set_server_md5(fix, key=b""))


def test_vrf_overlap_md5_del_1010():
    """Removing keys should not raise ENOENT because they are distinct"""
    with VrfFixture() as fix:
        set_server_md5(fix, key=KEY1, ifindex=fix.vrf1_ifindex)
        set_server_md5(fix, key=KEY0)
        set_server_md5(fix, key=b"", ifindex=fix.vrf1_ifindex)
        set_server_md5(fix, key=b"")
        assert_raises_enoent(lambda: set_server_md5(fix, key=b""))


@skipif_missing_tcp_authopt
@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_vrf_overlap_ao_samekey(exit_stack: ExitStack, address_family):
    """Single server serving both VRF and non-VRF client with same password.

    This requires no special support from TCP-AO.
    """
    fix = VrfFixture(address_family)
    exit_stack.enter_context(fix)
    set_tcp_authopt_key(fix.listen_socket, tcp_authopt_key(key="11111"))

    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    client_socket2 = fix.create_client_socket(fix.nsfixture.client2_netns_name)

    set_tcp_authopt_key(client_socket1, tcp_authopt_key(key="11111"))
    set_tcp_authopt_key(client_socket2, tcp_authopt_key(key="11111"))
    client_socket1.connect(fix.server_addr_port)
    client_socket2.connect(fix.server_addr_port)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket2)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket2)


@skipif_missing_tcp_authopt
@pytest.mark.parametrize("address_family", [socket.AF_INET, socket.AF_INET6])
def test_vrf_overlap_ao(exit_stack: ExitStack, address_family):
    """Single server serving both VRF and non-VRF client with different passwords

    This requires kernel to handle ifindex
    """
    fix = VrfFixture(address_family)
    exit_stack.enter_context(fix)
    set_tcp_authopt_key(
        fix.listen_socket,
        tcp_authopt_key(key=KEY0, ifindex=0),
    )
    set_tcp_authopt_key(
        fix.listen_socket,
        tcp_authopt_key(key=KEY1, ifindex=fix.vrf1_ifindex),
    )
    set_tcp_authopt_key(
        fix.listen_socket,
        tcp_authopt_key(key=KEY2, ifindex=fix.vrf2_ifindex),
    )

    client_socket0 = fix.create_client_socket(fix.nsfixture.client0_netns_name)
    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    client_socket2 = fix.create_client_socket(fix.nsfixture.client2_netns_name)
    set_tcp_authopt_key(client_socket0, tcp_authopt_key(key=KEY0))
    set_tcp_authopt_key(client_socket1, tcp_authopt_key(key=KEY1))
    set_tcp_authopt_key(client_socket2, tcp_authopt_key(key=KEY2))
    client_socket0.connect(fix.server_addr_port)
    client_socket1.connect(fix.server_addr_port)
    client_socket2.connect(fix.server_addr_port)
    check_socket_echo(client_socket0)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket2)
    check_socket_echo(client_socket0)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket2)


@parametrize_product(
    address_family=(socket.AF_INET, socket.AF_INET6),
    tcp_l3mdev_accept=(0, 1),
    bind_key_to_vrf=(0, 1),
)
def test_md5_pervrf(
    exit_stack: ExitStack, address_family, tcp_l3mdev_accept, bind_key_to_vrf
):
    """Test one VRF-bound socket.

    Since the socket is already bound to the vrf binding the key should not be required.
    """
    fix = VrfFixture(
        address_family,
        tcp_l3mdev_accept=tcp_l3mdev_accept,
        init_default_listen_socket=False,
    )
    exit_stack.enter_context(fix)
    listen_socket1 = fix.create_listen_socket(bind_device="veth1")
    linux_tcp_md5sig.setsockopt_md5sig_kwargs(
        listen_socket1,
        key=KEY1,
        addr=fix.client_addr,
        ifindex=fix.vrf1_ifindex if bind_key_to_vrf else None,
    )
    fix.server_thread.add_listen_socket(listen_socket1)
    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    set_client_md5_key(fix, client_socket1, KEY1)
    client_socket1.connect(fix.server_addr_port)
    check_socket_echo(client_socket1)


@pytest.mark.parametrize(
    "address_family",
    (socket.AF_INET, socket.AF_INET6),
)
def test_vrf_overlap_md5_pervrf(exit_stack: ExitStack, address_family):
    """Test overlapping via per-VRF sockets"""
    fix = VrfFixture(
        address_family,
        tcp_l3mdev_accept=0,
        init_default_listen_socket=False,
    )
    exit_stack.enter_context(fix)
    listen_socket0 = fix.create_listen_socket()
    listen_socket1 = fix.create_listen_socket(bind_device="veth1")
    listen_socket2 = fix.create_listen_socket(bind_device="veth2")
    linux_tcp_md5sig.setsockopt_md5sig_kwargs(
        listen_socket0,
        key=KEY0,
        addr=fix.client_addr,
    )
    linux_tcp_md5sig.setsockopt_md5sig_kwargs(
        listen_socket1,
        key=KEY1,
        addr=fix.client_addr,
    )
    linux_tcp_md5sig.setsockopt_md5sig_kwargs(
        listen_socket2,
        key=KEY2,
        addr=fix.client_addr,
    )
    fix.server_thread.add_listen_socket(listen_socket0)
    fix.server_thread.add_listen_socket(listen_socket1)
    fix.server_thread.add_listen_socket(listen_socket2)
    client_socket0 = fix.create_client_socket(fix.nsfixture.client0_netns_name)
    client_socket1 = fix.create_client_socket(fix.nsfixture.client1_netns_name)
    client_socket2 = fix.create_client_socket(fix.nsfixture.client2_netns_name)
    set_client_md5_key(fix, client_socket0, KEY0)
    set_client_md5_key(fix, client_socket1, KEY1)
    set_client_md5_key(fix, client_socket2, KEY2)
    client_socket0.connect(fix.server_addr_port)
    client_socket1.connect(fix.server_addr_port)
    client_socket2.connect(fix.server_addr_port)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket1)
    check_socket_echo(client_socket0)
