===================
Virtio-GPIO protocol specification
===================
...........
Specification for virtio-based GPIO devices
...........

+------------
+Version_ 1.0
+------------

General
===================

The virtio-gpio protocol provides access to general purpose IO devices via
virtio interfaces, used by many virtual machine monitors as well as hardware
fabrics. In VM setups, these GPIOs could be either provided by some simulator
(e.g. virtual HIL), routed to some external device or routed to real GPIOs on
the host (e.g. virtualized embedded applications).

Instead of simulating some existing real GPIO chip within an VMM, this
protocol provides a hardware independent interface between CPU and device
that solely relies on an active virtio connection (no matter which transport
actually used), no other buses or additional platform driver logic required.

At the same time, this protocol be implemented directly in virtio attached
hardware, FPGAs or tiny MCUs.

Protocol layout
===================

Configuration space
----------------------

+--------+----------+-------------------------------+
| Offset | Type     | Description                   |
+========+==========+===============================+
| 0x00   | u8       | version                       |
+--------+----------+-------------------------------+
| 0x02   | u16      | number of GPIO lines          |
+--------+----------+-------------------------------+
| 0x04   | u32      | size of gpio name block       |
+--------+----------+-------------------------------+
| 0x20   | char[32] | device name (0-terminated)    |
+--------+----------+-------------------------------+
| 0x40   | char[]   | line names block              |
+--------+----------+-------------------------------+

- for version field currently only value 1 supported.
- the line names block holds a stream of zero-terminated strings,
  containing the individual line names in ASCII. line names must unique.
- unspecified fields are reserved for future use and should be zero.

Virtqueues and messages:
------------------------

- Queue #0: transmission from device to CPU
- Queue #1: transmission from CPU to device

The queues transport messages of the struct virtio_gpio_msg:

Message format:
~~~~~~~~~~~~~~~

+--------+----------+---------------+
| Offset | Type     | Description   |
+========+==========+===============+
| 0x00   | uint16   | message type  |
+--------+----------+---------------+
| 0x02   | uint16   | line id       |
+--------+----------+---------------+
| 0x04   | uint32   | value         |
+--------+----------+---------------+

Message types:
~~~~~~~~~~~~~~

+---------+----------------------------------------+-----------------------------+
| Code    | Symbol                                 |                             |
+=========+========================================+=============================+
| 0x0001  | VIRTIO_GPIO_MSG_CPU_REQUEST            | request gpio line           |
+---------+----------------------------------------+-----------------------------+
| 0x0002  | VIRTIO_GPIO_MSG_CPU_DIRECTION_INPUT    | set direction to input      |
+---------+----------------------------------------+-----------------------------+
| 0x0003  | VIRTIO_GPIO_MSG_CPU_DIRECTION_OUTPUT   | set direction to output     |
+---------+----------------------------------------+-----------------------------+
| 0x0004  | VIRTIO_GPIO_MSG_CPU_GET_DIRECTION      | read current direction      |
+---------+----------------------------------------+-----------------------------+
| 0x0005  | VIRTIO_GPIO_MSG_CPU_GET_LEVEL          | read current level          |
+---------+----------------------------------------+-----------------------------+
| 0x0006  | VIRTIO_GPIO_MSG_CPU_SET_LEVEL          | set current (out) level     |
+---------+----------------------------------------+-----------------------------+
| 0x0011  | VIRTIO_GPIO_MSG_DEVICE_LEVEL           | state changed (device->CPU) |
+---------+----------------------------------------+-----------------------------+
| 0x8000  | VIRTIO_GPIO_MSG_REPLY                  | device reply mask           |
+---------+----------------------------------------+-----------------------------+

Data flow:
----------------------

- all operations, except ``VIRTIO_GPIO_MSG_DEVICE_LEVEL``, are initiated by CPU
- device replies with the orinal ``type`` value OR'ed with ``VIRTIO_GPIO_MSG_REPLY``
- ``VIRTIO_GPIO_MSG_DEVICE_LEVEL`` is only sent asynchronously from device to CPU
- in replies, a negative ``value`` field denotes an Unix-style / POSIX errno code
- valid direction values are:
  * 0 = output
  * 1 = input
- valid line state values are:
  * 0 = inactive
  * 1 = active

VIRTIO_GPIO_MSG_CPU_REQUEST
~~~~~~~~~~~~~~~~~~~~~~~~~~~

- notify the device that given line# is going to be used
- request:
  * ``line`` field: line number
  * ``value`` field: unused
- reply:
  * ``value`` field: errno code (0 = success)

VIRTIO_GPIO_MSG_CPU_DIRECTION_INPUT
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- set line line direction to input
- request:
  * ``line`` field: line number
  * ``value`` field: unused
- reply: value field holds errno
  * ``value`` field: errno code (0 = success)

VIRTIO_GPIO_MSG_CPU_DIRECTION_OUTPUT
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- set line direction to output and given line state
- request:
  * ``line`` field: line number
  * ``value`` field: output state (0=inactive, 1=active)
- reply:
  * ``value`` field: holds errno

VIRTIO_GPIO_MSG_CPU_GET_DIRECTION
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- retrieve line direction
- request:
  * ``line`` field: line number
  * ``value`` field: unused
- reply:
  * ``value`` field: direction (0=output, 1=input) or errno code

VIRTIO_GPIO_MSG_CPU_GET_LEVEL
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- retrieve line state value
- request:
  * ``line`` field: line number
  * ``value`` field: unused
- reply:
  * ``value`` field: line state (0=inactive, 1=active) or errno code

VIRTIO_GPIO_MSG_CPU_SET_LEVEL
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- set line state value (output only)
- request:
  * ``line`` field: line number
  * ``value`` field: line state (0=inactive, 1=active)
- reply:
  * ``value`` field: new line state or errno code

VIRTIO_GPIO_MSG_DEVICE_LEVEL
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- async notification from device to CPU: line state changed
- ``line`` field: line number
- ``value`` field: new line state (0=inactive, 1=active)

Request concurrency
===================

- CPU may send multiple request in serial, as long as the virtio queue
  is not exceeded
- device replies must be sent in the same order than the CPU requests
- CPU should process asynchronous messages from device as soon as possible,
  in order to avoid missing messages due to queue overrun

Future versions
===================

- future versions must increment the ``version`` value
- the basic data structures (config space, message format) should remain
  backwards compatible, but may increased in size or use reserved fields
- device needs to support commands in older versions
- CPU should not send commands of newer versions that the device doesn't support
