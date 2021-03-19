#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# OVS kernel module self tests
#
# Tests currently implemented:
#
# - mismatched_mtu_with_conntrack
#	Set up two namespaces (client and server) which each have devices specifying
#	incongruent MTUs (1450 vs 1500 in the test).  Transmit a UDP packet of 1901 bytes
#	from client to server, and back.  Ensure that the ct() action
#	uses the mru as a hint, but not a forced check.


# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

PAUSE_ON_FAIL=no
VERBOSE=0
TRACING=0

tests="
	mismatched_mtu_with_conntrack		ipv4: IP Fragmentation with conntrack"


usage() {
	echo
	echo "$0 [OPTIONS] [TEST]..."
	echo "If no TEST argument is given, all tests will be run."
	echo
	echo "Options"
	echo "  -t: capture traffic via tcpdump"
	echo "  -v: verbose"
	echo "  -p: pause on failure"
	echo
	echo "Available tests${tests}"
	exit 1
}

on_exit() {
	echo "$1" > ${ovs_dir}/cleanup.tmp
	cat ${ovs_dir}/cleanup >> ${ovs_dir}/cleanup.tmp
	mv ${ovs_dir}/cleanup.tmp ${ovs_dir}/cleanup
}

ovs_setenv() {
	sandbox=$1

	ovs_dir=$ovs_base${1:+/$1}; export ovs_dir

	test -e ${ovs_dir}/cleanup || : > ${ovs_dir}/cleanup

	OVS_RUNDIR=$ovs_dir; export OVS_RUNDIR
	OVS_LOGDIR=$ovs_dir; export OVS_LOGDIR
	OVS_DBDIR=$ovs_dir; export OVS_DBDIR
	OVS_SYSCONFDIR=$ovs_dir; export OVS_SYSCONFDIR
	OVS_PKGDATADIR=$ovs_dir; export OVS_PKGDATADIR
}

ovs_exit_sig() {
	. "$ovs_dir/cleanup"
	ovs-dpctl del-dp ovs-system
}

ovs_cleanup() {
	ovs_exit_sig
	[ $VERBOSE = 0 ] || echo "Error detected.  See $ovs_dir for more details."
}

ovs_normal_exit() {
	ovs_exit_sig
	rm -rf ${ovs_dir}
}

info() {
    [ $VERBOSE = 0 ] || echo $*
}

kill_ovs_vswitchd () {
	# Use provided PID or save the current PID if available.
	TMPPID=$1
	if test -z "$TMPPID"; then
		TMPPID=$(cat $OVS_RUNDIR/ovs-vswitchd.pid 2>/dev/null)
	fi

	# Tell the daemon to terminate gracefully
	ovs-appctl -t ovs-vswitchd exit --cleanup 2>/dev/null

	# Nothing else to be done if there is no PID
	test -z "$TMPPID" && return

	for i in 1 2 3 4 5 6 7 8 9; do
		# Check if the daemon is alive.
		kill -0 $TMPPID 2>/dev/null || return

		# Fallback to whole number since POSIX doesn't require
		# fractional times to work.
		sleep 0.1 || sleep 1
	done

	# Make sure it is terminated.
	kill $TMPPID
}

start_daemon () {
	info "exec: $@ -vconsole:off --detach --no-chdir --pidfile --log-file"
	"$@" -vconsole:off --detach --no-chdir --pidfile --log-file >/dev/null
	pidfile="$OVS_RUNDIR"/$1.pid

	info "setting kill for $@..."
	on_exit "test -e \"$pidfile\" && kill \`cat \"$pidfile\"\`"
}

if test "X$vswitchd_schema" = "X"; then
	vswitchd_schema="/usr/share/openvswitch"
fi

ovs_sbx() {
	if test "X$2" != X; then
		(ovs_setenv $1; shift; "$@" >> ${ovs_dir}/debug.log)
	else
		ovs_setenv $1
	fi
}

seq () {
	if test $# = 1; then
		set 1 $1
	fi
	while test $1 -le $2; do
		echo $1
		set `expr $1 + ${3-1}` $2 $3
	done
}

ovs_wait () {
	info "$1: waiting $2..."

	# First try the condition without waiting.
	if ovs_wait_cond; then info "$1: wait succeeded immediately"; return 0; fi

	# Try a quick sleep, so that the test completes very quickly
	# in the normal case.  POSIX doesn't require fractional times to
	# work, so this might not work.
	sleep 0.1
	if ovs_wait_cond; then info "$1: wait succeeded quickly"; return 0; fi

	# Then wait up to OVS_CTL_TIMEOUT seconds.
	local d
	for d in `seq 1 "$OVS_CTL_TIMEOUT"`; do
		sleep 1
		if ovs_wait_cond; then info "$1: wait succeeded after $d seconds"; return 0; fi
	done

	info "$1: wait failed after $d seconds"
	ovs_wait_failed
}

sbxs=
sbx_add () {
	info "adding sandbox '$1'"

	sbxs="$sbxs $1"

	NO_BIN=0
	which ovsdb-tool >/dev/null 2>&1 || NO_BIN=1
	which ovsdb-server >/dev/null 2>&1 || NO_BIN=1
	which ovs-vsctl >/dev/null 2>&1 || NO_BIN=1
	which ovs-vswitchd >/dev/null 2>&1 || NO_BIN=1

	if [ $NO_BIN = 1 ]; then
		info "Missing required binaries..."
		return 4
	fi
	# Create sandbox.
	local d="$ovs_base"/$1
	if [ -e $d ]; then
		info "removing $d"
		rm -rf "$d"
	fi
	mkdir "$d" || return 1
	ovs_setenv $1

	# Create database and start ovsdb-server.
        info "$1: create db and start db-server"
	: > "$d"/.conf.db.~lock~
	ovs_sbx $1 ovsdb-tool create "$d"/conf.db "$vswitchd_schema"/vswitch.ovsschema || return 1
	ovs_sbx $1 start_daemon ovsdb-server --detach --remote=punix:"$d"/db.sock || return 1

	# Initialize database.
	ovs_sbx $1 ovs-vsctl --no-wait -- init || return 1

	# Start ovs-vswitchd
        info "$1: start vswitchd"
	ovs_sbx $1 start_daemon ovs-vswitchd -vvconn -vofproto_dpif -vunixctl

	ovs_wait_cond () {
		if ip link show ovs-netdev >/dev/null 2>&1; then return 1; else return 0; fi
	}
	ovs_wait_failed () {
		:
	}

	ovs_wait "sandbox_add" "while ip link show ovs-netdev" || return 1
}

ovs_base=`pwd`

# mismatched_mtu_with_conntrack test
#  - client has 1450 byte MTU
#  - server has 1500 byte MTU
#  - use UDP to send 1901 bytes each direction for mismatched
#    fragmentation.
test_mismatched_mtu_with_conntrack() {

	sbx_add "test_mismatched_mtu_with_conntrack" || return $?

	info "create namespaces"
	for ns in client server; do
		ip netns add $ns || return 1
		on_exit "ip netns del $ns"
	done

	# setup the base bridge
	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-vsctl add-br br0 || return 1

	# setup the client
	info "setup client namespace"
	ip link add c0 type veth peer name c1 || return 1
	on_exit "ip link del c0 >/dev/null 2>&1"

	ip link set c0 mtu 1450
	ip link set c0 up

	ip link set c1 netns client || return 1
	ip netns exec client ip addr add 172.31.110.2/24 dev c1
	ip netns exec client ip link set c1 mtu 1450
	ip netns exec client ip link set c1 up
	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-vsctl add-port br0 c0 || return 1

	# setup the server
	info "setup server namespace"
	ip link add s0 type veth peer name s1 || return 1
	on_exit "ip link del s0 >/dev/null 2>&1; ip netns exec server ip link del s0 >/dev/null 2>&1"
	ip link set s0 up

	ip link set s1 netns server || return 1
	ip netns exec server ip addr add 172.31.110.1/24 dev s1 || return 1
	ip netns exec server ip link set s1 up
	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-vsctl add-port br0 s0 || return 1

	info "setup flows"
	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-ofctl del-flows br0

	cat >${ovs_dir}/flows.txt <<EOF
table=0,priority=100,arp action=normal
table=0,priority=100,ip,udp action=ct(table=1)
table=0,priority=10 action=drop

table=1,priority=100,ct_state=+new+trk,in_port=c0,ip action=ct(commit),s0
table=1,priority=100,ct_state=+est+trk,in_port=s0,ip action=c0

EOF
	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-ofctl --bundle add-flows br0 ${ovs_dir}/flows.txt || return 1
	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-ofctl dump-flows br0

	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-vsctl show

	# setup echo
	mknod -m 777 ${ovs_dir}/fifo p || return 1
	# on_exit "rm ${ovs_dir}/fifo"

	# test a udp connection
	info "send udp data"
	ip netns exec server sh -c 'cat ${ovs_dir}/fifo | nc -l -vv -u 8888 >${ovs_dir}/fifo 2>${ovs_dir}/s1-nc.log & echo $! > ${ovs_dir}/server.pid'
	on_exit "test -e \"${ovs_dir}/server.pid\" && kill \`cat \"${ovs_dir}/server.pid\"\`"
	if [ $TRACING = 1 ]; then
		ip netns exec server sh -c "tcpdump -i s1 -l -n -U -xx >> ${ovs_dir}/s1-pkts.cap &"
		ip netns exec client sh -c "tcpdump -i c1 -l -n -U -xx >> ${ovs_dir}/c1-pkts.cap &"
	fi

	ip netns exec client sh -c "python -c \"import time; print('a' * 1900); time.sleep(2)\" | nc -v -u 172.31.110.1 8888 2>${ovs_dir}/c1-nc.log" || return 1

	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-appctl dpctl/dump-flows
	ovs_sbx "test_mismatched_mtu_with_conntrack" ovs-ofctl dump-flows br0

	info "check udp data was tx and rx"
	grep "1901 bytes received" ${ovs_dir}/c1-nc.log || return 1
	ovs_normal_exit
}

run_test() {
	(
	tname="$1"
	tdesc="$2"

	if ! lsmod | grep openvswitch >/dev/null 2>&1; then
		printf "TEST: %-60s  [SKIP]\n" "${tdesc}"
		return $ksft_skip
	fi

	unset IFS

	eval test_${tname}
	ret=$?

	if [ $ret -eq 0 ]; then
		printf "TEST: %-60s  [ OK ]\n" "${tdesc}"
		ovs_normal_exit
	elif [ $ret -eq 1 ]; then
		printf "TEST: %-60s  [FAIL]\n" "${tdesc}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "Pausing. Hit enter to continue"
			read a
		fi
		ovs_exit_sig
		exit 1
	elif [ $ret -eq $ksft_skip ]; then
		printf "TEST: %-60s  [SKIP]\n" "${tdesc}"
	elif [ $ret -eq 2 ]; then
		rm -rf test_${tname}
		run_test "$1" "$2"
	fi

	return $ret
	)
	ret=$?
	case $ret in
		0)
			all_skipped=false
			[ $exitcode=$ksft_skip ] && exitcode=0
		;;
		$ksft_skip)
			[ $all_skipped = true ] && exitcode=$ksft_skip
		;;
		*)
			all_skipped=false
			exitcode=1
		;;
	esac

	return $ret
}


exitcode=0
desc=0
all_skipped=true

while getopts :pvt o
do
	case $o in
	p) PAUSE_ON_FAIL=yes;;
	v) VERBOSE=1;;
	t) if which tcpdump > /dev/null 2>&1; then
		TRACING=1
	   else
		echo "=== tcpdump not available, tracing disabled"
	   fi
	   ;;
	*) usage;;
	esac
done
shift $(($OPTIND-1))

IFS="	
"

for arg do
	# Check first that all requested tests are available before running any
	command -v > /dev/null "test_${arg}" || { echo "=== Test ${arg} not found"; usage; }
done

name=""
desc=""
for t in ${tests}; do
	[ "${name}" = "" ]	&& name="${t}"	&& continue
	[ "${desc}" = "" ]	&& desc="${t}"

	run_this=1
	for arg do
		[ "${arg}" != "${arg#--*}" ] && continue
		[ "${arg}" = "${name}" ] && run_this=1 && break
		run_this=0
	done
	if [ $run_this -eq 1 ]; then
		run_test "${name}" "${desc}"
	fi
	name=""
	desc=""
done

exit ${exitcode}
