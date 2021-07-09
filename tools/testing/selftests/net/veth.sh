#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

readonly STATS="$(mktemp -p /tmp ns-XXXXXX)"
readonly BASE=`basename $STATS`
readonly SRC=2
readonly DST=1
readonly DST_NAT=100
readonly NS_SRC=$BASE$SRC
readonly NS_DST=$BASE$DST

# "baremetal" network used for raw UDP traffic
readonly BM_NET_V4=192.168.1.
readonly BM_NET_V6=2001:db8::

readonly NPROCS=`nproc`
ret=0

cleanup() {
	local ns
	local jobs
	readonly jobs="$(jobs -p)"
	[ -n "${jobs}" ] && kill -1 ${jobs} 2>/dev/null
	rm -f $STATS

	for ns in $NS_SRC $NS_DST; do
		ip netns del $ns 2>/dev/null
	done
}

trap cleanup EXIT

create_ns() {
	local ns

	for ns in $NS_SRC $NS_DST; do
		ip netns add $ns
		ip -n $ns link set dev lo up
	done

	ip link add name veth$SRC type veth peer name veth$DST

	for ns in $SRC $DST; do
		ip link set dev veth$ns netns $BASE$ns up
		ip -n $BASE$ns addr add dev veth$ns $BM_NET_V4$ns/24
		ip -n $BASE$ns addr add dev veth$ns $BM_NET_V6$ns/64 nodad
	done
	echo "#kernel" > $BASE
	chmod go-rw $BASE
}

__chk_flag() {
	local msg="$1"
	local target=$2
	local expected=$3
	local flagname=$4

	local flag=`ip netns exec $BASE$target ethtool -k veth$target |\
		    grep $flagname | awk '{print $2}'`

	printf "%-60s" "$msg"
	if [ "$flag" = "$expected" ]; then
		echo " ok "
	else
		echo " fail - expected $expected found $flag"
		ret=1
	fi
}

chk_gro_flag() {
	__chk_flag "$1" $2 $3 generic-receive-offload
}

chk_tso_flag() {
	__chk_flag "$1" $2 $3 tcp-segmentation-offload
}

chk_channels() {
	local msg="$1"
	local target=$2
	local rx=$3
	local tx=$4

	local dev=veth$target
	local combined=$tx
	[ $rx -lt $tx ] && combined=$rx

	local cur_rx=`ip netns exec $BASE$target ethtool -l $dev |\
		grep RX: | tail -n 1 | awk '{print $2}' `
	local cur_tx=`ip netns exec $BASE$target ethtool -l $dev |\
		grep TX: | tail -n 1 | awk '{print $2}'`
	local cur_combined=`ip netns exec $BASE$target ethtool -l $dev |\
		grep Combined: | tail -n 1 | awk '{print $2}'`

	printf "%-60s" "$msg"
	if [ "$cur_rx" = "$rx" -a "$cur_tx" = "$tx" -a "$cur_combined" = "$combined" ]; then
		echo " ok "
	else
		echo " fail rx:$rx:$cur_rx tx:$tx:$cur_tx combined:$combined:$cur_combined"
	fi
}

chk_gro() {
	local msg="$1"
	local expected=$2

	ip netns exec $BASE$SRC ping -qc 1 $BM_NET_V4$DST >/dev/null
	NSTAT_HISTORY=$STATS ip netns exec $NS_DST nstat -n

	printf "%-60s" "$msg"
	ip netns exec $BASE$DST ./udpgso_bench_rx -C 1000 -R 10 &
	local spid=$!
	sleep 0.1

	ip netns exec $NS_SRC ./udpgso_bench_tx -4 -s 13000 -S 1300 -M 1 -D $BM_NET_V4$DST
	local retc=$?
	wait $spid
	local rets=$?
	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo " fail client exit code $retc, server $rets"
		ret=1
		return
	fi

	local pkts=`NSTAT_HISTORY=$STATS ip netns exec $NS_DST nstat IpInReceives | \
		    awk '{print $2}' | tail -n 1`
	if [ "$pkts" = "$expected" ]; then
		echo " ok "
	else
		echo " fail - got $pkts packets, expected $expected "
		ret=1
	fi
}

if [ ! -f ../bpf/xdp_dummy.o ]; then
	echo "Missing xdp_dummy helper. Build bpf selftest first"
	exit 1
fi

create_ns
chk_gro_flag "default - gro flag" $SRC off
chk_gro_flag "        - peer gro flag" $DST off
chk_tso_flag "        - tso flag" $SRC on
chk_tso_flag "        - peer tso flag" $DST on
chk_gro "        - aggregation" 1
ip netns exec $NS_SRC ethtool -K veth$SRC tx-udp-segmentation off
chk_gro "        - aggregation with TSO off" 10
cleanup

# reset default, just in case
echo 1 > /sys/module/veth/parameters/tx_queues
create_ns
ip netns exec $NS_DST ethtool -K veth$DST gro on
chk_gro_flag "with gro on - gro flag" $DST on
chk_gro_flag "        - peer gro flag" $SRC off
chk_tso_flag "        - tso flag" $SRC on
chk_tso_flag "        - peer tso flag" $DST on
ip netns exec $NS_SRC ethtool -K veth$SRC tx-udp-segmentation off
ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
chk_gro "        - aggregation with TSO off" 1
cleanup

create_ns
chk_channels "default channels" $DST 1 1

# will affect next veth device pair creation
echo 128 > /sys/module/veth/parameters/tx_queues

ip -n $NS_DST link set dev veth$DST down
ip netns exec $NS_DST ethtool -K veth$DST gro on
chk_gro_flag "with gro enabled on link down - gro flag" $DST on
chk_gro_flag "        - peer gro flag" $SRC off
chk_tso_flag "        - tso flag" $SRC on
chk_tso_flag "        - peer tso flag" $DST on
ip -n $NS_DST link set dev veth$DST up
ip netns exec $NS_SRC ethtool -K veth$SRC tx-udp-segmentation off
ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
chk_gro "        - aggregation with TSO off" 1
cleanup

create_ns

ip netns exec $NS_DST ethtool -L veth$DST tx 2
chk_channels "setting tx channels" $DST 128 2

ip netns exec $NS_DST ethtool -L veth$DST rx 3 tx 4
chk_channels "setting both rx and tx channels" $DST 3 4
ip netns exec $NS_DST ethtool -L veth$DST combined 2 2>/dev/null
chk_channels "bad setting: combined channels" $DST 3 4

ip netns exec $NS_DST ethtool -L veth$DST tx 1025 2>/dev/null
chk_channels "setting invalid channels nr" $DST 3 4

printf "%-60s" "bad setting: XDP with RX nr less than TX"
ip -n $NS_DST link set dev veth$DST xdp object ../bpf/xdp_dummy.o section xdp_dummy 2>/dev/null &&\
	echo "fail - set operation successful ?!?" || echo " ok "

# the following tests will run with multiple channels active
ip netns exec $NS_SRC ethtool -L veth$SRC tx 3

ip -n $NS_DST link set dev veth$DST xdp object ../bpf/xdp_dummy.o section xdp_dummy 2>/dev/null
printf "%-60s" "bad setting: reducing RX nr below peer TX with XDP set"
ip netns exec $NS_DST ethtool -L veth$DST rx 2 2>/dev/null &&\
	echo "fail - set operation successful ?!?" || echo " ok "
printf "%-60s" "bad setting: increasing peer TX nr above RX with XDP set"
ip netns exec $NS_SRC ethtool -L veth$SRC tx 4 2>/dev/null &&\
	echo "fail - set operation successful ?!?" || echo " ok "

chk_channels "setting invalid channels nr" $DST 3 4

chk_gro_flag "with xdp attached - gro flag" $DST on
chk_gro_flag "        - peer gro flag" $SRC off
chk_tso_flag "        - tso flag" $SRC off
chk_tso_flag "        - peer tso flag" $DST on
ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
chk_gro "        - aggregation" 1


ip -n $NS_DST link set dev veth$DST down
ip -n $NS_SRC link set dev veth$SRC down
chk_gro_flag "        - after dev off, flag" $DST on
chk_gro_flag "        - peer flag" $SRC off

ip netns exec $NS_DST ethtool -K veth$DST gro on
ip -n $NS_DST link set dev veth$DST xdp off
chk_gro_flag "        - after gro on xdp off, gro flag" $DST on
chk_gro_flag "        - peer gro flag" $SRC off
chk_tso_flag "        - tso flag" $SRC on
chk_tso_flag "        - peer tso flag" $DST on

ip netns exec $NS_DST ethtool -L veth$DST tx 5
chk_channels "setting tx channels with device down" $DST 3 4

ip -n $NS_DST link set dev veth$DST up
ip -n $NS_SRC link set dev veth$SRC up
chk_channels "[takes effect after link up]" $DST 3 5
chk_gro "        - aggregation" 1

ip netns exec $NS_DST ethtool -K veth$DST gro off
ip netns exec $NS_SRC ethtool -K veth$SRC tx-udp-segmentation off
chk_gro "aggregation again with default and TSO off" 10

exit $ret
