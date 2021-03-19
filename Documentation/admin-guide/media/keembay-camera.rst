.. SPDX-License-Identifier: GPL-2.0

.. include:: <isonum.txt>

===========================
Intel Keembay camera driver
===========================

Copyright |copy| 2021 Intel Corporation

Introduction
============

This file documents the Intel Keem Bay camera driver located under
drivers/media/platform/keembay-camera.

The current version of the driver supports Intel Keem Bay VPU Camera Subsystem
found in Intel Keem Bay platform.

The Keem Bay VPU camera receives the raw Bayer data from the sensors
and outputs the frames in a YUV format, it operates in per-frame mode
and processing parameters are required for each processed video output.

The Keem Bay Camera driver uses Xlink for communication with remote Keem Bay
VPU Camera subsystem.

The driver implements V4L2, Media controller and V4L2 subdev interfaces.
Camera sensor using V4L2 subdev interface in the kernel is supported.

Topology
========
.. _keembay_camera_topology_graph:

.. kernel-figure:: keembay_camera.dot
    :alt:   Diagram of the Keem Bay Camera media pipeline topology
    :align: center


The driver has 1 subdevice:

- keembay-camera-isp: ISP subdevice responsible for all ISP operatios.
  The subdevice supports V4L2_EVENT_FRAME_SYNC event.

The driver has 3 video devices:

- kmb-video-capture: capture device for retrieving processed YUV output.
- keembay-metadata-stats: metadata capture device for retrieving statistics.
- keembay-metadata-params: metadata output device that receives processing
  parameters from userspace.

Device operation
----------------

The Keem Bay Camera driver is represented as a media device with single
V4L2 ISP subdev, which provides a V4L2 subdev interface to the user space.

The V4L2 ISP subdev represents a pipe, which can support a maximum of one stream.

The pipe has two source pads and two sink pads for the following purpose:

.. tabularcolumns:: |p{0.8cm}|p{4.0cm}|p{4.0cm}|

.. flat-table::

    * - pad
      - direction
      - purpose

    * - 0
      - sink
      - MIPI CSI-2 input, connected to the sensor subdev

    * - 1
      - sink
      - Processing parameters

    * - 2
      - source
      - Output processed video stream

    * - 3
      - source
      - 3A statistics

Pad 0 is connected to sensor subdev and should receive data in raw Bayer
format over MIPI CSI-2 receiver.

Pads 1, 2 and 3 are connected to a corresponding V4L2 video interface,
exposed to userspace as a V4L2 video device node.

With ISP subdev once the input video node keembay-metadata-params
connected to pad 1 is queued with ISP processing parameters buffer,
ISP subdev starts processing and produces the video output in
YUV format and statistics output on respective output node.

At a minimum, all of the video nodes should be enabled and have buffers queued
to start the processing.

The Keem Bay ISP V4L2 subdev has the following set of video nodes:

Capture video node
------------------

The frames received by the sensor over MIPI CSI-2 input are processed by the
Keem Bay ISP and are output to one single video node in YUV format.

Only the multi-planar API is supported. More details can be found at
:ref:`planar-apis`.

Parameters video node
---------------------

The parameters video node receives the Keem Bay ISP algorithm parameters [#f1]_
that are used to configure how the Keem Bay ISP algorithms process the image.

Details on processing parameters specific to the Keem Bay ISP can be found in
:ref:`v4l2-meta-fmt-params`.

Statistics video node
---------------------

3A statistics video node is used by the Keem Bay ISP driver to output the
statistics for the frames that are being processed by the Keem Bay ISP to
user space applications. User space applications can use this statistics
data to compute the desired algorithm parameters for the Keem Bay ISP.


Configuring the Keem Bay Camera driver
======================================

The Keem Bay Camera pipeline can be configured using the Media Controller,
defined at
:ref:`media_controller`.

Configuring Keem Bay ISP subdev for frame processing
----------------------------------------------------

The Keem Bay ISP V4L2 subdev has to be configured with media controller APIs
to have all the video nodes setup correctly.

Let us take "keembay-camera-isp" subdev as an example. We will use
media-ctl [#f3]_ and yavta [#f2]_ tools for our example.
Lets assume that we have sensor subdev connected which produces Raw bayer to

./media-ctl -d $MDEV  -V "'keembay-camera-isp':0[fmt:SRGGB12_1X12/3840x2160]"

./media-ctl -d $MDEV  -V "'keembay-camera-isp':3[fmt:YUYV8_1_5X8/3840x2160]"


Now the pipeline is configured and ready to stream. Keem Bay ISP need buffers
to  be queued on the all of the video nodes to start the stream.
For that purpose we can use multiple instancies of the yavta tool:

yavta --data-prefix -Bmeta-output -c10 -n5 \
--file=isp-config.bin /dev/video0 &

yavta --data-prefix -Bmeta-capture -c10 -n5 -I \
--file=frame-#.stat /dev/video1 &

yavta --data-prefix -Bcapture-mplane -c10 -n5 -I -s3840x2160 \
--file=frame-#.out -f NV12 /dev/video2 &


The captured frames will be stored to frame-#.out files and statistics for
corresponding frames in frame-#.stat files.

References
==========

.. [#f1] include/uapi/linux/keembay-isp-ctl.h

.. [#f2] http://git.ideasonboard.org/yavta.git

.. [#f3] http://git.ideasonboard.org/?p=media-ctl.git;a=summary
