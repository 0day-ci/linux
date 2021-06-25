HTE Kernel provider driver
==========================

Description
-----------
The Nvidia tegra194 chip has many hardware timestamping engine (HTE) instances
known as generic timestamping engine (GTE). This provider driver implements
two GTE instances 1) GPIO GTE and 2) IRQ GTE. The both GTEs instances get the
timestamp from the system counter TSC which has 31.25MHz clock rate, and the
driver converts clock tick rate to nano seconds before storing it as timestamp
value.

GPIO GTE
--------

This GTE instance help timestamps GPIO in real time, for that to happen GPIO
needs to be configured as input and IRQ needs to ba enabled as well. The only
always on (AON) gpio controller instance supports timestamping GPIOs in
realtime and it has 39 GPIO lines. There is also a dependency on AON GPIO
controller as it requires very specific bits to be set in GPIO config register.
It in a way creates cyclic dependency between GTE and GPIO controller. The GTE
GPIO functionality is accessed from the GPIOLIB. It can support both the in
kernel and userspace consumers. In the later case, requests go through GPIOLIB
CDEV framework. The below APIs are added in GPIOLIB framework to access HTE
subsystem and GPIO GTE for in kernel consumers.

.. c:function:: int gpiod_hw_timestamp_control( struct gpio_desc *desc, bool enable )

	To enable HTE on given GPIO line.

.. c:function:: u64 gpiod_get_hw_timestamp( struct gpio_desc *desc, bool block )

	To retrieve hardwre timestamp in nano seconds.

.. c:function:: bool gpiod_is_hw_timestamp_enabled( const struct gpio_desc *desc )

	To query if HTE is enabled on the given GPIO.

There is hte-tegra194-gpio-test.c, located in ``drivers/hte/`` directory, test
driver which demonstrates above APIs for the Jetson AGX platform. For userspace
consumers, GPIO_V2_LINE_FLAG_EVENT_CLOCK_HARDWARE flag must be specifed during
IOCTL calls, refer ``tools/gpio/gpio-event-mon.c``, which returns the timestamp
in nano second.

IRQ GTE
--------

This GTE instance helps timestamp LIC IRQ lines in real time. There are 352 IRQ
lines which this instance can help timestamp realtime. The hte devicetree
binding described at ``Documentation/devicetree/bindings/hte/`` gives out
example how consumer can request IRQ line, since it is one to one mapping,
consumers can simply specify IRQ number that they are interested in. There is
no userspace consumer support for this GTE instance. The sample test code
hte-tegra194-irq-test.c, located in ``drivers/hte/`` directory,
demonstrates how to use IRQ GTE instance. The below is sample device tree
snippet code for the test driver::

 tegra_hte_irq_test {
        compatible = "nvidia,tegra194-hte-irq-test";
        htes = <&tegra_hte_lic 0x19>;
        hte-names = "hte-lic";
 };

The source code of the driver both IRQ and GPIO GTE is locate at
``drivers/hte/hte-tegra194.c``.