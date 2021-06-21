=============================
 drm/vdrm virtual DRM driver
=============================

Virtual DRM splits the overlay planes of a display controller into multiple
virtual devices to allow each plane to be accessed by each process.

This makes it possible to overlay images output from multiple processes on a
display. For example, one process displays the camera image without compositor
while another process overlays the UI.

The virtual DRM creates standalone virtual device and make DRM planes from a
master device (e.g. card0) accessible via one or more virtual device. However,
these plane are no longer accessible from the original device.
Each virtual device (and plane) can be accessed via a separate device file.

The virtual DRM driver doesn't directly control the display hardware and has
no access to the physical bus. Instead, the virtual DRM driver issues requests
to the standard DRM device driver ("master" driver) when the hardware needs to
be controlled. The master driver is modified to notify the virtual DRM driver
of interrupt events from the display hardware.

Plane position and size
=======================
The initial position, size and z-position of the plane used in virtual DRM is
specified in the device tree. The position and size of the planes are set as
properties and can be updated. The name of each property is as
follows:
* vdrm_offset_x: x-coordinate of the left-top of the plane on the screen
* vdrm_offset_y: y-coordinate of the left-top of the plane on the screen
* vdrm_width: width of the plane
* vdrm_height: height of the plane

Virtual DRM Functions Reference
===============================

.. kernel-doc:: drivers/gpu/drm/vdrm/vdrm_api.h
    :internal:

.. kernel-doc:: drivers/gpu/drm/vdrm/vdrm_drv.c
    :export:

Driver limitations
==================
1. Each virtual DRM device only supports one plane per CRTC.

2. Virtual DRM doesn't support hot plug connector.

3. If virtual DRM has multiple CRTCs, the initial position and size of the
   virtual DRM planes is the same for all planes, since they cannot be set
   for each plane in the device tree.
