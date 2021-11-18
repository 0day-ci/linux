.. SPDX-License-Identifier: GPL-2.0-or-later

Configfs GPIO Simulator
=======================

The configfs GPIO Simulator (gpio-sim) provides a way to create simulated GPIO
chips for testing purposes. The lines exposed by these chips can be accessed
using the standard GPIO character device interface as well as manipulated
using sysfs attributes.

Creating simulated chips
------------------------

The gpio-sim module registers a configfs subsystem called 'gpio-sim'.

In order to instantiate a new simulated chip, the user needs to mkdir() a new
directory gpio-sim/. Inside each new directory, there's a set of attributes
that can be used to configure the new chip. Additionally the user can mkdir()
subdirectories inside the chip's directory that allow to pass additional
configuration for specific lines. The name of those subdirectories must take
the form of: 'line<offset>' (e.g. 'line0', 'line20', etc.) as the name will be
used by the module to assign the config to the specific line at given offset.

Once the confiuration is complete, the 'active' attribute must be set to 1 in
order to instantiate the chip. It can be set back to 0 to destroy the simulated
chip.

Currently supported chip configuration attributes are:

  num_lines - an unsigned integer value defining the number of GPIO lines to
              export

  label - a string defining the label for the GPIO chip

Additionally two read-only attributes named 'chip_name' and 'dev_name' are
exposed in order to provide users with a mapping from configfs directories to
the actual devices created in the kernel. The former returns the name of the
GPIO device as assigned by gpiolib (i.e. "gpiochip0", "gpiochip1", etc.). The
latter returns the parent device name as defined by the gpio-sim driver (i.e.
"gpio-sim.0", "gpio-sim.1", etc.). This allows user-space to map the configfs
items both to the correct character device file as well as the associated entry
in sysfs.

Supported line configuration attributes are:

  name - a string defining the name of this line as used by the
         "gpio-line-names" device property

Simulated GPIO chips can also be defined in device-tree. The compatible string
must be: "gpio-simulator". Supported properties are:

  "gpio-sim,label" - chip label

Other standard GPIO properties (like "gpio-line-names", "ngpios" or gpio-hog)
are also supported.

Manipulating simulated lines
----------------------------

Each simulated GPIO chip creates a sysfs attribute group under its device
directory called 'line-ctrl'. Inside each group, there's a separate attribute
for each GPIO line. The name of the attribute is of the form 'gpioX' where X
is the line's offset in the chip.

Reading from a line attribute returns the current value. Writing to it (0 or 1)
changes the configuration of the simulated pull-up/pull-down resistor
(1 - pull-up, 0 - pull-down).
