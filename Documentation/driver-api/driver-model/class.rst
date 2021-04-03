==============
Device Classes
==============

Introduction
~~~~~~~~~~~~
A device class describes a type of device, like an audio or network
device. It defines a set of semantics and a programming interface
that devices of that class adhere to. Device drivers are the
implementation of that programming interface for a particular device on
a particular bus.

Device classes are agnostic with respect to what bus a device resides
on.

Programming Interface
~~~~~~~~~~~~~~~~~~~~~
The device class structure looks like::

  struct class {
        const char              *name;
        struct module           *owner;

        const struct attribute_group    **class_groups;
        const struct attribute_group    **dev_groups;
        struct kobject                  *dev_kobj;
        ...
  };

See the kerneldoc for the struct class.

A typical device class definition would look like::

  struct class input_class = {
        .name           = "input",
        .dev_release    = input_dev_release,
  };

Each device class structure should be exported in a header file so it
can be used by drivers, extensions and interfaces.

Device classes are registered and unregistered with the core using::

  int class_register(struct class *class);
  void class_unregister(struct class *class);

Devices
~~~~~~~
When a device is added, it is also added to the 'klist_devices' inside
the 'subsys_private' struct of the class. Later, the devices belonging
to the class are accessed using::

  class_dev_iter_next()
  class_find_device()
  class_find_device_by_name()

It is also possible to access the devices of a class in a platform
dependent way using::

  class_find_device_by_of_node()
  class_find_device_by_acpi_dev()

sysfs directory structure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
There is a top-level sysfs directory named 'class'.

Each class gets a directory in the top-level class directory::

  class/
      |-- input
      |-- block
      |-- drm
      |-- nvme

Each device gets a symlink in the class directory that points to the
device's directory in the physical hierarchy::

  class/
  |-- input
           |-- input0 -> ../../devices/LNXSYSTM:00/LNXSYBUS:00/PNP0C0E:00/input/input0
           |-- input1 -> ../../devices/LNXSYSTM:00/LNXSYBUS:00/PNP0C0D:00/input/input1
           |-- event0 -> ../../devices/LNXSYSTM:00/LNXSYBUS:00/PNP0C0E:00/input/input0/event0
           `-- event1 -> ../../devices/LNXSYSTM:00/LNXSYBUS:00/PNP0C0D:00/input/input1/event1

Exporting Attributes
~~~~~~~~~~~~~~~~~~~~

::

  struct class_attribute {
        struct attribute attr;
        ssize_t (*show)(struct class *class, struct class_attribute *attr,
                        char *buf);
        ssize_t (*store)(struct class *class, struct class_attribute *attr,
                        const char *buf, size_t count);
  };

Class drivers can export attributes using the CLASS_ATTR_* macros that works
similarly to the DEVICE_ATTR_* macros for devices. For example, a definition
like this::

  static CLASS_ATTR_RW(debug, 0644, show_debug, store_debug);

is equivalent to declaring::

  static struct class_attribute class_attr_debug = {
        .attr = {
              .name = debug,
              .mode = 0644,
        },
        .show = show_debug,
        .store = store_debug,
  };

The drivers can add and remove the attribute from the class's sysfs
directory using::

  int class_create_file(struct class *, struct class_attribute *);
  void class_remove_file(struct class *, struct class_attribute *);

In the example above, a file named 'debug' will be placed in the
class's directory in sysfs.


Interfaces
~~~~~~~~~~
There may exist multiple mechanisms for accessing the same device of a
particular class type. Device interfaces describe these mechanisms.

When a device is added to a device class, the core attempts to add it
to every interface that is registered with the device class. The
interfaces can be added and removed from the class using::

  int class_interface_register(struct class_interface *);
  void class_interface_unregister(struct class_interface *);

For further information, see <linux/device/class.h>.
