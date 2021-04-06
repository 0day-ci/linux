.. SPDX-License-Identifier: GPL-2.0

======================================
HiSilicon PCIe Tune and Trace device
======================================

Introduction
============

HiSilicon PCIe tune and trace device (PTT) is a PCIe Root Complex
integrated Endpoint (RCiEP) device, providing the capability
to dynamically monitor and tune the PCIe link's events (tune),
and trace the TLP headers (trace). The two functions are independent,
but is recommended to use them together to analyze and enhance the
PCIe link's performance.

On Kunpeng 930 SoC, the PCIe root complex is composed of several
PCIe cores.
Each core is composed of several root ports, RCiEPs, and one
PTT device, like below. The PTT device is capable of tuning and
tracing the link of the PCIe core.
::
          +--------------Core 0-------+
          |       |       [   PTT   ] |
          |       |       [Root Port]---[Endpoint]
          |       |       [Root Port]---[Endpoint]
          |       |       [Root Port]---[Endpoint]
    Root Complex  |------Core 1-------+
          |       |       [   PTT   ] |
          |       |       [Root Port]---[ Switch ]---[Endpoint]
          |       |       [Root Port]---[Endpoint] `-[Endpoint]
          |       |       [Root Port]---[Endpoint]
          +---------------------------+

The PTT device driver cannot be loaded if debugfs is not mounted.
Each PTT device will be presented under /sys/kernel/debugfs/hisi_ptt
as its root directory, with name of its BDF number.
::

    /sys/kernel/debug/hisi_ptt/<domain>:<bus>:<device>.<function>

Tune
====

PTT tune is designed for monitoring and adjusting PCIe link parameters(events).
Currently we support events in 4 classes. The scope of the events
covers the PCIe core with which the PTT device belongs to.

Each event is presented as a file under $(PTT root dir)/$(BDF)/tune, and
mostly a simple open/read/write/close cycle will be used to tune
the event.
::
    $ cd /sys/kernel/debug/hisi_ptt/$(BDF)/tune
    $ ls
    qos_tx_cpl    qos_tx_np    qos_tx_p
    tx_path_rx_req_alloc_buf_level
    tx_path_tx_req_alloc_buf_level
    $ cat qos_tx_dp
    1
    $ echo 2 > qos_tx_dp
    $ cat qos_tx_dp
    2

Current value(numerical value) of the event can be simply read
from the file, and the desired value written to the file to tune.
Tuning multiple events at the same time is not permitted, which means
you cannot read or write more than one tune file at one time.

1. Tx path QoS control
------------------------

Following files are provided to tune the QoS of the tx path of the PCIe core.

- qos_tx_cpl: weight of tx completion TLPs
- qos_tx_np: weight of tx non-posted TLPs
- qos_tx_p: weight of tx posted TLPs

The weight influences the proportion of certain packets on the PCIe link.
For example, for the storage scenario, increase the proportion
of the completion packets on the link to enhance the performance as
more completions are consumed.

The available tune data of these events is [0, 1, 2].
Writing a negative value will return an error, and out of range
values will be converted to 2. Note that the event value just
indicates a probable level, but is not precise.

2. Tx path buffer control
-------------------------

Following files are provided to tune the buffer of tx path of the PCIe core.

- tx_path_rx_req_alloc_buf_level: watermark of RX requested
- tx_path_tx_req_alloc_buf_level: watermark of TX requested

These events influence the watermark of the buffer allocated for each
type. RX means the inbound while Tx means outbound. For a busy
direction, you should increase the related buffer watermark to enhance
the performance.

The available tune data of above events is [0, 1, 2].
Writing a negative value will return an error, and out of range
values will be converted to 2. Note that the event value just
indicates a probable level, but is not precise.

Trace
=====

PTT trace is designed for dumping the TLP headers to the memory, which
can be used to analyze the transactions and usage condition of the PCIe
Link. You can chose to filter the traced headers by either requester ID,
or those downstream of a set of root ports on the same core of the PTT
device. It's also support to trace the headers of certain type and of
certain direction.

In order to start trace, you need to configure the parameters first.
The parameters files is provided under $(PTT root dir)/$(BDF)/trace.
::
    $ cd /sys/kernel/debug/hisi_ptt/$(BDF)/trace
    $ ls
    free_buffer     filter      buflet_nums     buflet_size
    direction       type        data            trace_on
    data_format

1. filter
---------

You can configure the filter of TLP headers through the file. The filter
is provided as BDF numbers of either root port or subordinates, which
belong to the same PCIe core. You can get the filters available and
currently configured by read the file, and write the desired BDF to the
file to set the filters. There is no default filter, which means you
must specifiy at least one filter before start tracing.
Write invalid BDF(not in the available list) will return
a failure.
::
    $ echo 0000:80:04.0 > filter
    $ cat filter
    #### Root Ports ####
    0000:80:00.0
    [0000:80:04.0]
    #### Functions ####
    0000:81:00.0
    0000:81:00.1
    0000:82:00.0

Note that multiple root ports can be specified at one time, but only
one Endpoint function can be specified in one trace.
Specifying both root port and function at the same time is not supported.

If no filter is available, read the filter will get the hint.
::
    $ cat filter
    #### No available filter ####

The filter can be dynamically updated, which means you can always
get correct filter information when hotplug events happens, or
manually remove/rescan the devices.

2. type
-------

You can trace the TLP headers of certain types by configure the file.
Read the file will get available types and current setting, and write
the desired type to the file to configure. The default type is
`posted_request` and write types not in the available list will return
a failure.
::
    $ echo completion > type
    $ cat type
    all  posted_request  non-posted_request  [completion]

3. direction
------------

You can trace the TLP headers from certain direction, which is relative
to the root port or the PCIe core. Read the file to get available
directions and current configurition, and write the desired direction
to configure. The default value is `rx` and any invalid direction will
return a failure. Note `rxtx_no_dma_p2p` means the headers of both
directions, but not include P2P DMA access.
::
    $ echo rxtx > direction
    $ cat direction
    rx  tx  [rxtx]  rxtx_no_dma_p2p

4. buflet_size
--------------

The traced TLP headers will be written to the memory allocated
by the driver. The hardware accept 4 DMA address with same size,
and write the buflet sequentially like below. If DMA addr 3 is
finished and the trace is still on, it will return to addr 0.
Driver will allocated each DMA buffer (we call it buflet).
The finished buflet will be replaced with a new one, so
a long time trace can be achieved.
::
    +->[DMA addr 0]->[DMA addr 1]->[DMA addr 2]->[DMA addr 3]-+
    +---------------------------------------------------------+

You should both configure the buflet_size and buflet_nums to
configure the `trace buffer` to receive the TLP headers. The
total trace buffer size is buflet_size * buflet_nums. Note
that the trace buffer will not be allocated immediately after you
configure the parameters, but will be allocated right before
the trace starts.

This file configures the buflet size. Read the file will get
available buflet size and size set currently, write the desired
size to the file to configure. The default size is 2 MiB and any
invalid size written will return a failure.
::
    $ cat buflet_size
    [2 MiB]     4 MiB
    $ echo 4 > buflet_size
    $ cat buflet_size
    2 MiB     [4 MiB]

5. buflet_nums
--------------

You can write the desired buflet count to the file to configure,
and read the file to get current buflet count. The default
value is 64. And any positive value is valid. Note that big value
may lead to DMA memory allocation failure, and you will not be
able to start tracing. If it happens, you should consider adjusting
buflet_nums or buflet_size.
::
    $ cat buflet_nums
    64
    $ echo 128 > buflet_nums
    $ cat buflet_nums
    128

6. data
-------

The file to access the traced data. You can read the file to get the
binary blob of traced TLP headers. The format of the headers is
4 Dword length and is just as defined by the PCIe Spec r4.0,
Sec 2.2.4.1, or 8 Dword length with additional 4 Dword extra
information.

echo "" > data will free all the trace buffers allocated as well as
the traced datas.

7. trace_on
-----------

Start or end the trace by simple writing to the file, and monitor the
trace status by reading the file.
::
    $ echo 1 > trace_on     # start trace
    $ cat trace_on          # get the trace status
    1
    $ echo 0 > trace_on     # manually end trace

The read value of the trace_on will be auto cleared if the buffer
allocated is full. 1 indicates the trace is running and 0 for
stopped. Write any non-zero value to the file can start trace.

8. free_buffer
--------------

File to indicate the trace buffer status and to manually free the
trace buffer. The read value of 1 indicates the trace buffer has
been allocated and exists in the memory, while 0 indicates there
is no buffer allocated. Write 1 to the file to free the trace
buffer as well as the traced datas.
::
    $ cat free_buffer
    1                       # indicate the buffer exists
    $ echo 1 > free_buffer  # free the trace buffer
    $ cat free_buffer
    0

9. data_format
--------------

File to indicate the format of the traced TLP headers. User can also
specify the desired format of traced TLP headers. Available formats
are 4DW, 8DW which indicates the length of each TLP headers traced.
::
    $ cat data_format
    [4DW]    8DW
    $ echo 8 > data_format
    $ cat data_format
    4DW     [8DW]

The traced TLP header format is different from the PCIe standard.
4DW format is like
::
    bits [31:30] [ 29:25 ][24][23][22][21][    20:11   ][    10:0    ]
         |-----|---------|---|---|---|---|-------------|-------------|
     DW0 [ Fmt ][  Type  ][T9][T8][TH][SO][   Length   ][    Time    ]
     DW1 [                     Header DW1                            ]
     DW2 [                     Header DW2                            ]
     DW3 [                     Header DW3                            ]

For 8DW format, the bit[31:11] of DW0 is always 0x1fffff, which can be
used to distinguish the data format. 8DW format is like
::
    bits [                 31:11                 ][       10:0       ]
         |---------------------------------------|-------------------|
     DW0 [                0x1fffff               ][ Reserved (0x7ff) ]
     DW1 [                       Prefix                              ]
     DW2 [                     Header DW0                            ]
     DW3 [                     Header DW1                            ]
     DW4 [                     Header DW2                            ]
     DW5 [                     Header DW3                            ]
     DW6 [                   Reserved (0x0)                          ]
     DW7 [                        Time                               ]

All the fields of the traced TLP header is defined by the PCIe Specification.
While 'Header DWx' means standard TLP header DWord x, and 'Time' is the
timestamp of the traced header.
