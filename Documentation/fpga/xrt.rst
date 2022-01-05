.. SPDX-License-Identifier: GPL-2.0

==================================
XRTV2 Linux Kernel Driver Overview
==================================

Authors:

* Sonal Santan <sonal.santan@xilinx.com>
* Max Zhen <max.zhen@xilinx.com>
* Lizhi Hou <lizhi.hou@xilinx.com>

XRTV2 drivers are second generation `XRT <https://github.com/Xilinx/XRT>`_
drivers which support `Alveo <https://www.xilinx.com/products/boards-and-kits/alveo.html>`_
PCIe platforms from Xilinx.

XRTV2 drivers support *subsystem* style data driven platforms where the driver's
configuration and behavior are determined by the metadata provided by the
platform (in *device tree* format). Primary management physical function (MPF)
driver is called **xrt-mgmt**. Primary user physical function (UPF) driver is
called **xrt-user** and is under development. xrt_driver common APIs are packaged
into a library module called **xrt-lib**, which is shared by **xrt-mgmt** and
**xrt-user** (under development).

Driver Modules
==============

xrt-lib.ko
----------

xrt-lib is the repository of functions that can potentially be shared between
xrt-mgmt and xrt-user.

Alveo platform consists of one or more FPGA partitions. Each partition has
multiple HW peripherals (also referred to as endpoints) and metadata to describe
the endpoints. This metadata is in flat device tree format. xrt-lib relies on OF
kernel APIs to un-flatten the metadata and overlay the un-flattened device tree
nodes to the system base device tree.

xrt-mgmt.ko
------------

The xrt-mgmt driver is a PCIe device driver driving MPF found on Xilinx's Alveo
PCIe device. It reads Alveo platform partition metadata and creates one or more
partitions based on the hardware design. xrt-lib APIs are called to overlay the
endpoint nodes to the system base tree. Eventually, platform devices are
generated for each endpoint defined in the partition metadata.

The xrt-mgmt driver uses xrt-lib APIs to manage the life cycle of partitions,
which, in turn, manages multiple endpoints (platform devices) generated during
partition creation. This flexibility allows xrt-mgmt.ko and xrt-lib.ko to support
various HW subsystems exposed by different Alveo shells. The differences among
these Alveo shells is handled in the endpoint (platform device) drivers.
See :ref:`alveo_platform_overview`.

The instantiation of a specific endpoint driver is completely data driven based
on the metadata (in the device tree format). The flattened device tree is stored
in a xsabin file which is discovered through the PCIe VSEC capability.


Driver Object Model
===================

The system device tree after overlaying Alveo partitions looks like the
following::

                            +-----------+
                            |  of root  |
                            +-----------+
                                  |
              +-------------------+-------------------+
              |                   |                   |
              v                   v                   v
      +-------------+      +------------+        +---------+
      | xrt-part0   |      | xrt-partN  |        |         |
      |(simple-bus) |  ... |(simple-bus)|        |   ...   |
      +-------------+      +------------+        +---------+
              |                   |
              |                   |
        +-----+--------+          |
        |              |          |
        v              v          v
  +-----------+  +-----------+  +------------+
  |ep_foo@123 |..|ep_bar@456 |  | ep_foo@789 |
  +-----------+  +-----------+  +------------+

partition node
--------------

The partition node is created and added to the system device tree when the driver
creates a new partition. It is compatible with ``simple-bus`` which is a
transparent bus node defined by Linux kernel. The partition node is used for
translating the address of underneath endpoint to CPU address.

endpoint node
-------------

During the partition creation, xrt driver un-flattens the partition metadata and
adds all the endpoint nodes under the partition node to the system device tree.
Eventually, all the endpoint nodes will be populated by the existing platform
device and OF infrastructure. This means a platform device will be created for
each endpoint node. The platform driver will be bound based on the ``compatible``
property defined in the endpoint node.

.. _alveo_platform_overview:

Alveo Platform Overview
=======================

Alveo platforms are architected as two physical FPGA partitions: *Shell* and
*User*. The Shell provides basic infrastructure for the Alveo platform like
PCIe connectivity, board management, Dynamic Function Exchange (DFX), sensors,
clocking, reset, and security. DFX, partial reconfiguration, is responsible for
loading the user compiled FPGA binary.

For DFX to work properly, physical partitions require strict HW compatibility
with each other. Every physical partition has two interface UUIDs: the *parent*
UUID and the *child* UUID. For simple single stage platforms, Shell → User forms
the parent child relationship.

.. note::
   Partition compatibility matching is a key design component of the Alveo platforms
   and XRT. Partitions have child and parent relationship. A loaded partition
   exposes child partition UUID to advertise its compatibility requirement. When
   loading a child partition, the xrt-mgmt driver matches the parent
   UUID of the child partition against the child UUID exported by the parent.
   The parent and child partition UUIDs are stored in the *xclbin* (for the user)
   and the *xsabin* (for the shell). Except for the root UUID exported by VSEC,
   the hardware itself does not know about the UUIDs. The UUIDs are stored in
   xsabin and xclbin. The image format has a special node called Partition UUIDs
   which define the compatibility UUIDs.


The physical partitions and their loading are illustrated below::

           SHELL                               USER
        +-----------+                  +-------------------+
        |           |                  |                   |
        | VSEC UUID | CHILD     PARENT |    LOGIC UUID     |
        |           o------->|<--------o                   |
        |           | UUID       UUID  |                   |
        +-----+-----+                  +--------+----------+
              |                                 |
              .                                 .
              |                                 |
          +---+---+                      +------+--------+
          |  POR  |                      | USER COMPILED |
          | FLASH |                      |    XCLBIN     |
          +-------+                      +---------------+


Loading Sequence
----------------

The Shell partition is loaded from flash at system boot time. It establishes the
PCIe link and exposes two physical functions to the BIOS. After the OS boots,
the xrt-mgmt driver attaches to the PCIe physical function 0 exposed by the Shell
and then looks for VSEC in the PCIe extended configuration space. Using VSEC, it
determines the logic UUID of the Shell and uses the UUID to load matching *xsabin*
file from Linux firmware directory. The xsabin file contains the metadata to
discover the peripherals that are part of the Shell and the firmware for any
embedded soft processors in the Shell. The xsabin file also contains Partition
UUIDs.

The Shell exports a child interface UUID which is used for the compatibility
check when loading the user compiled xclbin over the User partition as part of DFX.
When a user requests loading of a specific xclbin, the xrt-mgmt driver reads
the parent interface UUID specified in the xclbin and matches it with the child
interface UUID exported by the Shell to determine if the xclbin is compatible with
the Shell. If the match fails, loading of xclbin is denied.

xclbin loading is requested using the ICAP_DOWNLOAD_AXLF ioctl command. When loading
a xclbin, the xrt-mgmt driver performs the following *logical* operations:

1. Copy xclbin from user to kernel memory
2. Sanity check the xclbin contents
3. Isolate the User partition
4. Download the bitstream using the FPGA config engine (ICAP)
5. De-isolate the User partition
6. Program the clocks (ClockWiz) driving the User partition
7. Wait for the memory controller (MIG) calibration
8. Return the loading status back to the caller

`Platform Loading Overview <https://xilinx.github.io/XRT/master/html/platforms_partitions.html>`_
provides more detailed information on platform loading.


xsabin
------

Each Alveo platform comes packaged with its own xsabin. The xsabin is a trusted
component of the platform. For format details refer to :ref:`xsabin_xclbin_container_format`
below. xsabin contains basic information like UUIDs, platform name and metadata in the
form of flat device tree.

xclbin
------

xclbin is compiled by end user using
`Vitis <https://www.xilinx.com/products/design-tools/vitis/vitis-platform.html>`_
tool set from Xilinx. The xclbin contains sections describing user compiled
acceleration engines/kernels, memory subsystems, clocking information etc. It also
contains an FPGA bitstream for the user partition, UUIDs, platform name, etc.


.. _xsabin_xclbin_container_format:

xsabin/xclbin Container Format
------------------------------

xclbin/xsabin is ELF-like binary container format. It is structured as series of
sections. There is a file header followed by several section headers which is
followed by sections. A section header points to an actual section. There is an
optional signature at the end. The format is defined by the header file ``xclbin.h``.
The following figure illustrates a typical xclbin::


           +---------------------+
           |                     |
           |       HEADER        |
           +---------------------+
           |   SECTION  HEADER   |
           |                     |
           +---------------------+
           |         ...         |
           |                     |
           +---------------------+
           |   SECTION  HEADER   |
           |                     |
           +---------------------+
           |       SECTION       |
           |                     |
           +---------------------+
           |         ...         |
           |                     |
           +---------------------+
           |       SECTION       |
           |                     |
           +---------------------+
           |      SIGNATURE      |
           |      (OPTIONAL)     |
           +---------------------+


xclbin/xsabin files can be packaged, un-packaged and inspected using an XRT
utility called **xclbinutil**. xclbinutil is part of the XRT open source
software stack. The source code for xclbinutil can be found at
https://github.com/Xilinx/XRT/tree/master/src/runtime_src/tools/xclbinutil

For example, to enumerate the contents of a xclbin/xsabin use the *--info* switch
as shown below::


  xclbinutil --info --input /opt/xilinx/firmware/u50/gen3x16-xdma/blp/test/bandwidth.xclbin
  xclbinutil --info --input /lib/firmware/xilinx/862c7020a250293e32036f19956669e5/partition.xsabin

Deployment Models
=================

Baremetal
---------

In bare-metal deployments, both MPF and UPF are visible and accessible. The
xrt-mgmt driver binds to MPF. The xrt-mgmt driver operations are privileged and
available to system administrator. The full stack is illustrated below::

                            HOST

               [XRT-MGMT]         [XRT-USER]
                    |                  |
                    |                  |
                 +-----+            +-----+
                 | MPF |            | UPF |
                 |     |            |     |
                 | PF0 |            | PF1 |
                 +--+--+            +--+--+
          ......... ^................. ^..........
                    |                  |
                    |   PCIe DEVICE    |
                    |                  |
                 +--+------------------+--+
                 |         SHELL          |
                 |                        |
                 +------------------------+
                 |         USER           |
                 |                        |
                 |                        |
                 |                        |
                 |                        |
                 +------------------------+



Virtualized
-----------

In virtualized deployments, the privileged MPF is assigned to the host but the
unprivileged UPF is assigned to a guest VM via PCIe pass-through. The xrt-mgmt
driver in host binds to MPF. The xrt-mgmt driver operations are privileged and
only accessible to the MPF. The full stack is illustrated below::


                                 ..............
                  HOST           .    VM      .
                                 .            .
               [XRT-MGMT]        . [XRT-USER] .
                    |            .     |      .
                    |            .     |      .
                 +-----+         .  +-----+   .
                 | MPF |         .  | UPF |   .
                 |     |         .  |     |   .
                 | PF0 |         .  | PF1 |   .
                 +--+--+         .  +--+--+   .
          ......... ^................. ^..........
                    |                  |
                    |   PCIe DEVICE    |
                    |                  |
                 +--+------------------+--+
                 |         SHELL          |
                 |                        |
                 +------------------------+
                 |         USER           |
                 |                        |
                 |                        |
                 |                        |
                 |                        |
                 +------------------------+





Platform Security Considerations
================================

`Security of Alveo Platform <https://xilinx.github.io/XRT/master/html/security.html>`_
discusses the deployment options and security implications in great detail.
