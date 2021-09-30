HTE Kernel provider driver
==========================

Description
-----------
The Nvidia tegra194 HTE provider driver implements two GTE
(Generic Timestamping Engine) instances 1) GPIO GTE and 2) LIC IRQ GTE. The
both GTEs instances get the timestamp from the system counter TSC which has
31.25MHz clock rate, and the driver converts clock tick rate to nano seconds
before storing it as timestamp value.

GPIO GTE
--------

This GTE instance timestamps GPIO in real time, for that to happen GPIO
needs to be configured as input and IRQ needs to ba enabled. The only always on
(AON) gpio controller instance supports timestamping GPIOs in realtime and it
has 39 GPIO lines. The GPIO GTE and AON GPIO controller are tightly coupled as
it requires very specific bits to be set in GPIO config register before GPIO
GTE can be used. The GPIO GTE functionality is accessed from the GPIOLIB
framework for the in kernel and userspace consumers. In the later case,
requests go through GPIOLIB CDEV framework. The below APIs are added in GPIOLIB
framework to access HTE subsystem and GPIO GTE.

.. kernel-doc:: drivers/gpio/gpiolib.c
   :functions: gpiod_req_hw_timestamp_ns gpiod_rel_hw_timestamp_ns

There is hte-tegra194-gpio-test.c, located in ``drivers/hte/`` directory, test
driver which demonstrates above APIs for the Jetson AGX platform.

For userspace consumers, GPIO_V2_LINE_FLAG_EVENT_CLOCK_HARDWARE flag must be
specifed during IOCTL calls, refer ``tools/gpio/gpio-event-mon.c``, which
returns the timestamp in nano second.

LIC IRQ GTE
-----------

This GTE instance timestamp LIC IRQ lines in real time. There are 352 IRQ
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

The provider source code of both IRQ and GPIO GTE instances is locate at
``drivers/hte/hte-tegra194.c``.

