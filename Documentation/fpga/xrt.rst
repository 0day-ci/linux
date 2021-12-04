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

XRTV2 drivers support *subsystem* style data driven platforms where driver's
configuration and behavior are determined by metadata provided by the platform
(in *device tree* format). Primary management physical function (MPF) driver
is called **xrt-mgmt**. Primary user physical function (UPF) driver is called
**xrt-user** and is under development. xrt_driver framework and FPGA subsystem
drivers are packaged into a library module called **xrt-lib**, which is shared
by **xrt-mgmt** and **xrt-user** (under development). The xrt_driver framework
implements a ``bus_type`` called **xrt_bus_type** which is used to discover HW
subsystems and facilitate inter HW subsystem interaction.

Driver Modules
==============

xrt-lib.ko
----------

xrt-lib is the repository of xrt drivers and pure software modules that can
potentially be shared between xrt-mgmt and xrt-user. All these drivers are
structured as **xrt_driver** and are instantiated by xrt-mgmt (or xrt-user under
development) based on the metadata associated with the hardware.  The metadata
is in the form of a device tree as mentioned before.

xrt-lib relies on OF kernel APIs to unflatten the metadata and overlay the
unflattened device tree nodes to system device tree. In xrt-lib module initialization
routine, "/xrt-bus" is created in system device tree, all XRT device
tree nodes and properties will be under "/xrt-bus".

The xrt-lib infrastructure provides hooks to xrt_drivers for device node
management, user file operations and ioctl callbacks. The core infrastructure also
provides a bus functionality called **xrt_bus_type** for xrt_driver registration,
discovery and inter xrt_driver calls. xrt-lib does not have any dependency on PCIe
subsystem.

xrt-mgmt.ko
------------

The xrt-mgmt driver is a PCIe device driver driving MPF found on Xilinx's Alveo
PCIe device. It creates one or more *group* device and one or more *xleaf* device.
The group and xleaf drivers are in xrt-lib and instantiations of the xrt_driver but
are called group and xleaf to symbolize the logical operation performed by them.

The xrt-mgmt driver uses xrt-lib APIs to manages the life cycle of multiple group
drivers, which, in turn, manages multiple xleaf drivers. This flexibility allows
xrt-mgmt.ko and xrt-lib.ko to support various HW subsystems exposed by different
Alveo shells. The differences among these Alveo shells is handled in xleaf drivers.
The group driver is part of the infrastructure which provides common services to xleaf
drivers found on various Alveo shells. See :ref:`alveo_platform_overview`.

The instantiation of specific group driver or xleaf drivers is completely data
driven based on metadata (mostly in device tree format) found through VSEC
capability and inside the firmware files, such as platform xsabin or user xclbin
file.


Driver Object Model
===================

The driver object model looks like the following::

                              +-----------+
                              |  of root  |
                              +-----+-----+
                                    |
                          +---------+---------+
                          |                   |
                    +-----------+        +----------+
                    |  xrt-bus  |        |   ...    |
                    +-----+-----+        +----------+
                          |
              +-----------+-----------+
              |                       |
              v                       v
        +-----------+          +-----------+
        |   group   |    ...   |   group   |
        +-----+-----+          +------+----+
              |                       |
              |                       |
        +-----+----+            +-----+----+
        |          |            |          |
        v          v            v          v
    +-------+  +-------+    +-------+  +-------+
    | xleaf |..| xleaf |    | xleaf |..| xleaf |
    +-------+  +-------+    +-------+  +-------+

As an example, for Xilinx Alveo U50 before user xclbin download, the tree
looks like the following::

                                +-----------+
                                |  xrt-bus  |
                                +-----+-----+
                                      |
            +-------------------------+--------------------+
            |                         |                    |
            v                         v                    v
       +--------+                +--------+            +--------+
       | group0 |                | group1 |            | group2 |
       +----+---+                +----+---+            +---+----+
            |                         |                    |
            |                         |                    |
      +-----+-----+        +----+-----+---+    +-----+-----+----+--------+
      |           |        |    |         |    |     |          |        |
      v           v        |    v         v    |     v          v        |
 +------------+  +------+  | +------+ +------+ |  +------+ +-----------+ |
 | xmgmt_main |  | VSEC |  | | GPIO | | QSPI | |  |  CMC | | AXI-GATE0 | |
 +------------+  +------+  | +------+ +------+ |  +------+ +-----------+ |
                           | +---------+       |  +------+ +-----------+ |
                           +>| MAILBOX |       +->| ICAP | | AXI-GATE1 |<+
                             +---------+       |  +------+ +-----------+
                                               |  +-------+
                                               +->| CALIB |
                                                  +-------+

After a xclbin is downloaded, group3 will be added and the tree looks like the
following::

                                +-----------+
                                |  xrt-bus  |
                                +-----+-----+
                                      |
            +-------------------------+--------------------+-----------------+
            |                         |                    |                 |
            v                         v                    v                 |
       +--------+                +--------+            +--------+            |
       | group0 |                | group1 |            | group2 |            |
       +----+---+                +----+---+            +---+----+            |
            |                         |                    |                 |
            |                         |                    |                 |
      +-----+-----+       +-----+-----+---+    +-----+-----+----+--------+   |
      |           |       |     |         |    |     |          |        |   |
      v           v       |     v         v    |     v          v        |   |
 +------------+  +------+ | +------+ +------+  |  +------+ +-----------+ |   |
 | xmgmt_main |  | VSEC | | | GPIO | | QSPI |  |  |  CMC | | AXI-GATE0 | |   |
 +------------+  +------+ | +------+ +------+  |  +------+ +-----------+ |   |
                          | +---------+        |  +------+ +-----------+ |   |
                          +>| MAILBOX |        +->| ICAP | | AXI-GATE1 |<+   |
                            +---------+        |  +------+ +-----------+     |
                                               |  +-------+                  |
                                               +->| CALIB |                  |
                                                  +-------+                  |
                      +---+----+                                             |
                      | group3 |<--------------------------------------------+
                      +--------+
                          |
                          |
     +-------+--------+---+--+--------+------+-------+
     |       |        |      |        |      |       |
     v       |        v      |        v      |       v
 +--------+  |   +--------+  |   +--------+  |    +-----+
 | CLOCK0 |  |   | CLOCK1 |  |   | CLOCK2 |  |    | UCS |
 +--------+  v   +--------+  v   +--------+  v    +-----+
 +-------------+ +-------------+ +-------------+
 | CLOCK-FREQ0 | | CLOCK-FREQ1 | | CLOCK-FREQ2 |
 +-------------+ +-------------+ +-------------+


group
-----

The group driver represents a pseudo device whose life cycle is managed by
root and does not have real IO mem or IRQ resources. It's part of the
infrastructure of the MPF driver and resides in xrt-lib.ko. This driver

* manages one or more xleaf drivers
* handle requests from xleaf drivers. For example event notifications and
  inter xleaf calls.

In xrt-mgmt, an initial group driver instance will be created by the PCIe driver.
This instance contains xleaf drivers that will trigger group instances to be
created to manage groups of xleaf drivers found on different partitions of
hardware, such as VSEC, Shell, and User.

xleaf
-----

The xleaf driver is a xrt_driver whose life cycle is managed by
a group driver and may or may not have real IO mem or IRQ resources. They
manage HW subsystems they are attached to.

A xleaf driver without real hardware resources manages in-memory states for
xrt-mgmt. These states are shareable by other xleaf drivers.

Xleaf drivers assigned to specific hardware resources drive a specific subsystem
in the device. To manipulate the subsystem or carry out a task, a xleaf driver
may ask for help from the root via root calls and/or from other leaves via
inter xleaf calls.

A xleaf can also broadcast events through infrastructure code for other leaves
to process. It can also receive event notification from infrastructure about
certain events, such as post-creation or pre-exit of a particular xleaf.

xrt_bus_type
------------

xrt_bus_type defines a virtual bus which handles xrt_driver probe, remove and match
operations. All xrt_drivers register with xrt_bus_type as part of xrt-lib driver
``module_init`` and un-register as part of xrt-lib driver ``module_exit``.

FPGA Manager Interaction
========================

fpga_manager
------------

An instance of fpga_manager is created by xmgmt_main and is used for xclbin
image download. fpga_manager requires the full xclbin image before it can
start programming the FPGA configuration engine via Internal Configuration
Access Port (ICAP) xrt_driver.

fpga_region
-----------

For every interface exposed by the currently loaded xclbin/xsabin in the
*parent* fpga_region a new instance of fpga_region is created like a *child*
fpga_region. The device tree of the *parent* fpga_region defines the
resources for a new instance of fpga_bridge which isolates the parent from
child fpga_region. This new instance of fpga_bridge will be used when a
xclbin image is loaded on the child fpga_region. After the xclbin image is
downloaded to the fpga_region, an instance of a group is created for the
fpga_region using the device tree obtained as part of the xclbin. If this
device tree defines any child interfaces, it can trigger the creation of
fpga_bridge and fpga_region for the next region in the chain.

fpga_bridge
-----------

Like the fpga_region, an fpga_bridge is created by walking the device tree
of the parent group. The bridge is used for isolation between a parent and
its child.

Driver Interfaces
=================

xrt-mgmt Driver Ioctls
----------------------

Ioctls exposed by the xrt-mgmt driver to user space are enumerated in the
following table:

== ===================== ============================ ==========================
#  Functionality         ioctl request code            data format
== ===================== ============================ ==========================
1  FPGA image download   XMGMT_IOCICAPDOWNLOAD_AXLF    xmgmt_ioc_bitstream_axlf
== ===================== ============================ ==========================

A user xclbin can be downloaded by using the xbmgmt tool from the XRT open source
suite. See example usage below::

  xbmgmt partition --program --path /lib/firmware/xilinx/862c7020a250293e32036f19956669e5/test/verify.xclbin --force

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
form of device tree. See :ref:`device_tree_usage` below for details and example.

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


.. _device_tree_usage:

Device Tree Usage
-----------------

The xsabin file stores metadata which advertise HW subsystems present in a
partition. The metadata is stored in device tree format with a well defined
schema. XRT management driver uses this information to create *xrt_devices* and
bind *xrt_drivers* to them. The xrt_drivers could be independent modules or
found in **xrt-lib.ko** kernel module.

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
