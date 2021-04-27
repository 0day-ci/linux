#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="multipath_seed_test"
NUM_NETIFS=8
source lib.sh

veth_prepare()
{
	ip link add ecmp1l type veth peer name ecmp1r
	ip link add ecmp2l type veth peer name ecmp2r
	ip link add ecmphost1l type veth peer name ecmphost1r
	ip link add ecmphost2l type veth peer name ecmphost2r
}

cl1_create()
{
	local ns_exec="ip netns exec ecmp_cl1"

	ip netns add ecmp_cl1
	ip l set dev ecmphost1l netns ecmp_cl1
	$ns_exec ip l set dev ecmphost1l up
	$ns_exec ip a a 10.100.0.2/30 dev ecmphost1l
	$ns_exec ip a a 2001:db8:3::2/64 dev ecmphost1l
	$ns_exec ip r add default via 10.100.0.1
	$ns_exec ip r add default via 2001:db8:3::1
}

cl2_create()
{
	local ns_exec="ip netns exec ecmp_cl2"

	ip netns add ecmp_cl2
	ip l set dev ecmphost2l netns ecmp_cl2
	$ns_exec ip l set dev ecmphost2l up
	$ns_exec ip a a 10.200.0.2/30 dev ecmphost2l
	$ns_exec ip a a 2001:db8:4::2/64 dev ecmphost2l
	$ns_exec ip r add default via 10.200.0.1
	$ns_exec ip r add default via 2001:db8:4::1
}

r1_create()
{
	local ns_exec="ip netns exec ecmp1"

	ip netns add ecmp1
	ip l set dev ecmp1l netns ecmp1
	ip l set dev ecmp2l netns ecmp1
	ip l set dev ecmphost1r netns ecmp1
	$ns_exec ip l set dev ecmphost1r up
	$ns_exec ip l set dev ecmp1l up
	$ns_exec ip l set dev ecmp2l up
	$ns_exec ip a a 10.100.0.1/30 dev ecmphost1r
	$ns_exec ip a a 10.10.0.1/30 dev ecmp1l
	$ns_exec ip a a 10.20.0.1/30 dev ecmp2l
	$ns_exec ip a a 2001:db8:3::1/64 dev ecmphost1r
	$ns_exec ip a a 2001:db8:1::1/64 dev ecmp1l
	$ns_exec ip a a 2001:db8:2::1/64 dev ecmp2l
	$ns_exec sysctl -q net.ipv4.ip_forward=1
	$ns_exec sysctl -q net.ipv6.conf.all.forwarding=1
	$ns_exec sysctl -q net.ipv4.fib_multipath_hash_policy=1
	$ns_exec sysctl -q net.ipv6.fib_multipath_hash_policy=1
	$ns_exec ip route add 10.200.0.0/30 nexthop via 10.10.0.2 \
		  weight 1 nexthop via 10.20.0.2 weight 1
	$ns_exec ip route add 2001:db8:4::/64 nexthop via 2001:db8:1::2 \
		  weight 1 nexthop via 2001:db8:2::2 weight 1
}

r2_create()
{
	local ns_exec="ip netns exec ecmp2"

	ip netns add ecmp2
	ip l set dev ecmp1r netns ecmp2
	ip l set dev ecmp2r netns ecmp2
	ip l set dev ecmphost2r netns ecmp2
	$ns_exec ip l set dev ecmphost2r up
	$ns_exec ip l set dev ecmp1r up
	$ns_exec ip l set dev ecmp2r up
	$ns_exec ip a a 10.200.0.1/30 dev ecmphost2r
	$ns_exec ip a a 10.10.0.2/30 dev ecmp1r
	$ns_exec ip a a 10.20.0.2/30 dev ecmp2r
	$ns_exec ip a a 2001:db8:4::1/64 dev ecmphost2r
	$ns_exec ip a a 2001:db8:1::2/64 dev ecmp1r
	$ns_exec ip a a 2001:db8:2::2/64 dev ecmp2r
	$ns_exec sysctl -q net.ipv4.ip_forward=1
	$ns_exec sysctl -q net.ipv6.conf.all.forwarding=1
	$ns_exec sysctl -q net.ipv4.fib_multipath_hash_policy=1
	$ns_exec sysctl -q net.ipv6.fib_multipath_hash_policy=1
	$ns_exec ip route add 10.100.0.0/30 nexthop via 10.10.0.1 \
		  weight 1 nexthop via 10.20.0.1 weight 1
	$ns_exec ip route add 2001:db8:3::/64 nexthop via 2001:db8:1::1 \
		  weight 1 nexthop via 2001:db8:2::1 weight 1
}

cl1_destroy()
{
	ip netns del ecmp_cl1
}

cl2_destroy()
{
	ip netns del ecmp_cl2
}

r1_destroy()
{
	ip netns del ecmp1
}

r2_destroy()
{
	ip netns del ecmp2
}

gen_udp4()
{
	local sp=$1; shift
	local dp=$1; shift
	local tx1_1_start tx1_2_start tx2_1_start tx2_2_start
	local tx1_1_end tx1_2_end tx2_1_end tx2_2_end
	local tx1_1 tx1_2 tx2_1 tx2_2
	local tx1_1_res tx1_2_res tx2_1_res tx2_2_res
	local chan1 chan2
	local cl1_exec="ip netns exec ecmp_cl1"
	local cl2_exec="ip netns exec ecmp_cl2"

	tx1_1_start=$(ns_link_stats_tx_packets_get ecmp1 ecmp1l)
	tx1_2_start=$(ns_link_stats_tx_packets_get ecmp1 ecmp2l)
	tx2_1_start=$(ns_link_stats_tx_packets_get ecmp2 ecmp1r)
	tx2_2_start=$(ns_link_stats_tx_packets_get ecmp2 ecmp2r)

	$cl1_exec $MZ ecmphost1l -q -c 20 -p 64 -A 10.100.0.2 -B 10.200.0.2 \
		-t udp "sp=${sp},dp=${dp}"

	$cl2_exec $MZ ecmphost2l -q -c 20 -p 64 -A 10.200.0.2 -B 10.100.0.2 \
		-t udp "sp=${dp},dp=${sp}"

	tx1_1_end=$(ns_link_stats_tx_packets_get ecmp1 ecmp1l)
	tx1_2_end=$(ns_link_stats_tx_packets_get ecmp1 ecmp2l)
	tx2_1_end=$(ns_link_stats_tx_packets_get ecmp2 ecmp1r)
	tx2_2_end=$(ns_link_stats_tx_packets_get ecmp2 ecmp2r)

	let "tx1_1 = $tx1_1_end - $tx1_1_start"
	let "tx1_2 = $tx1_2_end - $tx1_2_start"
	let "tx2_1 = $tx2_1_end - $tx2_1_start"
	let "tx2_2 = $tx2_2_end - $tx2_2_start"

	[ "$tx1_1" -ge 20 ] && tx1_1_res=1 || tx1_1_res=0
	[ "$tx1_2" -ge 20 ] && tx1_2_res=1 || tx1_2_res=0
	[ "$tx2_1" -ge 20 ] && tx2_1_res=1 || tx2_1_res=0
	[ "$tx2_2" -ge 20 ] && tx2_2_res=1 || tx2_2_res=0

	let "chan1 = $tx1_1_res + $tx2_1_res"
	let "chan2 = $tx1_2_res + $tx2_2_res"

	if [ $chan1 -eq 2 ] || [ $chan2 -eq 2 ]; then
		return 0
	fi

	return 1;
}

gen_udp6()
{
	local sp=$1; shift
	local dp=$1; shift
	local tx1_1_start tx1_2_start tx2_1_start tx2_2_start
	local tx1_1_end tx1_2_end tx2_1_end tx2_2_end
	local tx1_1 tx1_2 tx2_1 tx2_2
	local tx1_1_res tx1_2_res tx2_1_res tx2_2_res
	local chan1 chan2
	local cl1_exec="ip netns exec ecmp_cl1"
	local cl2_exec="ip netns exec ecmp_cl2"

	tx1_1_start=$(ns_link_stats_tx_packets_get ecmp1 ecmp1l)
	tx1_2_start=$(ns_link_stats_tx_packets_get ecmp1 ecmp2l)
	tx2_1_start=$(ns_link_stats_tx_packets_get ecmp2 ecmp1r)
	tx2_2_start=$(ns_link_stats_tx_packets_get ecmp2 ecmp2r)

	$cl1_exec $MZ ecmphost1l -6 -q -c 20 -p 64 -A 2001:db8:3::2 -B 2001:db8:4::2 \
		-t udp "sp=${sp},dp=${dp}"

	$cl2_exec $MZ ecmphost2l -6 -q -c 20 -p 64 -A 2001:db8:4::2 -B 2001:db8:3::2 \
		-t udp "sp=${dp},dp=${sp}"

	tx1_1_end=$(ns_link_stats_tx_packets_get ecmp1 ecmp1l)
	tx1_2_end=$(ns_link_stats_tx_packets_get ecmp1 ecmp2l)
	tx2_1_end=$(ns_link_stats_tx_packets_get ecmp2 ecmp1r)
	tx2_2_end=$(ns_link_stats_tx_packets_get ecmp2 ecmp2r)

	let "tx1_1 = $tx1_1_end - $tx1_1_start"
	let "tx1_2 = $tx1_2_end - $tx1_2_start"
	let "tx2_1 = $tx2_1_end - $tx2_1_start"
	let "tx2_2 = $tx2_2_end - $tx2_2_start"

	[ "$tx1_1" -ge 20 ] && tx1_1_res=1 || tx1_1_res=0
	[ "$tx1_2" -ge 20 ] && tx1_2_res=1 || tx1_2_res=0
	[ "$tx2_1" -ge 20 ] && tx2_1_res=1 || tx2_1_res=0
	[ "$tx2_2" -ge 20 ] && tx2_2_res=1 || tx2_2_res=0

	let "chan1 = $tx1_1_res + $tx2_1_res"
	let "chan2 = $tx1_2_res + $tx2_2_res"

	if [ $chan1 -eq 2 ] || [ $chan2 -eq 2 ]; then
		return 0
	fi

	return 1;
}


seed4_test_equal()
{
	RET=0
	local sp
	local dp
	local i
	local res=0
	local seed=$(${OPENSSL} rand -hex 16)

	seed=${seed:0:16},${seed:16:16}

	ip netns exec ecmp1 sysctl -q \
		net.ipv4.fib_multipath_hash_seed=${seed}
	ip netns exec ecmp2 sysctl -q \
		net.ipv4.fib_multipath_hash_seed=${seed}

	for i in {1..30}; do
		sp=$(shuf -i 1024-65000 -n 1)
		dp=$(shuf -i 1024-65000 -n 1)
		gen_udp4 $sp $dp && let res++
	done

	[ $res != 30 ] && RET=1
	log_test "IPv4 multipath seed tests [equal seed]"
}

seed4_test_diff()
{
	RET=0
	local sp
	local dp
	local i
	local res=0
	local seed1=$(${OPENSSL} rand -hex 16)
	local seed2=$(${OPENSSL} rand -hex 16)

	seed1=${seed1:0:16},${seed1:16:16}
	seed2=${seed2:0:16},${seed2:16:16}

	ip netns exec ecmp1 sysctl -q \
		net.ipv4.fib_multipath_hash_seed=${seed1}
	ip netns exec ecmp2 sysctl -q \
		net.ipv4.fib_multipath_hash_seed=${seed2}

	for i in {1..30}; do
		sp=$(shuf -i 1024-65000 -n 1)
		dp=$(shuf -i 1024-65000 -n 1)
		gen_udp4 $sp $dp && let res++
	done

	[ $res -eq 30 ] && RET=1
	log_test "IPv4 multipath seed tests [different seed]"
}

seed6_test_equal()
{
	RET=0
	local sp
	local dp
	local i
	local res=0
	local seed=$(${OPENSSL} rand -hex 16)

	seed=${seed:0:16},${seed:16:16}

	ip netns exec ecmp1 sysctl -q \
		net.ipv6.fib_multipath_hash_seed=${seed}
	ip netns exec ecmp2 sysctl -q \
		net.ipv6.fib_multipath_hash_seed=${seed}

	for i in {1..30}; do
		sp=$(shuf -i 1024-65000 -n 1)
		dp=$(shuf -i 1024-65000 -n 1)
		gen_udp6 $sp $dp && let res++
	done

	[ $res != 30 ] && RET=1
	log_test "IPv6 multipath seed tests [equal seed]"
}

seed6_test_diff()
{
	RET=0
	local sp
	local dp
	local i
	local res=0
	local seed1=$(${OPENSSL} rand -hex 16)
	local seed2=$(${OPENSSL} rand -hex 16)

	seed1=${seed1:0:16},${seed1:16:16}
	seed2=${seed2:0:16},${seed2:16:16}

	ip netns exec ecmp1 sysctl -q \
		net.ipv6.fib_multipath_hash_seed=${seed1}
	ip netns exec ecmp2 sysctl -q \
		net.ipv6.fib_multipath_hash_seed=${seed2}

	for i in {1..30}; do
		sp=$(shuf -i 1024-65000 -n 1)
		dp=$(shuf -i 1024-65000 -n 1)
		gen_udp4 $sp $dp && let res++
	done

	[ $res -eq 30 ] && RET=1
	log_test "IPv6 multipath seed tests [different seed]"
}

multipath_seed_test()
{
	require_command $OPENSSL
	veth_prepare
	cl1_create
	cl2_create
	r1_create
	r2_create

	log_info "Running IPv4 multipath seed tests [equal seed]"
	seed4_test_equal
	log_info "Running IPv4 multipath seed tests [different seed]"
	seed4_test_diff
	log_info "Running IPv6 multipath seed tests [equal seed]"
	seed6_test_equal
	log_info "Running IPv6 multipath seed tests [different seed]"
	seed6_test_diff

	cl1_destroy
	cl2_destroy
	r1_destroy
	r2_destroy
}

tests_run

exit $EXIT_STATUS
