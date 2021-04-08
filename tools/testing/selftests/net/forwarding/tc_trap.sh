#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# In the following simple routing scenario, put SW datapath packet probes on
# $swp1, $swp2 and $h2. Always expect packets to arrive at $h2. Depending on
# whether, in the HW datapath, $swp1 lets packets pass, traps them, or
# traps_forwards them, $swp1 and $swp2 probes are expected to give different
# results.
#
# +----------------------+                             +----------------------+
# | H1                   |                             |                   H2 |
# |    + $h1             |                             |            $h2 +     |
# |    | 192.0.2.1/28    |                             |  192.0.2.18/28 |     |
# +----|-----------------+                             +----------------|-----+
#      |                                                                |
# +----|----------------------------------------------------------------|-----+
# | SW |                                                                |     |
# |    + $swp1                                                    $swp2 +     |
# |      192.0.2.2/28                                     192.0.2.17/28       |
# +---------------------------------------------------------------------------+


ALL_TESTS="
	no_trap_test
	trap_fwd_test
	trap_test
"

NUM_NETIFS=4
source lib.sh
source tc_common.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
	ip route add vrf v$h1 192.0.2.16/28 via 192.0.2.2
}

h1_destroy()
{
	ip route del vrf v$h1 192.0.2.16/28 via 192.0.2.2
	simple_if_fini $h1 192.0.2.1/28
}

h2_create()
{
	simple_if_init $h2 192.0.2.18/28
	ip route add vrf v$h2 192.0.2.0/28 via 192.0.2.17
	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact
	ip route del vrf v$h2 192.0.2.0/28 via 192.0.2.17
	simple_if_fini $h2 192.0.2.18/28
}

switch_create()
{
	simple_if_init $swp1 192.0.2.2/28
	__simple_if_init $swp2 v$swp1 192.0.2.17/28

	tc qdisc add dev $swp1 clsact
	tc qdisc add dev $swp2 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp2 clsact
	tc qdisc del dev $swp1 clsact

	__simple_if_fini $swp2 192.0.2.17/28
	simple_if_fini $swp1 192.0.2.2/28
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

__test()
{
	local action=$1; shift
	local ingress_should_fail=$1; shift
	local egress_should_fail=$1; shift

	tc filter add dev $swp1 ingress protocol ip pref 2 handle 101 \
		flower skip_sw dst_ip 192.0.2.18 action $action
	tc filter add dev $swp1 ingress protocol ip pref 1 handle 102 \
		flower skip_hw dst_ip 192.0.2.18 action pass
	tc filter add dev $swp2 egress protocol ip pref 1 handle 103 \
		flower skip_hw dst_ip 192.0.2.18 action pass
	tc filter add dev $h2 ingress protocol ip pref 1 handle 104 \
		flower dst_ip 192.0.2.18 action drop

	RET=0

	$MZ $h1 -c 1 -p 64 -a $(mac_get $h1) -b $(mac_get $swp1) \
		-A 192.0.2.1 -B 192.0.2.18 -q -t ip

	tc_check_packets "dev $swp1 ingress" 102 1
	check_err_fail $ingress_should_fail $? "ingress should_fail $ingress_should_fail"

	tc_check_packets "dev $swp2 egress" 103 1
	check_err_fail $egress_should_fail $? "egress should_fail $egress_should_fail"

	tc_check_packets "dev $h2 ingress" 104 1
	check_err $? "Did not see the packet on host"

	log_test "$action test"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 104 flower
	tc filter del dev $swp2 egress protocol ip pref 1 handle 103 flower
	tc filter del dev $swp1 ingress protocol ip pref 1 handle 102 flower
	tc filter del dev $swp1 ingress protocol ip pref 2 handle 101 flower
}

no_trap_test()
{
	__test pass 1 1
}

trap_fwd_test()
{
	__test trap_fwd 0 1
}

trap_test()
{
	__test trap 0 0
}

trap cleanup EXIT

setup_prepare
setup_wait

if ! tc_offload_check; then
	check_err 1 "Could not test offloaded functionality"
	log_test "offloaded tc_trap test"
	exit
fi

tests_run

exit $EXIT_STATUS
