# SPDX-License-Identifier: GPL-2.0
import subprocess
import socket
from ipaddress import IPv4Address
from ipaddress import IPv6Address


class NamespaceFixture:
    """Create a pair of namespaces connected by one veth pair

    Each end of the pair has multiple addresses but everything is in the same subnet
    """

    server_netns_name = "tcp_authopt_test_server"
    client_netns_name = "tcp_authopt_test_client"

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

    # 02:* means "locally administered"
    server_mac_addr = "02:00:00:00:00:01"
    client_mac_addr = "02:00:00:00:00:02"

    ipv4_prefix_len = 16
    ipv6_prefix_len = 64

    @classmethod
    def get_prefix_length(cls, address_family) -> int:
        return {
            socket.AF_INET: cls.ipv4_prefix_len,
            socket.AF_INET6: cls.ipv6_prefix_len,
        }[address_family]

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)

    def __enter__(self):
        self._del_netns()
        script = f"""
set -e
ip netns add {self.server_netns_name}
ip netns add {self.client_netns_name}
ip link add veth0 netns {self.server_netns_name} type veth peer name veth0 netns {self.client_netns_name}
ip netns exec {self.server_netns_name} ip link set veth0 up addr {self.server_mac_addr}
ip netns exec {self.client_netns_name} ip link set veth0 up addr {self.client_mac_addr}
"""
        for index in [1, 2, 3]:
            script += f"ip -n {self.server_netns_name} addr add {self.get_ipv4_addr(1, index)}/16 dev veth0\n"
            script += f"ip -n {self.client_netns_name} addr add {self.get_ipv4_addr(2, index)}/16 dev veth0\n"
            script += f"ip -n {self.server_netns_name} addr add {self.get_ipv6_addr(1, index)}/64 dev veth0 nodad\n"
            script += f"ip -n {self.client_netns_name} addr add {self.get_ipv6_addr(2, index)}/64 dev veth0 nodad\n"
        subprocess.run(script, shell=True, check=True)
        return self

    def _del_netns(self):
        script = f"""\
set -e
if ip netns list | grep -q {self.server_netns_name}; then
    ip netns del {self.server_netns_name}
fi
if ip netns list | grep -q {self.client_netns_name}; then
    ip netns del {self.client_netns_name}
fi
"""
        subprocess.run(script, shell=True, check=True)

    def __exit__(self, *a):
        self._del_netns()
