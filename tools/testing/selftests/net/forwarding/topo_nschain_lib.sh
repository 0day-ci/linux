#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# A chain of 4 nodes connected with veth pairs.
# Each node lives in its own network namespace.
# Each veth interface has an IPv4 and an IPv6 address. A host route provides
# connectivity to the adjacent node. This base network only allows nodes to
# communicate with their immediate neighbours.
#
# The two nodes at the extremities of the chain also have 4 host IPs on their
# loopback device:
#   * An IPv4 address, routed as is to the adjacent router.
#   * An IPv4 address, routed over MPLS to the adjacent router.
#   * An IPv6 address, routed as is to the adjacent router.
#   * An IPv6 address, routed over MPLS to the adjacent router.
#
# This topology doesn't define how RTA and RTB handle these packets: users of
# this script are responsible for the plumbing between RTA and RTB.
#
# As each veth connects two different namespaces, their MAC and IP addresses
# are defined depending on the local and remote namespaces. For example
# veth-h1-rta, which sits in H1 and links to RTA, has MAC address
# 00:00:5e:00:53:1a, IPv4 192.0.2.0x1a and IPv6 2001:db8::1a, where "1a" means
# that it's in H1 and links to RTA (the rest of each address is always built
# from a IANA documentation prefix).
#
# Routed addresses in H1 and H2 on the other hand encode the routing type (with
# or without MPLS encapsulation) and the namespace the address resides in. For
# example H2 has 198.51.100.2 and 2001:db8::1:2, that are routed as is through
# RTB. It also has 198.51.100.0x12 and 2001:db8::1:12, that are routed through
# RTB with MPLS encapsulation.
#
# For clarity, the prefixes used for host IPs are different from the ones used
# on the veths.
#
# The MPLS labels follow a similar principle: the first digit represents the
# IP version of the encapsulated packet ("4" for IPv4, "6" for IPv6), the
# second digit represents the destination host ("1" for H1, "2" for H2).
#
# +----------------------------------------------------+
# |                    Host 1 (H1)                     |
# |                                                    |
# |   lo                                               |
# |     198.51.100.1    (for plain IPv4)               |
# |     2001:db8::1:1   (for plain IPv6)               |
# |     198.51.100.0x11 (for IPv4 over MPLS, label 42) |
# |     2001:db8::1:11  (for IPv6 over MPLS, label 62) |
# |                                                    |
# | + veth-h1-rta                                      |
# | |   192.0.2.0x1a                                   |
# | |   2001:db8::1a                                   |
# +-|--------------------------------------------------+
#   |
# +-|--------------------+
# | |  Router A (RTA)    |
# | |                    |
# | + veth-rta-h1        |
# |     192.0.2.0xa1     |
# |     2001:db8::a1     |
# |                      |
# | + veth-rta-rtb       |
# | |   192.0.2.0xab     |
# | |   2001:db8::ab     |
# +-|--------------------+
#   |
# +-|--------------------+
# | |  Router B (RTB)    |
# | |                    |
# | + veth-rtb-rta       |
# |     192.0.2.0xba     |
# |     2001:db8::ba     |
# |                      |
# | + veth-rtb-h2        |
# | |   192.0.2.0xb2     |
# | |   2001:db8::b2     |
# +-|--------------------+
#   |
# +-|--------------------------------------------------+
# | |                  Host 2 (H2)                     |
# | |                                                  |
# | + veth-h2-rtb                                      |
# |     192.0.2.0x2b                                   |
# |     2001:db8::2b                                   |
# |                                                    |
# |   lo                                               |
# |     198.51.100.2    (for plain IPv4)               |
# |     2001:db8::1:2   (for plain IPv6)               |
# |     198.51.100.0x12 (for IPv4 over MPLS, label 41) |
# |     2001:db8::1:12  (for IPv6 over MPLS, label 61) |
# +----------------------------------------------------+
#
# This topology can be used for testing different routing or switching
# scenarios, as H1 and H2 are pre-configured for sending different kinds of
# packets (IPv4, IPv6, with or without MPLS encapsulation), which RTA and RTB
# can easily match and process according to the forwarding mechanism to test.

readonly H1=$(mktemp -u h1-XXXXXXXX)
readonly RTA=$(mktemp -u rta-XXXXXXXX)
readonly RTB=$(mktemp -u rtb-XXXXXXXX)
readonly H2=$(mktemp -u h2-XXXXXXXX)

# Create and configure a veth pair between two network namespaces A and B
#
# Parameters:
#
#   * $1: Name of netns A.
#   * $2: Name of netns B.
#   * $3: Name of the veth device to create in netns A.
#   * $4: Name of the veth device to create in netns B.
#   * $5: Identifier used to configure IP and MAC addresses in netns A.
#   * $6: Identifier used to configure IP and MAC addresses in netns B.
#
# The identifiers are a one byte long integer given in hexadecimal format
# (without a "0x" prefix). They're used as the lowest order byte for the MAC,
# IPv4 and IPv6 addresses.
#
nsc_veth_config()
{
	local NS_A="${1}"; readonly NS_A
	local NS_B="${2}"; readonly NS_B
	local DEV_A="${3}"; readonly DEV_A
	local DEV_B="${4}"; readonly DEV_B
	local ID_A="${5}"; readonly ID_A
	local ID_B="${6}"; readonly ID_B

	ip link add name "${DEV_A}" address 00:00:5e:00:53:"${ID_A}"	\
		netns "${NS_A}" type veth peer name "${DEV_B}"		\
		address 00:00:5e:00:53:"${ID_B}" netns "${NS_B}"
	ip -netns "${NS_A}" link set dev "${DEV_A}" up
	ip -netns "${NS_B}" link set dev "${DEV_B}" up

	ip -netns "${NS_A}" address add dev "${DEV_A}"		\
		192.0.2.0x"${ID_A}" peer 192.0.2.0x"${ID_B}"/32
	ip -netns "${NS_B}" address add dev "${DEV_B}"		\
		192.0.2.0x"${ID_B}" peer 192.0.2.0x"${ID_A}"/32

	ip -netns "${NS_A}" address add dev "${DEV_A}"			\
		2001:db8::"${ID_A}" peer 2001:db8::"${ID_B}" nodad
	ip -netns "${NS_B}" address add dev "${DEV_B}"			\
		2001:db8::"${ID_B}" peer 2001:db8::"${ID_A}" nodad
}

# Add host IP addresses to the loopback device and route them to the adjacent
# router.
#
# Parameters:
#
#   $1: Name of the netns to configure.
#   $2: Identifier used to configure the local IP address.
#   $3: Identifier used to configure the remote IP address.
#   $4: IPv4 address of the adjacent router.
#   $5: IPv6 address of the adjacent router.
#   $6: Name of the network interface that links to the adjacent router.
#
# The identifiers are a one byte long integer given in hexadecimal format
# (without a "0x" prefix). They're used as the lowest order byte for the IPv4
# and IPv6 addresses.
#
nsc_lo_config()
{
	local NS="${1}"; readonly NS
	local LOCAL_NSID="${2}"; readonly LOCAL_NSID
	local PEER_NSID="${3}"; readonly PEER_NSID
	local GW_IP4="${4}"; readonly GW_IP4
	local GW_IP6="${5}"; readonly GW_IP6
	local IFACE="${6}"; readonly IFACE

	# For testing plain IPv4 traffic
	ip -netns "${NS}" address add 198.51.100.0x"${LOCAL_NSID}"/32 dev lo
	ip -netns "${NS}" route add 198.51.100.0x"${PEER_NSID}"/32	\
		src 198.51.100.0x"${LOCAL_NSID}" via "${GW_IP4}"

	# For testing plain IPv6 traffic
	ip -netns "${NS}" address add 2001:db8::1:"${LOCAL_NSID}"/128 dev lo
	ip -netns "${NS}" route add 2001:db8::1:"${PEER_NSID}"/128	\
		src 2001:db8::1:"${LOCAL_NSID}" via "${GW_IP6}"

	# For testing IPv4 over MPLS traffic
	ip -netns "${NS}" address add 198.51.100.0x1"${LOCAL_NSID}"/32 dev lo
	ip -netns "${NS}" route add 198.51.100.0x1"${PEER_NSID}"/32	\
		src 198.51.100.0x1"${LOCAL_NSID}"			\
		encap mpls 4"${PEER_NSID}" via "${GW_IP4}"

	# For testing IPv6 over MPLS traffic
	ip -netns "${NS}" address add 2001:db8::1:1"${LOCAL_NSID}"/128 dev lo
	ip -netns "${NS}" route add 2001:db8::1:1"${PEER_NSID}"/128	\
		src 2001:db8::1:1"${LOCAL_NSID}"			\
		encap mpls 6"${PEER_NSID}" via "${GW_IP6}"

	# Allow MPLS traffic
	ip netns exec "${NS}" sysctl -qw net.mpls.platform_labels=100
	ip netns exec "${NS}" sysctl -qw net.mpls.conf."${IFACE}".input=1

	# Deliver MPLS packets locally
	ip -netns "${NS}" -family mpls route add 4"${LOCAL_NSID}" dev lo
	ip -netns "${NS}" -family mpls route add 6"${LOCAL_NSID}" dev lo
}

# Remove the network namespaces
#
# Parameters:
#
#   * The list of network namespaces to delete.
#
nsc_cleanup_ns()
{
	for ns in "$@"; do
		ip netns delete "${ns}" 2>/dev/null || true
	done
}

# Remove the network namespaces and return error
#
# Parameters:
#
#   * The list of network namespaces to delete.
#
nsc_err_cleanup_ns()
{
	nsc_cleanup_ns "$@"
	return 1
}

# Create the four network namespaces (H1, RTA, RTB and H2)
#
nsc_setup_ns()
{
	ip netns add "${H1}" || nsc_err_cleanup_ns
	ip netns add "${RTA}" || nsc_err_cleanup_ns "${H1}"
	ip netns add "${RTB}" || nsc_err_cleanup_ns "${H1}" "${RTA}"
	ip netns add "${H2}" || nsc_err_cleanup_ns "${H1}" "${RTA}" "${RTB}"
}

# Create base networking topology:
#
#   * Set up the loopback device in all network namespaces.
#   * Create a veth pair to connect each netns in sequence.
#   * Add an IPv4 and an IPv6 address on each veth interface.
#
# Requires the network namespaces to already exist (see nsc_setup_ns()).
#
nsc_setup_base_net()
{
	for ns in "${H1}" "${RTA}" "${RTB}" "${H2}"; do
		ip -netns "${ns}" link set dev lo up
	done;

	nsc_veth_config "${H1}" "${RTA}" veth-h1-rta veth-rta-h1 1a a1
	nsc_veth_config "${RTA}" "${RTB}" veth-rta-rtb veth-rtb-rta ab ba
	nsc_veth_config "${RTB}" "${H2}" veth-rtb-h2 veth-h2-rtb b2 2b
}

# Configure the host IP addresses and routes in H1 and H2:
#
#   * Define the four host IP addresses on the loopback device of H1 and H2.
#   * Route these addresses in H1 and H2 through the adjacent router (with MPLS
#     encapsulation for two of them).
#   * No routing is defined between RTA and RTB, that's the responsibility of
#     the calling script.
#
# Requires the base network to be configured (see nsc_setup_base_net()).
#
nsc_setup_hosts_net()
{
	nsc_lo_config "${H1}" 1 2 192.0.2.0xa1 2001:db8::a1 veth-h1-rta
	nsc_lo_config "${H2}" 2 1 192.0.2.0xb2 2001:db8::b2 veth-h2-rtb
}
