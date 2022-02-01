============================================
The Linux Hardware Timestamping Engine (HTE)
============================================

:Author: Dipen Patel

Introduction
------------

Certain devices have built in hardware timestamping engines which can
monitor sets of system signals, lines, buses etc... in realtime for state
change; upon detecting the change they can automatically store the timestamp at
the moment of occurrence. Such functionality may help achieve better accuracy
in obtaining timestamps than using software counterparts i.e. ktime and
friends.

This document describes the API that can be used by hardware timestamping
engine provider and consumer drivers that want to use the hardware timestamping
engine (HTE) framework. Both consumers and providers must include
#include <linux/hte.h>.

The HTE framework APIs for the providers
----------------------------------------

.. kernel-doc:: drivers/hte/hte.c
   :functions: devm_hte_register_chip hte_push_ts_ns

The HTE framework APIs for the consumers
----------------------------------------

.. kernel-doc:: drivers/hte/hte.c
   :functions: devm_of_hte_request_ts_ns hte_req_ts_by_linedata_ns hte_release_ts hte_enable_ts hte_disable_ts hte_get_clk_src_info

The HTE framework public structures
-----------------------------------
.. kernel-doc:: include/linux/hte.h

More on the HTE timestamp data
------------------------------
The struct hte_ts_data is used to pass timestamp details between the consumers
and the providers. It expresses timestamp data in nanoseconds in u64 data
type. For now all the HTE APIs using struct hte_ts_data require tsc to be in
nanoseconds. An example of the typical hte_ts_data data life cycle, for the
GPIO line is as follows::

 - Monitors GPIO line change.
 - Detects the state change on GPIO line.
 - Converts timestamps in nanoseconds and stores it in tsc.
 - Stores GPIO raw level in raw_level variable if the provider has that
 hardware capability.
 - Pushes this hte_ts_data object to HTE subsystem.
 - HTE subsystem increments seq counter and invokes consumer provided callback.
 Based on callback return value, the HTE starts a kernel thread and invokes
 secondary callback in the thread context.

HTE subsystem debugfs attributes
--------------------------------
HTE subsystem creates debugfs attributes at ``/sys/kernel/debug/hte/``.
It also creates line/signal-related debugfs attributes at
``/sys/kernel/debug/hte/<provider>/<label or line id>/``.

`ts_requested`
		The total number of entities requested from the given provider,
		where entity is specified by the provider and could represent
		lines, GPIO, chip signals, buses etc...
                The attribute will be available at
		``/sys/kernel/debug/hte/<provider>/``.

		Read-only value

`total_ts`
		The total number of entities supported by the provider.
                The attribute will be available at
		``/sys/kernel/debug/hte/<provider>/``.

		Read-only value

`dropped_timestamps`
		The dropped timestamps for a given line.
                The attribute will be available at
		``/sys/kernel/debug/hte/<provider>/<label or line id>/``.

		Read-only value
