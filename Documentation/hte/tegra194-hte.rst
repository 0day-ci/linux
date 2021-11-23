HTE Kernel provider driver
==========================

Description
-----------
The Nvidia tegra194 HTE provider driver implements two GTE
(Generic Timestamping Engine) instances: 1) GPIO GTE and 2) LIC
(Legacy Interrupt Controller) IRQ GTE. Both GTEs instances get the
timestamp from the system counter TSC which has 31.25MHz clock rate, and the
driver converts clock tick rate to nanoseconds before storing it as timestamp
value.

GPIO GTE
--------

This GTE instance timestamps GPIO in real time. For that to happen GPIO
needs to be configured as input and IRQ needs to be enabled. The only always on
(AON) GPIO controller instance supports timestamping GPIOs in real time and it
has 39 GPIO lines. The GPIO GTE and AON GPIO controller are tightly coupled as
it requires very specific bits to be set in GPIO config register before GPIO
GTE can be used. The GPIO GTE functionality is accessed from the GPIOLIB
framework for the in-kernel and userspace consumers. In the latter case,
requests go through GPIOLIB CDEV framework. The below APIs are added in GPIOLIB
framework to access HTE subsystem and GPIO GTE.

.. kernel-doc:: drivers/gpio/gpiolib.c
   :functions: gpiod_req_hw_timestamp_ns gpiod_rel_hw_timestamp_ns

There is hte-tegra194-gpio-test.c, located in ``drivers/hte/`` directory, test
driver which demonstrates above APIs for the Jetson AGX platform.

For userspace consumers, GPIO_V2_LINE_FLAG_EVENT_CLOCK_HARDWARE flag must be
specified during IOCTL calls. Refer to ``tools/gpio/gpio-event-mon.c``, which
returns the timestamp in nanoseconds.

LIC (Legacy Interrupt Controller) IRQ GTE
-----------------------------------------

This GTE instance timestamps LIC IRQ lines in real time. There are 352 IRQ
lines which this instance can add timestamps to in real time. The hte
devicetree binding described at ``Documentation/devicetree/bindings/hte/``
provides an example of how a consumer can request an IRQ line. Since it is a
one-to-one mapping, consumers can simply specify the IRQ number that they are
interested in. There is no userspace consumer support for this GTE instance in
the hte framework. The sample test code hte-tegra194-irq-test.c, located in
the ``drivers/hte/`` directory, demonstrates how to use an IRQ GTE instance.
The below is sample device tree snippet code for the test driver::

 tegra_hte_irq_test {
        compatible = "nvidia,tegra194-hte-irq-test";
        htes = <&tegra_hte_lic 0x19>;
        hte-names = "hte-lic";
 };

The provider source code of both IRQ and GPIO GTE instances is located at
``drivers/hte/hte-tegra194.c``.

