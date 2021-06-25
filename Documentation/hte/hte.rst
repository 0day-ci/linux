============================================
The Linux Hardware Timestamping Engine (HTE)
============================================

:Author: Dipen Patel

Introduction
------------

The certain devices have the built in hardware timestamping engine which can
monitor sets of system signals, lines, buses etc... in realtime for the state
change; upon detecting the change it can automatically store the timestamp at
the moment of occurrence. Such functionality may help achieve better accuracy
in obtaining timestamp than using software counterparts i.e. ktime and friends.

This document describes the API that can be used by hardware timestamping
engine provider and consumer drivers that want to use the hardware timestamping
engine (HTE) framework.

The HTE framework APIs for the providers
----------------------------------------
Each driver must #include <linux/hte.h>. The ``linux/hte.h`` declares the
following functions for the provider:

.. c:function:: int hte_register_chip( struct hte_chip *chip )
		int hte_unregister_chip( struct hte_chip *chip )

	The provider uses these APIs to un/register itself with HTE framework.

.. c:function:: int hte_push_ts_ns_atomic( const struct hte_chip *chip, u32 xlated_id, struct hte_ts_data *data, size_t n )

	The provider pushes timestamp data in nano seconds unit using this API.

The detail about parameters and API usage are described in each functions
definitions in ``drivers/hte/hte.c`` file.

The HTE framework APIs for the consumers
----------------------------------------
The consumers use following APIs to control the line for the timestamp:

.. c:function:: int hte_release_ts( struct hte_ts_desc *desc )
		int devm_hte_release_ts( struct device *dev, struct hte_ts_desc *desc )

	The consumer uses API to release specified desc from timestamping.
	The API frees resources associated with the desc and disables the
	timestamping on it. The later is managed version of the same API.

.. c:function:: struct hte_ts_desc *of_hte_request_ts( struct device *dev, const char *label, void (*cb)(enum hte_notify n) )
		struct hte_ts_desc *devm_of_hte_request_ts( struct device *dev, const char *label, void (*cb)(enum hte_notify n) )

	The consumers can use above request APIs to request real timestamp
	capability on specified entity. The later is resource managed version
	of the of_hte_request_ts API. Both the APIs expect consumer to follow
	device tree bindings for the HTE consumer. The details about binding
	is in ``Documentation/devicetree/bindings/hte/hte-consumer.yaml``.

.. c:function:: struct hte_ts_desc *hte_req_ts_by_dt_node( struct device_node *of_node, unsigned int id, void (*cb)(enum hte_notify n) )

	The consumer can request timestamping directly specifying provider
	device tree node.

.. c:function:: int hte_enable_ts( struct hte_ts_desc *desc )
.. c:function:: int hte_disable_ts( struct hte_ts_desc *desc )

	The consumer can enable/disable timestamping on given desc.

.. c:function:: int hte_retrieve_ts_ns( const struct hte_ts_desc *desc, struct hte_ts_data *el, size_t n )
		int hte_retrieve_ts_ns_wait( const struct hte_ts_desc *desc, struct hte_ts_data *el, size_t n )

	The consumer uses above two API versions to get/retrieve timestamp data
	for the given desc. The later is blocking version.

.. c:function:: hte_get_clk_src_info(const struct hte_line_desc *desc, struct hte_clk_info *ci)

	The consumer retrieves clock source information that provider uses to
	timestamp entity in the structure hte_clk_info. This information
	specifies clock rate in HZ and clock.

The details on struct hte_clk_info
-----------------------------------
This structure presents detail of the hardware clock that provider uses for
realtime timestamping purposes. The consumer can use hte_get_clk_src_info API
to get the information in hte_clk_info structure. It has hz and type parameters
where hz represents clock rate in HZ and type is clock type of clockid_t and
of CLOCK_* family (for example, CLOCK_MONOTONIC).

The consumers calling of_hte_request_ts or hte_req_ts_by_dt_node APIs with
cb parameter set, usually will call hte_retrieve_ts (non blocking
version) after being notified by the callbacks from HTE subsystem. The
consumers calling those requests APIs with cb parameter NULL, usually will call
hte_retrieve_ts_wait API.

The HTE subsystem provides software buffer per requested id/entity to store
timestamp data (struct hte_ts_data type). The consumers can manage the buffer.
It also provides buffer watermark which can notify (if cb parameter is provided
during request API call) consumer or unblock consumers calling
hte_retrieve_ts_wait API. The following APIs are used to manipulate the
software buffer:

.. c:function:: int hte_set_buf_len( const struct hte_ts_desc *desc,unsigned int len )
		int hte_get_buf_len( const struct hte_ts_desc *desc )

	The consumer uses above APIs to set/get software buffer depth.

.. c:function:: int hte_set_buf_watermark( const struct hte_ts_desc *desc, unsigned int val )
		int hte_get_buf_watermark( const struct hte_ts_desc *desc )

	The consumer uses above APIs to set/get software threshold, threshold
	can be used to notity or unblock waiting consumer when data becomes
	available equal or above to threshold value.

.. c:function:: size_t hte_available_ts( const struct hte_ts_desc *desc )

	The consumer uses above API to get available timestamp data stored
	in the software buffer for the desc.

The detail about parameters and API usage are described in each functions
definitions in ``drivers/hte/hte.c`` file.

The HTE timestamp element detail
--------------------------------
The struct hte_ts_data, declared at ``include/linux/hte.h``, is used to pass
timestamp details between the consumers and the providers. It expresses
timestamp data in nano second in u64 data type. For now all the HTE APIs
using struct hte_ts_data requires tsc to be in nano seconds. The timestamp
element structure stores below information along with timestamp data::

 struct hte_ts_data {
	/*
	 * Timestamp value
	 */
	u64 tsc;
	/*
	 * The sequence counter, keep track of the number of timestamps.
	 * It can be used to check if data is dropped in between.
	 */
	u64 seq;
	/* Direction of the event, i.e. falling or rising */
	int dir;
 };

The typical hte_ts_data data life cycle::
In this example the provider provides timestamp in nano seconds and for the
GPIO line::

 - Monitors GPIO line change.
 - Detects the state change on GPIO line.
 - Converts timestamps in nano seconds and stores it in tsc.
 - Stores GPIO direction in dir variable if the provider has that hardware
 capability.
 - Pushes this hte_timestamp_el object to HTE subsystem.
 - HTE subsystem increments seq counter and stores it in software buffer
 dedicated to requested GPIO line.
 - Waiting consumer gets notified.
 - The consumer calls the retrieve timestamp API.

HTE subsystem debugfs attributes
--------------------------------
HTE subsystem creates debugfs attributes at ``/sys/kernel/debug/hte/``.
It also creates line/signal related debugfs attributes at
``/sys/kernel/debug/hte/<provider>/<label or line id>/``.

`ts_requested`
		The total number of entities requested from the given provider,
		where entity is the provider specific and could represent
		lines, GPIO, chip signals, buses etc...
                The attribute will be availble at
		``/sys/kernel/debug/hte/<provider>/``.

		Read only value

`total_ts`
		The total number of entities supported by the provider.
                The attribute will be availble at
		``/sys/kernel/debug/hte/<provider>/``.

		Read only value

`ts_buffer_depth`
		The software buffer lenth to store timestamp data.
                The attribute will be availble at
		``/sys/kernel/debug/hte/<provider>/<label or id>/``.

		Read only value

`ts_buffer_watermark`
		The software buffer watermark or threshold.
                The attribute will be availble at
		``/sys/kernel/debug/hte/<provider>/<label or line id>/``.

		Read only value

`dropped_timestamps`
		The dropped timestamps for a given line.
                The attribute will be availble at
		``/sys/kernel/debug/hte/<provider>/<label or line id>/``.

		Read only value
