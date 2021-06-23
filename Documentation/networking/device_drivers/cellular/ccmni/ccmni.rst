.. SPDX-License-Identifier: GPL-2.0+

===================================================================
Linux kernel driver for Cross Core Multi Network Interface (ccmni):
===================================================================


1. Introduction
===============

ccmni driver is built on the top of AP-CCCI (Application Processor Cross
Core Communite Interface) to emulate the modem’s data networking service
as a network interface card. This driver is used by all recent chipsets
using MediaTek Inc. modems.

This driver can support multiple tx/rx channels, each channel has its
own IP address, and ipv4 and ipv6 link-local addresses on the interface
is set through ioctl, which means that ccmni driver is a pure ip device
and it does not need the kernel to generate these ip addresses
automatically.

Creating logical network devices (ccmni devices) can be used to handle
multiple private data networks (PDNs), such as a defaule cellular
internet, IP Multimedia Subsystem (IMS) network (VoWiFi/VoLTE), IMS
Emergency, Tethering, Multimedia Messaging Service (MMS), and so on.
In general, one ccmni device corresponds to one PDN.

ccmni design
------------

- AT commands and responses between the Application Processor and Modem
  Processor take place over the ccci tty serial port emulation.
- IP packets coming into the ccmni driver (from the Modem) needs to be
  un-framed (via the Packet Framing Protocol) before it can be stuffed
  into the Socket Buffer.
- IP Packets going out of the ccmni driver (to the Modem) needs to be
  framed (via the Packet Framing Protocol) before it can be pushed into
  the respective ccci tx buffer.
- The Packet Framing Protocol module contains algorithms to correctly
  add or remove frames, going out to and coming into, the ccmni driver
  respectively.
- Packets are passed to, and received from the Linux networking system,
  via Socket Buffers.
- The Android Application Framework contains codes to setup the ccmni’s
  parameters (such as netmask), and the routing table.


2. Architecture
===============

         +------------------------+   +---------------------+
user     |        MTK RilD        |   |   network process   |
space    | (config/up/down ccmni) |   | (send/recv packets) |
         +------------------------+   +---------------------+
+--------------------------------------------------------------------+
         +--------------------------------------------------+
         |                      socket                      |
         +--------------------------------------------------+
                                      +---------------------+
                                      |     TCP/IP stack    |
                                      +---------------------+
         +--------------------------------------------------+
	 |                net device layer                  |
         +--------------------------------------------------+
                +--------+  +--------+     +--------+
kernel          | ccmni0 |  | ccmni1 | ... | ccmnix |
space           +--------+  +--------+     +--------+
         +-------------------------------------------------+
         |                   ccmni driver                  |
         +-------------------------------------------------+
         +-------------------------------------------------+
         |                    AP-CCCI                      |
         +-------------------------------------------------+
+---------------------------------------------------------------------+
         +-------------------------------------------------+
         |  +-------------------+   +-------------------+  |
         |  |  DPMAIF hardware  |   |      MD-CCCI      |  |
         |  +-------------------+   +-------------------+  |
         |                     ... ...                     |
         | Modem Processor                                 |
         +-------------------------------------------------+


3. Driver information and notes
===============================

Data Connection Set Up
----------------------

The data framework will first SetupDataCall with passing ccmni index,
and then RilD will activate PDN connection and get CID (Connection ID).

Next, RilD will creat ccmni socket to use ioctl to configure ccmni up,
and then ccmni_open() will be called.

In addition, since ccmni is a pureip device, RilD needs to use ioctl to
configure the ipv4/ipv6-link-local address for ccmni after it is up.

Alternatively, you can use the ip command as follows::
    ip link set up dev ccmni<x>
    ip addr add a.b.c.d dev ccmni<x>
    ip -6 addr add fe80::1/64 dev ccmni<x>

Data Connection Set Down
------------------------

The data service implements a method to tear down the data connection,
after RilD deactivate the PDN connection, RilD will down the specific
interface of ccmnix through ioctl SIOCSIFFLAGS, and then ccmni_close()
will be called. After that, if any network process (such as browser) wants
to write data to ccmni socket, TCP/IP stack will return an error to
this socket.

Data Transmit
-------------

In the uplink direction, when there is data to be transmitted to the cellular
network, ccmni_start_xmit() will be called by the Linux networking system.

main operations in ccmni_start_xmit():
- the datagram to be transmitted is housed in the Socket Buffer.
- check if the datagram is within limits (i.e. 1500 bytes) acceptable by the
  Modem. If the datagram exceeds limit, the datagram will be dropped, and free
  the Socket Buffer.
- check if the AP-CCCI TX buffer is busy, or do not have enough space for this
  datagram. If it is busy, or the free space is too small, ccmni_start_xmit()
  return NETDEV_TX_BUSY and ask the Linux netdevice to stop the tx queue.

To handle outcoming datagrams to the Modem, ccmni register a callback function
for AP-CCCI driver. ccmni_hif_hook() means ccci can implement specific egress
function to send these packets to the specific hardware.

Data Receive
------------

In the downlink direction, DPMAIF (Data Path MD AP Interface) hardware sends
packets and messages with channel id matching these packets to AP-CCCI driver.

To handle incoming datagrams from the Modem, ccmni register a callback
function for the AP-CCCI driver. ccmni_rx_push() responsible for extracting
the incoming packets from the ccci rx buffer, and updating skb. Once ready,
ccmni signal to the Linux networking system to take out Socket Buffer
(via netif_rx() / netif_rx_ni()).


Support
=======

If an issue is identified by published source code and supported adapter on
the supported kernel, please email the specific information about the issue
to rocco.yue@mediatek.com and chao.song@mediatek.com
