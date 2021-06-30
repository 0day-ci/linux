#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test redirecting frames received on L3 tunnel interfaces to an Ethernet
# interface, after having added an Ethernet header.
#
# Example:
#
#  $ tc filter add dev ipip0 ingress matchall          \
#       action vlan push_eth dst_mac 00:00:5e:00:53:01 \
#                            src_mac 00:00:5e:00:53:00 \
#       action mirred egress redirect dev eth0
#
# Network topology is as follow: H1 and H2 are end hosts, separated by two
# routers, RTA and RTB. They ping each other using IPv4, IPv6, IPv4 in MPLS
# and IPv6 in MPLS packets. The L3 tunnel to test is established between RTA
# and RTB. These routers redirect packets from the tunnel to the end host's
# veth and the other way around.
#
# This scripts only needs to define how packets are forwarded between RTA and
# RTB (as that's where we do and test the tunnel encapsulation and packet
# redirection). The base network configuration is done in topo_nschain_lib.sh.

ALL_TESTS="
	redir_gre
	redir_ipip
	redir_sit
	redir_ip6gre
	redir_ip6tnl
	redir_vxlan_gpe
	redir_bareudp
"

NUM_NETIFS=0

source topo_nschain_lib.sh
source lib.sh

readonly KSFT_PASS=0
readonly KSFT_FAIL=1
readonly KSFT_SKIP=4

KSFT_RET="${KSFT_PASS}"
TESTS_COMPLETED="no"

# Create tunnels between RTA and RTB, and forward packets between tunnel and
# veth interfaces.
#
# Parameters:
#
#   * $1: IP version of the underlay to use ("ipv4" or "ipv6").
#   * $2: Tunnel mode (either "classical" or "collect_md").
#   * $3: Device type (as in "ip link add mydev type <dev_type> ...").
#   * $4: Options for the "ip link add" command
#         (as in "ip link add mydev type dev_type <options>").
#   * $5: Options for the TC tunnel_key command
#         (as in "tc filter add ... action tunnel_key set <options>").
#
# For classical tunnels, the "local" and "remote" options of "ip link add" are
# set automatically and mustn't appear in $4.
#
# For collect_md tunnels, the "src_ip" and "dst_ip" options of
# "action tunnel_key" are set automatically and mustn't appear in $5.
#
setup_tunnel()
{
	local UNDERLAY_PROTO="$1"; readonly UNDERLAY_PROTO
	local TUNNEL_MODE="$2"; readonly TUNNEL_MODE
	local DEV_TYPE="$3"; readonly DEV_TYPE
	local DEV_OPTS="$4"; readonly DEV_OPTS
	local TK_OPTS="$5"; readonly TK_OPTS
	local RTA_TUNNEL_OPTS
	local RTB_TUNNEL_OPTS
	local RTA_TK_ACTION
	local RTB_TK_ACTION
	local IP_RTA
	local IP_RTB

	case "${UNDERLAY_PROTO}" in
		"IPv4"|"ipv4")
			IP_RTA="192.0.2.0xab"
			IP_RTB="192.0.2.0xba"
			;;
		"IPv6"|"ipv6")
			IP_RTA="2001:db8::ab"
			IP_RTB="2001:db8::ba"
			;;
		*)
			exit 1
			;;
	esac

	case "${TUNNEL_MODE}" in
		"classical")
			# Classical tunnel: underlay IP addresses are part of
			# the tunnel configuration.
			RTA_TUNNEL_OPTS="local ${IP_RTA} remote ${IP_RTB} ${DEV_OPTS}"
			RTB_TUNNEL_OPTS="local ${IP_RTB} remote ${IP_RTA} ${DEV_OPTS}"
			RTA_TK_ACTION=""
			RTB_TK_ACTION=""
			;;
		"collect_md")
			# External tunnel: underlay IP addresses are attached
			# to the packets' metadata with the tunnel_key action
			RTA_TUNNEL_OPTS="${DEV_OPTS}"
			RTB_TUNNEL_OPTS="${DEV_OPTS}"
			RTA_TK_ACTION="action tunnel_key set src_ip ${IP_RTA} dst_ip ${IP_RTB} ${TK_OPTS}"
			RTB_TK_ACTION="action tunnel_key set src_ip ${IP_RTB} dst_ip ${IP_RTA} ${TK_OPTS}"
			;;
		*)
		    echo "Internal error: setup_tunnel(): invalid tunnel mode \"${TUNNEL_MODE}\""
		    return 1
		    ;;
	esac

	# Transform options strings to arrays, so we can pass them to the ip or
	# tc commands with double quotes (prevents shellcheck warning).
	read -ra RTA_TUNNEL_OPTS <<< "${RTA_TUNNEL_OPTS}"
	read -ra RTB_TUNNEL_OPTS <<< "${RTB_TUNNEL_OPTS}"
	read -ra RTA_TK_ACTION <<< "${RTA_TK_ACTION}"
	read -ra RTB_TK_ACTION <<< "${RTB_TK_ACTION}"

	tc -netns "${RTA}" qdisc add dev veth-rta-h1 ingress
	tc -netns "${RTB}" qdisc add dev veth-rtb-h2 ingress

	ip -netns "${RTA}" link add name tunnel-rta up type "${DEV_TYPE}" \
		"${RTA_TUNNEL_OPTS[@]}"
	ip -netns "${RTB}" link add name tunnel-rtb up type "${DEV_TYPE}" \
		"${RTB_TUNNEL_OPTS[@]}"

	# Encapsulate IPv4 packets
	tc -netns "${RTA}" filter add dev veth-rta-h1 ingress	\
		protocol ipv4 flower dst_ip 198.51.100.2	\
		"${RTA_TK_ACTION[@]}"				\
		action mirred egress redirect dev tunnel-rta
	tc -netns "${RTB}" filter add dev veth-rtb-h2 ingress	\
		protocol ipv4 flower dst_ip 198.51.100.1	\
		"${RTB_TK_ACTION[@]}"				\
		action mirred egress redirect dev tunnel-rtb

	# Encapsulate IPv6 packets
	tc -netns "${RTA}" filter add dev veth-rta-h1 ingress	\
		protocol ipv6 flower dst_ip 2001:db8::1:2	\
		"${RTA_TK_ACTION[@]}"				\
		action mirred egress redirect dev tunnel-rta
	tc -netns "${RTB}" filter add dev veth-rtb-h2 ingress	\
		protocol ipv6 flower dst_ip 2001:db8::1:1	\
		"${RTB_TK_ACTION[@]}"				\
		action mirred egress redirect dev tunnel-rtb

	# Encapsulate MPLS packets
	tc -netns "${RTA}" filter add dev veth-rta-h1 ingress	\
		protocol mpls_uc matchall			\
		"${RTA_TK_ACTION[@]}"				\
		action mirred egress redirect dev tunnel-rta
	tc -netns "${RTB}" filter add dev veth-rtb-h2 ingress	\
		protocol mpls_uc matchall			\
		"${RTB_TK_ACTION[@]}"				\
		action mirred egress redirect dev tunnel-rtb

	tc -netns "${RTA}" qdisc add dev tunnel-rta ingress
	tc -netns "${RTB}" qdisc add dev tunnel-rtb ingress

	# Redirect packets from tunnel devices to end hosts
	tc -netns "${RTA}" filter add dev tunnel-rta ingress matchall	\
		action vlan push_eth dst_mac 00:00:5e:00:53:1a		\
			src_mac 00:00:5e:00:53:a1			\
		action mirred egress redirect dev veth-rta-h1
	tc -netns "${RTB}" filter add dev tunnel-rtb ingress matchall	\
		action vlan push_eth dst_mac 00:00:5e:00:53:2b		\
			src_mac 00:00:5e:00:53:b2			\
		action mirred egress redirect dev veth-rtb-h2
}

# Remove everything that was created by setup_tunnel().
#
cleanup_tunnel()
{
	ip -netns "${RTB}" link delete dev tunnel-rtb
	ip -netns "${RTA}" link delete dev tunnel-rta
	tc -netns "${RTB}" qdisc delete dev veth-rtb-h2 ingress
	tc -netns "${RTA}" qdisc delete dev veth-rta-h1 ingress
}

# Ping H2 from H1.
#
# Parameters:
#
#   $1: The protocol used for the ping test:
#         * ipv4: use plain IPv4 packets,
#         * ipv6: use plain IPv6 packets,
#         * ipv4-mpls: use IPv4 packets encapsulated into MPLS,
#         * ipv6-mpls: use IPv6 packets encapsulated into MPLS.
#   $2: Description of the test.
#
ping_test()
{
	local PROTO="$1"; readonly PROTO
	local MSG="$2"; readonly MSG
	local PING_CMD
	local PING_IP

	case "${PROTO}" in
		"ipv4")
			PING_CMD="${PING}"
			PING_IP="198.51.100.2"
			;;
		"ipv6")
			PING_CMD="${PING6}"
			PING_IP="2001:db8::1:2"
			;;
		"ipv4-mpls")
			PING_CMD="${PING}"
			PING_IP="198.51.100.0x12"
			;;
		"ipv6-mpls")
			PING_CMD="${PING6}"
			PING_IP="2001:db8::1:12"
			;;
		*)
			echo "Internal error: ping_test(): invalid protocol \"${PROTO}\""
			return 1
			;;
	esac

	set +e
	RET=0
	ip netns exec "${H1}" "${PING_CMD}" -w "${PING_TIMEOUT}" -c 1 "${PING_IP}" > /dev/null 2>&1
	RET=$?
	log_test "${MSG}" || KSFT_RET="${KSFT_FAIL}"
	set -e
}

# Inform the user and the kselftest infrastructure that a test has been
# skipped.
#
# Parameters:
#
#   $1: Description of the reason why the test was skipped.
#
skip_test()
{
	echo "SKIP: $1"

	# Do not override KSFT_FAIL
	if [ "${KSFT_RET}" -eq "${KSFT_PASS}" ]; then
		KSFT_RET="${KSFT_SKIP}"
	fi
}

# Check that no fallback tunnels are automatically created in new network
# namespaces.
#
has_fb_tunnels()
{
	local FB_TUNNELS

	FB_TUNNELS=$(sysctl -n net.core.fb_tunnels_only_for_init_net 2>/dev/null || echo 0);

	if [ "${FB_TUNNELS}" -ne 0 ]; then
		return 1
	else
		return 0
	fi
}

redir_gre()
{
	setup_tunnel "ipv4" "classical" "gre"
	ping_test ipv4 "GRE, classical mode: IPv4 / GRE / IPv4"
	ping_test ipv6 "GRE, classical mode: IPv4 / GRE / IPv6"
	ping_test ipv4-mpls "GRE, classical mode: IPv4 / GRE / MPLS / IPv4"
	ping_test ipv6-mpls "GRE, classical mode: IPv4 / GRE / MPLS / IPv6"
	cleanup_tunnel

	setup_tunnel "ipv4" "collect_md" "gre" "external" "nocsum"
	ping_test ipv4 "GRE, external mode: IPv4 / GRE / IPv4"
	ping_test ipv6 "GRE, external mode: IPv4 / GRE / IPv6"
	ping_test ipv4-mpls "GRE, external mode: IPv4 / GRE / MPLS / IPv4"
	ping_test ipv6-mpls "GRE, external mode: IPv4 / GRE / MPLS / IPv6"
	cleanup_tunnel
}

redir_ipip()
{
	setup_tunnel "ipv4" "classical" "ipip" "mode any"
	ping_test ipv4 "IPIP, classical mode: IPv4 / IPv4"
	ping_test ipv4-mpls "IPIP, classical mode: IPv4 / MPLS / IPv4"
	ping_test ipv6-mpls "IPIP, classical mode: IPv4 / MPLS / IPv6"
	cleanup_tunnel

	setup_tunnel "ipv4" "collect_md" "ipip" "mode any external"
	ping_test ipv4 "IPIP, external mode: IPv4 / IPv4"
	ping_test ipv4-mpls "IPIP, external mode: IPv4 / MPLS / IPv4"
	ping_test ipv6-mpls "IPIP, external mode: IPv4 / MPLS / IPv6"
	cleanup_tunnel
}

redir_sit()
{
	setup_tunnel "ipv4" "classical" "sit" "mode any"
	ping_test ipv4 "SIT, classical mode: IPv4 / IPv4"
	ping_test ipv6 "SIT, classical mode: IPv4 / IPv6"
	ping_test ipv4-mpls "SIT, classical mode: IPv4 / MPLS / IPv4"
	ping_test ipv6-mpls "SIT, classical mode: IPv4 / MPLS / IPv6"
	cleanup_tunnel

	if has_fb_tunnels; then
		skip_test "SIT, can't test the external mode, fallback tunnels are enabled: try \"sysctl -wq net.core.fb_tunnels_only_for_init_net=2\""
		return 0
	fi

	setup_tunnel "ipv4" "collect_md" "sit" "mode any external"
	ping_test ipv4 "SIT, external mode: IPv4 / IPv4"

	# ip6ip currently doesn' work in collect_md mode
	skip_test "SIT, ip6ip is known to fail in external mode (at least on Linux 5.13 and earlier versions)"
	#ping_test ipv6 "SIT, external mode: IPv4 / IPv6"

	ping_test ipv4-mpls "SIT, external mode: IPv4 / MPLS / IPv4"
	ping_test ipv6-mpls "SIT, external mode: IPv4 / MPLS / IPv6"
	cleanup_tunnel
}

redir_ip6gre()
{
	setup_tunnel "ipv6" "classical" "ip6gre"
	ping_test ipv4 "IP6GRE, classical mode: IPv6 / GRE / IPv4"
	ping_test ipv6 "IP6GRE, classical mode: IPv6 / GRE / IPv6"
	ping_test ipv4-mpls "IP6GRE, classical mode: IPv6 / GRE / MPLS / IPv4"
	ping_test ipv6-mpls "IP6GRE, classical mode: IPv6 / GRE / MPLS / IPv6"
	cleanup_tunnel

	setup_tunnel "ipv6" "collect_md" "ip6gre" "external" "nocsum"
	ping_test ipv4 "IP6GRE, external mode: IPv6 / GRE / IPv4"
	ping_test ipv6 "IP6GRE, external mode: IPv6 / GRE / IPv6"
	ping_test ipv4-mpls "IP6GRE, external mode: IPv6 / GRE / MPLS / IPv4"
	ping_test ipv6-mpls "IP6GRE, external mode: IPv6 / GRE / MPLS / IPv6"
	cleanup_tunnel
}

redir_ip6tnl()
{
	setup_tunnel "ipv6" "classical" "ip6tnl" "mode any"
	ping_test ipv4 "IP6TNL, classical mode: IPv6 / IPv4"
	ping_test ipv6 "IP6TNL, classical mode: IPv6 / IPv6"
	ping_test ipv4-mpls "IP6TNL, classical mode: IPv6 / MPLS / IPv4"
	ping_test ipv6-mpls "IP6TNL, classical mode: IPv6 / MPLS / IPv6"
	cleanup_tunnel

	setup_tunnel "ipv6" "collect_md" "ip6tnl" "mode any external"
	ping_test ipv4 "IP6TNL, external mode: IPv6 / IPv4"
	ping_test ipv6 "IP6TNL, external mode: IPv6 / IPv6"
	ping_test ipv4-mpls "IP6TNL, external mode: IPv6 / MPLS / IPv4"
	ping_test ipv6-mpls "IP6TNL, external mode: IPv6 / MPLS / IPv6"
	cleanup_tunnel
}

redir_vxlan_gpe()
{
	local IP

	# As of Linux 5.13, VXLAN-GPE only supports collect-md mode
	for UNDERLAY_IPVERS in 4 6; do
		IP="IPv${UNDERLAY_IPVERS}"

		setup_tunnel "${IP}" "collect_md" "vxlan" "gpe external" "id 10"
		ping_test ipv4 "VXLAN-GPE, external mode: ${IP} / UDP / VXLAN-GPE / IPv4"
		ping_test ipv6 "VXLAN-GPE, external mode: ${IP} / UDP / VXLAN-GPE / IPv6"
		ping_test ipv4-mpls "VXLAN-GPE, external mode: ${IP} / UDP / VXLAN-GPE / MPLS / IPv4"
		ping_test ipv6-mpls "VXLAN-GPE, external mode: ${IP} / UDP / VXLAN-GPE / MPLS / IPv6"
		cleanup_tunnel
	done
}

redir_bareudp()
{
	local IP

	# As of Linux 5.13, Bareudp only supports collect-md mode
	for UNDERLAY_IPVERS in 4 6; do
		IP="IPv${UNDERLAY_IPVERS}"

		# IPv4 overlay
		setup_tunnel "${IP}" "collect_md" "bareudp" \
			"dstport 6635 ethertype ipv4"
		ping_test ipv4 "Bareudp, external mode: ${IP} / UDP / IPv4"
		cleanup_tunnel

		# IPv6 overlay
		setup_tunnel "${IP}" "collect_md" "bareudp" \
			"dstport 6635 ethertype ipv6"
		ping_test ipv6 "Bareudp, external mode: ${IP} / UDP / IPv6"
		cleanup_tunnel

		# Combined IPv4/IPv6 overlay (multiproto mode)
		setup_tunnel "${IP}" "collect_md" "bareudp" \
			"dstport 6635 ethertype ipv4 multiproto"
		ping_test ipv4 "Bareudp, external mode: ${IP} / UDP / IPv4 (multiproto)"
		ping_test ipv6 "Bareudp, external mode: ${IP} / UDP / IPv6 (multiproto)"
		cleanup_tunnel

		# MPLS overlay
		setup_tunnel "${IP}" "collect_md" "bareudp" \
			"dstport 6635 ethertype mpls_uc"
		ping_test ipv4-mpls "Bareudp, external mode: ${IP} / UDP / MPLS / IPv4"
		ping_test ipv6-mpls "Bareudp, external mode: ${IP} / UDP / MPLS / IPv6"
		cleanup_tunnel
	done
}

exit_cleanup()
{
	if [ "${TESTS_COMPLETED}" = "no" ]; then
		KSFT_RET="${KSFT_FAIL}"
	fi

	pre_cleanup
	nsc_cleanup_ns "${H1}" "${RTA}" "${RTB}" "${H2}"
	exit "${KSFT_RET}"
}


if ! tc actions add action vlan help 2>&1 | grep --quiet 'push_eth'; then
	echo "SKIP: iproute2 is too old: tc doesn't support action \"push_eth\""
	exit "${KSFT_SKIP}"
fi

nsc_setup_ns || exit "${KSFT_FAIL}"

set -e
trap exit_cleanup EXIT

nsc_setup_base_net
nsc_setup_hosts_net

tests_run
TESTS_COMPLETED="yes"
