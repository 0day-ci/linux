.. SPDX-License-Identifier: GPL-2.0-only

===========================================
Intel(R) Dynamic Load Balancer Overview
===========================================

:Authors: Gage Eads and Mike Ximing Chen

Contents
========

- Introduction
- Scheduling
- Queue Entry
- Port
- Queue
- Credits
- Scheduling Domain
- Interrupts
- Power Management
- User Interface
- Reset

Introduction
============

The Intel(r) Dynamic Load Balancer (Intel(r) DLB) is a PCIe device and hardware
accelerator that provides load-balanced, prioritized scheduling for event based
workloads across CPU cores. It can be used to save CPU resources in high
throughput pipelines by replacing software based distribution and synchronization
schemes. Using Intel DLB in place of software based methods has been
demonstrated to reduce CPU utilization up to 2-3 CPU cores per DLB device,
while also improving packet processing pipeline performance.

In many applications running on processors with a large number of cores,
workloads must be distributed (load-balanced) across a number of cores.
In packet processing applications, for example, streams of incoming packets can
exceed the capacity of any single core. So they have to be divided between
available worker cores. The workload can be split by either breaking the
processing flow into stages and places distinct stages on separate cores in a
daisy chain fashion (a pipeline), or spraying packets across multiple workers
that may be executing the same processing stage. Many systems employ a hybrid
approach whereby each packet encounters multiple pipelined stages with
distribution across multiple workers at each individual stage.

The following diagram shows a typical packet processing pipeline with the Intel DLB.

                              WC1              WC4
 +-----+   +----+   +---+  /      \  +---+  /      \  +---+   +----+   +-----+
 |NIC  |   |Rx  |   |DLB| /        \ |DLB| /        \ |DLB|   |Tx  |   |NIC  |
 |Ports|---|Core|---|   |-----WC2----|   |-----WC5----|   |---|Core|---|Ports|
 +-----+   -----+   +---+ \        / +---+ \        / +---+   +----+   ------+
                           \      /         \      /
                              WC3              WC6

WCs are the worker cores which process packets distributed by DLB. Without
hardware accelerators (such as DLB) the distribution and load balancing are normally
carried out by software running on CPU cores. Using Intel DLB in this case not only
saves the CPU resources but also improves the system performance.

The Intel DLB consists of queues and arbiters that connect producer cores (which
enqueues events to DLB) and consumer cores (which dequeue events from DLB). The
device implements load-balanced queueing features including:

- Lock-free multi-producer/multi-consumer operation.
- Multiple priority levels for varying traffic types.
- Direct traffic (i.e. multi-producer/single-consumer)
- Simple unordered load-balanced distribution.
- Atomic lock free load balancing across multiple consumers.
- Queue element reordering feature allowing ordered load-balanced distribution.

Note: this document uses 'DLB' when discussing the device hardware and 'dlb' when
discussing the driver implementation.

Following diagram illustrates the functional blocks of an Intel DLB device.

                                       +----+
                                       |    |
                        +----------+   |    |   +-------+
                       /|   IQ     |---|----|--/|       |
                      / +----------+   |    | / |  CP   |
                     /                 |    |/  +-------+
        +--------+  /                  |    |
        |        | /    +----------+   |   /|   +-------+
        |  PP    |------|   IQ     |---|----|---|       |
        +--------+ \    +----------+   | /  |   |  CP   |
                    \                  |/   |   +-------+
           ...       \     ...         |    |
        +--------+    \               /|    |   +-------+
        |        |     \+----------+ / |    |   |       |
        |  PP    |------|   IQ     |/--|----|---|  CP   |
        +--------+      +----------+   |    |   +-------+
                                       |    |
                                       +----+     ...
PP: Producer Port                        |
CP: Consumer Port                        |
IQ: Internal Queue                   DLB Scheduler


As shown in the diagram, the high-level Intel DLB data flow is as follows:
 - Software threads interact with the hardware by enqueuing and dequeuing Queue
   Elements (QEs).
 - QEs are sent through a Producer Port (PP) to the Intel DLB internal QE
   storage (internal queues), optionally being reordered along the way.
 - The Intel DLB schedules QEs from internal queues to a consumer according to
   a two-stage priority arbiter (DLB Scheduler).
 - Once scheduled, the Intel DLB writes the QE to a memory-based Consumer Port
   (CP), which the software thread reads and processes.


Scheduling Types
================

Intel DLB supports four types of scheduling of 'events' (i.e., queue elements),
where an event can represent any type of data (e.g. a network packet). The
first, `directed`, is multi-producer/single-consumer style scheduling. The
remaining three are multi-producer/multi-consumer, and support load-balancing
across the consumers.

- `Directed`: events are scheduled to a single consumer.

- `Unordered`: events are load-balanced across consumers without any ordering
                 guarantees.

- `Ordered`: events are load-balanced across consumers, and the consumer can
               re-enqueue its events so the device re-orders them into the
               original order. This scheduling type allows software to
               parallelize ordered event processing without the synchronization
               cost of re-ordering packets.

- `Atomic`: events are load-balanced across consumers, with the guarantee that
              events from a particular 'flow' are only scheduled to a single
              consumer at a time (but can migrate over time). This allows, for
              example, packet processing applications to parallelize while
              avoiding locks on per-flow data and maintaining ordering within a
              flow.

Intel DLB provides hierarchical priority scheduling, with eight priority
levels within each. Each consumer selects up to eight queues to receive events
from, and assigns a priority to each of these 'connected' queues. To schedule
an event to a consumer, the device selects the highest priority non-empty queue
of the (up to) eight connected queues. Within that queue, the device selects
the highest priority event available (selecting a lower priority event for
starvation avoidance 1% of the time, by default).

The device also supports four load-balanced scheduler classes of service. Each
class of service receives a (user-configurable) guaranteed percentage of the
scheduler bandwidth, and any unreserved bandwidth is divided evenly among the
four classes.

Queue Element
===========

Each event is contained in a queue element (QE), the fundamental unit of
communication through the device, which consists of 8B of data and 8B of
metadata, as depicted below.

QE structure format
::
    data     :64
    opaque   :16
    qid      :8
    sched    :2
    priority :3
    msg_type :3
    lock_id  :16
    rsvd     :8
    cmd      :8

The `data` field can be any type that fits within 8B (pointer, integer,
etc.); DLB merely copies this field from producer to consumer. The
`opaque` and `msg_type` fields behave the same way.

`qid` is set by the producer to specify to which DLB internal queue it wishes
to enqueue this QE. The ID spaces for load-balanced and directed queues are both
zero-based.

`sched` controls the scheduling type: atomic, unordered, ordered, or
directed. The first three scheduling types are only valid for load-balanced
queues, and the directed scheduling type is only valid for directed queues.
This field distinguishes whether `qid` is load-balanced or directed, since
their ID spaces overlap.

`priority` is the priority with which this QE should be scheduled.

`lock_id`, used for atomic scheduling and ignored for ordered and unordered
scheduling, identifies the atomic flow to which the QE belongs. When sending a
directed event, `lock_id` is simply copied like the `data`, `opaque`, and
`msg_type` fields.

`cmd` specifies the operation, such as:
- Enqueue a new QE
- Forward a QE that was dequeued
- Complete/terminate a QE that was dequeued
- Return one or more consumer queue tokens.
- Arm the port's consumer queue interrupt.

Port
====

A core's interface to the DLB is called a "port", and consists of an MMIO
region (producer port) through which the core enqueues a queue element, and an
in-memory queue (the "consumer queue" or consumer port) to which the device
schedules QEs. A core enqueues a QE to a device queue, then the device
schedules the event to a port. Software specifies the connection of queues
and ports; i.e. for each queue, to which ports the device is allowed to
schedule its events. The device uses a credit scheme to prevent overflow of
the on-device queue storage.

Applications interface directly with the device by mapping the port's memory
and MMIO regions into the application's address space for enqueue and dequeue
operations, but call into the kernel driver for configuration operations. An
application can be polling- or interrupt-driven; DLB supports both modes
of operation.

Internal Queue
==============

A DLB device supports an implementation specific and runtime discoverable
number of load-balanced (i.e. capable of atomic, ordered, and unordered
scheduling) and directed queues. Each internal queue supports a set of
priority levels.

A load-balanced queue is capable of scheduling its events to any combination
of load-balanced ports, whereas each directed queue can only haveone-to-one
mapping with any directed port. There is no restriction on port or queue types
when a port enqueues an event to a queue; that is, a load-balanced port can
enqueue to a directed queue and vice versa.

Credits
=======

The Intel DLB uses a credit scheme to prevent overflow of the on-device
queue storage, with separate credits for load-balanced and directed queues. A
port spends one credit when it enqueues a QE, and one credit is replenished
when a QE is dequeued from a consumer queue. Each scheduling domain has one pool
of load-balanced credits and one pool of directed credits; software is
responsible for managing the allocation and replenishment of these credits among
the scheduling domain's ports.

Scheduling Domain
=================

Device resources -- including ports, queues, and credits -- are contained
within a scheduling domain. Scheduling domains are isolated from one another; a
port can only enqueue to and dequeue from queues within its scheduling domain.

The scheduling domain with a set of resources is created through configfs, and
can be accessed/shared by multiple processes.

Consumer Queue Interrupts
=========================

Each port has its own interrupt which fires, if armed, when the consumer queue
depth becomes non-zero. Software arms an interrupt by enqueueing a special
'interrupt arm' command to the device through the port's MMIO window.

Power Management
================

The kernel driver keeps the device in D3Hot (power save mode) when not in use.
The driver transitions the device to D0 when the first device file is opened,
and keeps it there until there are no open device files or memory mappings.

User Interface
==============

The dlb driver uses configfs and sysfs as its primary user interfaces. While
the sysfs is used to configure and inquire device-wide operation and
resources, the configfs provides domain/queue/port level configuration and
resource management.

The dlb device level sysfs files are created during driver probe and is located
at /sys/class/dlb/dlb<N>/device, where N is the zero-based device ID. The
configfs directories/files can be created by user applications at
/sys/kernel/config/dlb/dlb<N> using 'mkdir'. For example, 'mkdir domain0' will
create a /domain0 directory and associated files in the configfs. Within the
domain directory, directories for queues and ports can be created. An example of
a DLB configfs structure is shown in the following diagram.

                              config
                                |
                               dlb
                                |
                        +------+------+------+---
                        |      |      |      |
                       dlb0   dlb1   dlb2   dlb3
                        |
                +-----------+--+--------+-------
                |           |           |
             domain0     domain1     domain2
                |
        +-------+-----+------------+---------------+------------+------------
        |             |            |               |            |
 num_ldb_queues     port0         port1   ...    queue0       queue1   ...
 num_ldb_ports        |                            |
 ...                is_ldb                   num_sequence_numbers
 create             cq_depth                 num_qid_inflights
 start              ...                      num_atomic_iflights
                    enable                   ...
                    create                   create


To create a domain/queue/port in DLB, an application can configure the resources
by writing to corresponding files, and then write '1' to the 'create' file to
trigger the action in the driver.

The driver also exports an mmap interface through port files, which are
acquired through port configfs. This mmap interface is used to map
a port's memory and MMIO window into the process's address space. Once the
ports are mapped, applications may use 64-byte direct-store instructions such
as movdir64b to enqueue the events for better performance.

Reset
=====

The dlb driver currently supports scheduling domain reset.

Scheduling domain reset occurs when an application stops using its domain.
Specifically, when no more file references or memory mappings exist. At this
time, the driver resets all the domain's resources (flushes its queues and
ports) and puts them in their respective available-resource lists for later
use.
