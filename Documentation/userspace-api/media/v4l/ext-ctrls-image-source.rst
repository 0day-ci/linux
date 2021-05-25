.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _image-source-controls:

******************************
Image Source Control Reference
******************************

The Image Source control class is intended for low-level control of
image source devices such as image sensors. The devices feature an
analogue to digital converter and a bus transmitter to transmit the
image data out of the device.


.. _image-source-control-id:

Image Source Control IDs
========================

``V4L2_CID_IMAGE_SOURCE_CLASS (class)``
    The IMAGE_SOURCE class descriptor.

``V4L2_CID_VBLANK (integer)``
    Vertical blanking. The idle period after every frame during which no
    image data is produced. The unit of vertical blanking is a line.
    Every line has length of the image width plus horizontal blanking at
    the pixel rate defined by ``V4L2_CID_PIXEL_RATE`` control in the
    same sub-device.

``V4L2_CID_HBLANK (integer)``
    Horizontal blanking. The idle period after every line of image data
    during which no image data is produced. The unit of horizontal
    blanking is pixels.

``V4L2_CID_ANALOGUE_GAIN (integer)``
    Analogue gain is gain affecting all colour components in the pixel
    matrix. The gain operation is performed in the analogue domain
    before A/D conversion.

``V4L2_CID_TEST_PATTERN_RED (integer)``
    Test pattern red colour component.

``V4L2_CID_TEST_PATTERN_GREENR (integer)``
    Test pattern green (next to red) colour component.

``V4L2_CID_TEST_PATTERN_BLUE (integer)``
    Test pattern blue colour component.

``V4L2_CID_TEST_PATTERN_GREENB (integer)``
    Test pattern green (next to blue) colour component.

``V4L2_CID_UNIT_CELL_SIZE (struct)``
    This control returns the unit cell size in nanometers. The struct
    :c:type:`v4l2_area` provides the width and the height in separate
    fields to take into consideration asymmetric pixels.
    This control does not take into consideration any possible hardware
    binning.
    The unit cell consists of the whole area of the pixel, sensitive and
    non-sensitive.
    This control is required for automatic calibration of sensors/cameras.

.. c:type:: v4l2_area

.. flat-table:: struct v4l2_area
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``width``
      - Width of the area.
    * - __u32
      - ``height``
      - Height of the area.

``V4L2_CID_NOTIFY_GAIN_RED (integer)``
    Please refer to ``V4L2_CID_NOTIFY_GAIN_GREENB`` below.

``V4L2_CID_NOTIFY_GAIN_GREENR (integer)``
    Please refer to ``V4L2_CID_NOTIFY_GAIN_GREENB`` below.

``V4L2_CID_NOTIFY_GAIN_BLUE (integer)``
    Please refer to ``V4L2_CID_NOTIFY_GAIN_GREENB`` below.

``V4L2_CID_NOTIFY_GAIN_GREENB (integer)``
    Note: this description covers all four ``V4L2_CID_NOTIFY_GAIN``
    controls, according to the Bayer channel indicated at the end of
    the control name.

    The sensor is notified what gain will be applied to this Bayer
    channel by subsequent processing (such as by an ISP). The sensor
    is merely informed of this value in case it performs processing
    that requires it, but it does not apply it to the output pixels
    themselves.

    The units for the gain value are linear, with the default value
    being shared between all four controls and representing a gain of
    exactly 1. Other control properties, such as ranges, should also
    be common.
