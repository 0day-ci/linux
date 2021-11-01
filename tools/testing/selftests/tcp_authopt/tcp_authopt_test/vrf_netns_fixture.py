# SPDX-License-Identifier: GPL-2.0
import subprocess
from ipaddress import IPv4Address, IPv6Address


def ip_link_get_ifindex(dev: str, prefix: str = "") -> int:
    out = subprocess.check_output(
        f"{prefix}ip -o link show {dev}", text=True, shell=True
    )
    return int(out.split(":", 1)[0])


def get_ipv4_addr(ns=1, index=1) -> IPv4Address:
    return IPv4Address("10.10.0.0") + (ns << 8) + index


def get_ipv6_addr(ns=1, index=1) -> IPv6Address:
    return IPv6Address("fd00::") + (ns << 16) + index


class VrfNamespaceFixture:
    """Namespace fixture for VRF testing.

    Single server has two interfaces with same IP addr: one inside VRF and one
    outside.

    Two clients two namespaces have same client IP, one connected to VRF and one
    outside.
    """

    tcp_l3mdev_accept = 1

    server_netns_name = "tcp_authopt_test_server"
    client0_netns_name = "tcp_authopt_test_client0"
    client1_netns_name = "tcp_authopt_test_client1"
    client2_netns_name = "tcp_authopt_test_client2"

    # 02:* means "locally administered"
    server_veth0_mac_addr = "02:00:00:01:00:00"
    server_veth1_mac_addr = "02:00:00:01:00:01"
    server_veth2_mac_addr = "02:00:00:01:00:02"
    client0_mac_addr = "02:00:00:02:00:00"
    client1_mac_addr = "02:00:00:02:01:00"
    client2_mac_addr = "02:00:00:02:02:00"

    server_ipv4_addr = get_ipv4_addr(1, 1)
    server_ipv6_addr = get_ipv6_addr(1, 1)
    client_ipv4_addr = get_ipv4_addr(2, 1)
    client_ipv6_addr = get_ipv6_addr(2, 1)

    def __init__(self, **kw):
        import os

        import pytest

        from .conftest import raise_skip_no_netns

        raise_skip_no_netns()
        if not os.path.exists("/proc/sys/net/ipv4/tcp_l3mdev_accept"):
            pytest.skip(
                "missing tcp_l3mdev_accept, is CONFIG_NET_L3_MASTER_DEV enabled?)"
            )
        for k, v in kw.items():
            setattr(self, k, v)

    def get_server_ifindex(self, dev):
        return ip_link_get_ifindex(dev, f"ip netns exec {self.server_netns_name} ")

    def __enter__(self):
        self._del_netns()
        script = f"""
set -e
ip netns add {self.server_netns_name}
ip netns add {self.client0_netns_name}
ip netns add {self.client1_netns_name}
ip netns add {self.client2_netns_name}
# Enable tcp_l3mdev unconditionally
ip netns exec {self.server_netns_name} sysctl -q net.ipv4.tcp_l3mdev_accept={int(self.tcp_l3mdev_accept)}
ip link add veth0 netns {self.server_netns_name} type veth peer name veth0 netns {self.client0_netns_name}
ip link add veth1 netns {self.server_netns_name} type veth peer name veth0 netns {self.client1_netns_name}
ip link add veth2 netns {self.server_netns_name} type veth peer name veth0 netns {self.client2_netns_name}
ip link add vrf1 netns {self.server_netns_name} type vrf table 1000
ip link add vrf2 netns {self.server_netns_name} type vrf table 2000
ip -n {self.server_netns_name} link set vrf1 up
ip -n {self.server_netns_name} link set vrf2 up
ip -n {self.server_netns_name} link set veth1 vrf vrf1
ip -n {self.server_netns_name} link set veth2 vrf vrf2
ip -n {self.server_netns_name} link set veth0 up addr {self.server_veth0_mac_addr}
ip -n {self.server_netns_name} link set veth1 up addr {self.server_veth1_mac_addr}
ip -n {self.server_netns_name} link set veth2 up addr {self.server_veth2_mac_addr}
ip -n {self.server_netns_name} addr add {self.server_ipv4_addr}/16 dev veth0
ip -n {self.server_netns_name} addr add {self.server_ipv6_addr}/64 dev veth0 nodad
ip -n {self.server_netns_name} addr add {self.server_ipv4_addr}/16 dev veth1
ip -n {self.server_netns_name} addr add {self.server_ipv6_addr}/64 dev veth1 nodad
ip -n {self.server_netns_name} addr add {self.server_ipv4_addr}/16 dev veth2
ip -n {self.server_netns_name} addr add {self.server_ipv6_addr}/64 dev veth2 nodad
ip -n {self.client0_netns_name} link set veth0 up addr {self.client0_mac_addr}
ip -n {self.client0_netns_name} addr add {self.client_ipv4_addr}/16 dev veth0
ip -n {self.client0_netns_name} addr add {self.client_ipv6_addr}/64 dev veth0 nodad
ip -n {self.client1_netns_name} link set veth0 up addr {self.client1_mac_addr}
ip -n {self.client1_netns_name} addr add {self.client_ipv4_addr}/16 dev veth0
ip -n {self.client1_netns_name} addr add {self.client_ipv6_addr}/64 dev veth0 nodad
ip -n {self.client2_netns_name} link set veth0 up addr {self.client2_mac_addr}
ip -n {self.client2_netns_name} addr add {self.client_ipv4_addr}/16 dev veth0
ip -n {self.client2_netns_name} addr add {self.client_ipv6_addr}/64 dev veth0 nodad
"""
        subprocess.run(script, shell=True, check=True)
        self.server_veth0_ifindex = self.get_server_ifindex("veth0")
        self.server_veth1_ifindex = self.get_server_ifindex("veth1")
        self.server_veth2_ifindex = self.get_server_ifindex("veth2")
        self.server_vrf1_ifindex = self.get_server_ifindex("vrf1")
        self.server_vrf2_ifindex = self.get_server_ifindex("vrf2")
        return self

    def _del_netns(self):
        script = f"""\
set -e
for ns in {self.server_netns_name} {self.client0_netns_name} {self.client1_netns_name} {self.client2_netns_name}; do
    if ip netns list | grep -q "$ns"; then
        ip netns del "$ns"
    fi
done
"""
        subprocess.run(script, shell=True, check=True)

    def __exit__(self, *a):
        self._del_netns()
