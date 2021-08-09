#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# <:copyright-gpl
# Copyright (C) 2021 Allied Telesis Labs NZ
#
# check that NAT can masquerade using PSID defined ranges.
#
# Setup is:
#
# nsclient1(veth0) -> (veth1)nsrouter(veth2) -> (veth0)nsclient2
# Setup a nat masquerade rule with psid defined ranges.
#

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0
ns_all="nsclient1 nsrouter nsclient2"

readonly infile="$(mktemp)"
readonly outfile="$(mktemp)"
readonly datalen=32
readonly server_port=8080

conntrack -V > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without conntrack tool"
	exit $ksft_skip
fi

iptables --version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without iptables tool"
	exit $ksft_skip
fi

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

ipv4() {
	echo -n 192.168.$1.$2
}

cleanup() {
	for n in $ns_all; do ip netns del $n;done

	if [ -f "${outfile}" ]; then
		rm "$outfile"
	fi
	if [ -f "${infile}" ]; then
		rm "$infile"
	fi
}

server_listen() {
	ip netns exec nsclient2 nc -l -p "$server_port" > "$outfile" &
	server_pid=$!
	sleep 0.2
}

client_connect() {
	ip netns exec nsclient1 timeout 2 nc -w 1 -p "$port" $(ipv4 2 2) "$server_port" < $infile
}

verify_data() {
	local _ret=0
	wait "$server_pid"
	cmp "$infile" "$outfile" 2>/dev/null
	_ret=$?
	rm "$outfile"
	return $_ret
}

test_service() {
	server_listen
	client_connect
	verify_data
}

check_connection() {
	local _ret=0
	entry=$(ip netns exec nsrouter conntrack -p tcp --sport $port -L 2>&1)
	entry=${entry##*sport=8080 dport=}
	entry=${entry%% *}

	if [[ "x$(( ($entry & $psid_mask) / $two_power_j ))" != "x$psid" ]]; then
		_ret=1
		echo "Failed psid mask check for $offset_len:$psid:$psid_length with port $entry"
	fi

	if [[ "x$_ret" = "x0" ]] &&
	   [[ "x$offset_mask" != "x0" -a "x$(( ($entry & $offset_mask) ))" == "x0" ]]; then
		_ret=1
		echo "Failed offset mask check for $offset_len:$psid:$psid_length with port $entry"
	fi
	return $_ret
}

run_test() {
	ip netns exec nsrouter iptables -A FORWARD -i veth1 -j ACCEPT
	ip netns exec nsrouter iptables -P FORWARD DROP
	ip netns exec nsrouter iptables -A FORWARD -m state --state ESTABLISHED,RELATED -j ACCEPT
	ip netns exec nsrouter iptables -t nat --new psid
	ip netns exec nsrouter iptables -t nat --insert psid -j MASQUERADE \
		--psid $offset_len:$psid:$psid_length
	ip netns exec nsrouter iptables -t nat -I POSTROUTING -o veth2 -j psid

	# calculate psid mask
	offset=$(( 1 << (16 - $offset_len) ))
	two_power_j=$(( $offset / (1 << $psid_length) ))
	offset_mask=$(( ( (1 << $offset_len) - 1 ) << (16 - $offset_len) ))
	psid_mask=$(( ( (1 << $psid_length) - 1) * $two_power_j ))

	# Create file
	dd if=/dev/urandom of="${infile}" bs="${datalen}" count=1 >/dev/null 2>&1

	# Test multiple ports
	for p in 1 2 3 4 5; do
		port=1080$p

		test_service
		if [ $? -ne 0 ]; then
			ret=1
			break
		fi

		check_connection
		if [ $? -ne 0 ]; then
			ret=1
			break
		fi
	done

	# tidy up test rules
	ip netns exec nsrouter iptables -F
	ip netns exec nsrouter iptables -t nat -F
	ip netns exec nsrouter iptables -t nat -X psid
}

for n in $ns_all; do
	ip netns add $n
	ip -net $n link set lo up
done

for i in 1 2; do
	ip link add veth0 netns nsclient$i type veth peer name veth$i netns nsrouter

	ip -net nsclient$i link set veth0 up
	ip -net nsclient$i addr add $(ipv4 $i 2)/24 dev veth0

	ip -net nsrouter link set veth$i up
	ip -net nsrouter addr add $(ipv4 $i 1)/24 dev veth$i
done

ip -net nsclient1 route add default via $(ipv4 1 1)
ip -net nsclient2 route add default via $(ipv4 2 1)

ip netns exec nsrouter sysctl -q net.ipv4.conf.all.forwarding=1

offset_len=0
psid_length=8
for psid in 0 52; do
	run_test
	if [ $? -ne 0 ]; then
		break
	fi
done

offset_len=6
psid_length=8
for psid in 0 52; do
	run_test
	if [ $? -ne 0 ]; then
		break
	fi
done

cleanup
exit $ret
