.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-selection-flags:

***************
Selection flags
***************

.. _v4l2-selection-flags-table:

.. raw:: latex

   \small

.. tabularcolumns:: |p{5.6cm}|p{2.0cm}|p{6.5cm}|p{1.2cm}|p{1.2cm}|

.. cssclass:: longtable

.. flat-table:: Selection flag definitions
    :header-rows:  1
    :stub-columns: 0

    * - Flag name
      - id
      - Definition
      - Valid for V4L2
      - Valid for V4L2 subdev
    * - ``V4L2_SEL_FLAG_GE``
      - (1 << 0)
      - Suggest the driver it should choose greater or equal rectangle (in
	size) than was requested. Albeit the driver may choose a lesser
	size, it will only do so due to hardware limitations. Without this
	flag (and ``V4L2_SEL_FLAG_LE``) the behaviour is to choose the
	closest possible rectangle.
      - Yes
      - Yes
    * - ``V4L2_SEL_FLAG_LE``
      - (1 << 1)
      - Suggest the driver it should choose lesser or equal rectangle (in
	size) than was requested. Albeit the driver may choose a greater
	size, it will only do so due to hardware limitations.
      - Yes
      - Yes
    * - ``V4L2_SEL_FLAG_KEEP_CONFIG``
      - (1 << 2)
      - The configuration must not be propagated to any further processing
	steps. If this flag is not given, the configuration is propagated
	inside the subdevice to all further processing steps.
      - No
      - Yes
    * - ``V4L2_SEL_FLAG_ROI_AUTO_EXPOSURE``
      - (1 << 0)
      - Auto Exposure.
      - Yes
      - No
    * - ``V4L2_SEL_FLAG_ROI_AUTO_IRIS``
      - (1 << 1)
      - Auto Iris.
      - Yes
      - No
    * - ``V4L2_SEL_FLAG_ROI_AUTO_WHITE_BALANCE``
      - (1 << 2)
      - Auto White Balance.
      - Yes
      - No
    * - ``V4L2_SEL_FLAG_ROI_AUTO_FOCUS``
      - (1 << 3)
      - Auto Focus.
      - Yes
      - No
    * - ``V4L2_SEL_FLAG_ROI_AUTO_FACE_DETECT``
      - (1 << 4)
      - Auto Face Detect.
      - Yes
      - No
    * - ``V4L2_SEL_FLAG_ROI_AUTO_DETECT_AND_TRACK``
      - (1 << 5)
      - Auto Detect and Track.
      - Yes
      - No
    * - ``V4L2_SEL_FLAG_ROI_AUTO_IMAGE_STABILIXATION``
      - (1 << 6)
      - Image Stabilization.
      - Yes
      - No
    * - ``V4L2_SEL_FLAG_ROI_AUTO_HIGHER_QUALITY``
      - (1 << 7)
      - Higher Quality.
      - Yes
      - No

.. raw:: latex

   \normalsize
