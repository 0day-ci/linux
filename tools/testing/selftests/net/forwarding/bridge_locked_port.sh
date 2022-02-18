#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="locked_port_ipv4 locked_port_ipv6 locked_port_vlan"
NUM_NETIFS=4
CHECK_TC="no"
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64
	vrf_create "vrf-vlan-h1"
        ip link set dev vrf-vlan-h1 up
        vlan_create $h1 100 vrf-vlan-h1 192.0.3.1/24 2001:db8:3::1/64
}

h1_destroy()
{
	vlan_destroy $h1 100
	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24 2001:db8:1::2/64
	vrf_create "vrf-vlan-h2"
	ip link set dev vrf-vlan-h2 up
	vlan_create $h2 100 vrf-vlan-h2 192.0.3.2/24 2001:db8:3::2/64
}

h2_destroy()
{
	vlan_destroy $h2 100
	simple_if_fini $h2 192.0.2.2/24 2001:db8:1::2/64
}

switch_create()
{
	ip link add dev br0 type bridge vlan_filtering 1

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up

	bridge link set dev $swp1 learning off
}

switch_destroy()
{
	ip link set dev $swp2 down
	ip link set dev $swp1 down

	ip link del dev br0
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare

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

	vrf_cleanup
}

ifaddr()
{
	ip -br link show dev "$1" | awk '{ print($3); }'
}

locked_port_ipv4()
{
	RET=0

	check_locked_port_support || return 0

	ping_do $h1 192.0.2.2
	check_err $? "Ping didn't work when it should have"

	bridge link set dev $swp1 locked on

	ping_do $h1 192.0.2.2
	check_fail $? "Ping worked when it should not have"

	bridge fdb add `ifaddr $h1` dev $swp1 master static

	ping_do $h1 192.0.2.2
	check_err $? "Ping didn't work when it should have"

	bridge link set dev $swp1 locked off
	bridge fdb del `ifaddr $h1` dev $swp1 master static
	log_test "Locked port ipv4"
}

locked_port_vlan()
{
	RET=0

	check_locked_port_support || return 0
	check_vlan_filtering_support || return 0

	bridge vlan add vid 100 dev $swp1 tagged
	bridge vlan add vid 100 dev $swp2 tagged

	ping_do $h1.100 192.0.3.2
	check_err $? "Ping didn't work when it should have"

	bridge link set dev $swp1 locked on
	ping_do $h1.100 192.0.3.2
	check_fail $? "Ping worked when it should not have"

	bridge fdb add `ifaddr $h1` dev $swp1 vlan 100 master static

	ping_do $h1.100 192.0.3.2
	check_err $? "Ping didn't work when it should have"

	bridge link set dev $swp1 locked off
	bridge vlan del vid 100 dev $swp1
	bridge vlan del vid 100 dev $swp2
	bridge fdb del `ifaddr $h1` dev $swp1 vlan 100 master static
	log_test "Locked port vlan"
}

locked_port_ipv6()
{
	RET=0
	check_locked_port_support || return 0

	ping6_do $h1 2001:db8:1::2
	check_err $? "Ping6 didn't work when it should have"

	bridge link set dev $swp1 locked on

	ping6_do $h1 2001:db8:1::2
	check_fail $? "Ping worked when it should not have"

	bridge fdb add `ifaddr $h1` dev $swp1 master static
	ping6_do $h1 2001:db8:1::2
	check_err $? "Ping didn't work when it should have"

	bridge link set dev $swp1 locked off
	bridge fdb del `ifaddr $h1` dev $swp1 master static
	log_test "Locked port ipv6"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
