# SPDX-License-Identifier: GPL-2.0
import subprocess
import socket
from ipaddress import IPv4Address
from ipaddress import IPv6Address


class NamespaceFixture:
    """Create a pair of namespaces connected by one veth pair

    Each end of the pair has multiple addresses but everything is in the same subnet
    """

    ns1_name = "tcp_authopt_test_1"
    ns2_name = "tcp_authopt_test_2"

    @classmethod
    def get_ipv4_addr(cls, ns=1, index=1) -> IPv4Address:
        return IPv4Address("10.10.0.0") + (ns << 8) + index

    @classmethod
    def get_ipv6_addr(cls, ns=1, index=1) -> IPv6Address:
        return IPv6Address("fd00::") + (ns << 16) + index

    @classmethod
    def get_addr(cls, address_family=socket.AF_INET, ns=1, index=1):
        if address_family == socket.AF_INET:
            return cls.get_ipv4_addr(ns, index)
        elif address_family == socket.AF_INET6:
            return cls.get_ipv6_addr(ns, index)
        else:
            raise ValueError(f"Bad address_family={address_family}")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)

    def __enter__(self):
        script = f"""
set -e -x
ip netns del {self.ns1_name} || true
ip netns del {self.ns2_name} || true
ip netns add {self.ns1_name}
ip netns add {self.ns2_name}
ip link add veth0 netns {self.ns1_name} type veth peer name veth0 netns {self.ns2_name}
ip netns exec {self.ns1_name} ip link set veth0 up
ip netns exec {self.ns2_name} ip link set veth0 up
"""
        for index in [1, 2, 3]:
            script += f"ip -n {self.ns1_name} addr add {self.get_ipv4_addr(1, index)}/16 dev veth0\n"
            script += f"ip -n {self.ns2_name} addr add {self.get_ipv4_addr(2, index)}/16 dev veth0\n"
            script += f"ip -n {self.ns1_name} addr add {self.get_ipv6_addr(1, index)}/64 dev veth0 nodad\n"
            script += f"ip -n {self.ns2_name} addr add {self.get_ipv6_addr(2, index)}/64 dev veth0 nodad\n"
        subprocess.run(script, shell=True, check=True)
        return self

    def __exit__(self, *a):
        script = f"""
set -e -x
ip netns del {self.ns1_name} || true
ip netns del {self.ns2_name} || true
"""
        subprocess.run(script, shell=True, check=True)
