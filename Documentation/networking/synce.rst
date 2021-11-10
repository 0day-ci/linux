.. SPDX-License-Identifier: GPL-2.0

=============================
Synchronous Equipment Clocks
=============================

Synchronous Equipment Clocks use a physical layer clock to syntonize
the frequency across different network elements.

Basic Synchronous network node consist of a Synchronous Equipment
Clock (SEC) and and a PHY that has dedicated outputs of clocks recovered
from the Receive side and a dedicated TX clock input that is used as
a reference for the physical frequency of the transmit data to other nodes.

The PHY is able to recover the physical signal frequency of the RX data
stream on RX ports and redirect it (sometimes dividing it) to recovered
clock outputs. Number of recovered clock output pins is usually lower than
the number of RX portx. As a result the RX port to Recovered Clock output
mapping needs to be configured. the TX frequency is directly depends on the
input frequency - either on the PHY CLK input, or on a dedicated
TX clock input.

      ┌──────────┬──────────┐
      │ RX       │ TX       │
  1   │ ports    │ ports    │ 1
  ───►├─────┐    │          ├─────►
  2   │     │    │          │ 2
  ───►├───┐ │    │          ├─────►
  3   │   │ │    │          │ 3
  ───►├─┐ │ │    │          ├─────►
      │ ▼ ▼ ▼    │          │
      │ ──────   │          │
      │ \____/   │          │
      └──┼──┼────┴──────────┘
        1│ 2│        ▲
 RCLK out│  │        │ TX CLK in
         ▼  ▼        │
       ┌─────────────┴───┐
       │                 │
       │       SEC       │
       │                 │
       └─────────────────┘

The SEC can synchronize its frequency to one of the synchronization inputs
either clocks recovered on traffic interfaces or (in advanced deployments)
external frequency sources.

Some SEC implementations can automatically select synchronization source
through priority tables and synchronization status messaging and provide
necessary filtering and holdover capabilities.

The following interface can be applicable to diffferent packet network types
following ITU-T G.8261/G.8262 recommendations.

Interface
=========

The following RTNL messages are used to read/configure SyncE recovered
clocks.

RTM_GETRCLKSTATE
-----------------
Read the state of recovered pins that output recovered clock from
a given port. The message will contain the number of assigned clocks
(IFLA_RCLK_STATE_COUNT) and an N pin indexes in IFLA_RCLK_STATE_OUT_STATE
To support multiple recovered clock outputs from the same port, this message
will return the IFLA_RCLK_STATE_COUNT attribute containing the number of
recovered clock outputs (N) and N IFLA_RCLK_STATE_OUT_STATE attributes
listing the output indexes with the respective GET_RCLK_FLAGS_ENA flag.
This message will call the ndo_get_rclk_range to determine the allowed
recovered clock indexes and then will loop through them, calling
the ndo_get_rclk_state for each of them.


Attributes:
IFLA_RCLK_STATE_COUNT - Returns the number of recovered clock outputs
IFLA_RCLK_STATE_OUT_STATE - Returns the current state of a single recovered
			    clock output in the struct if_get_rclk_msg.
struct if_get_rclk_msg {
	__u32 out_idx; /* output index (from a valid range) */
	__u32 flags;   /* configuration flags */
};

Currently supported flags:
#define GET_RCLK_FLAGS_ENA	(1U << 0)


RTM_SETRCLKSTATE
-----------------
Sets the redirection of the recovered clock for a given pin. This message
expects one attribute:
struct if_set_rclk_msg {
	__u32 ifindex; /* interface index */
	__u32 out_idx; /* output index (from a valid range) */
	__u32 flags;   /* configuration flags */
};

Supported flags are:
SET_RCLK_FLAGS_ENA - if set in flags - the given output will be enabled,
		     if clear - the output will be disabled.

RTM_GETEECSTATE
----------------
Reads the state of the EEC or equivalent physical clock synchronizer.
This message returns the following attributes:
IFLA_EEC_STATE - current state of the EEC or equivalent clock generator.
		 The states returned in this attribute are aligned to the
		 ITU-T G.781 and are:
		  IF_EEC_STATE_INVALID - state is not valid
		  IF_EEC_STATE_FREERUN - clock is free-running
		  IF_EEC_STATE_LOCKED - clock is locked to the reference,
		                        but the holdover memory is not valid
		  IF_EEC_STATE_LOCKED_HO_ACQ - clock is locked to the reference
		                               and holdover memory is valid
		  IF_EEC_STATE_HOLDOVER - clock is in holdover mode
State is read from the netdev calling the:
int (*ndo_get_eec_state)(struct net_device *dev, enum if_eec_state *state,
			 u32 *src_idx, struct netlink_ext_ack *extack);

IFLA_EEC_SRC_IDX - optional attribute returning the index of the reference
		   that is used for the current IFLA_EEC_STATE, i.e.,
		   the index of the pin that the EEC is locked to.

Will be returned only if the ndo_get_eec_src is implemented.