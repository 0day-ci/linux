.. SPDX-License-Identifier: GPL-2.0

====================
hns3 devlink support
====================

This document describes the devlink features implemented by the ``hns3``
device driver.

Parameters
==========

The ``hns3`` driver implements the following driver-specific
parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 10 10 10 70

   * - Name
     - Type
     - Mode
     - Description
   * - ``rx_buf_len``
     - U32
     - driverinit
     - Set rx BD buffer size.

       * Now only support setting 2048 and 4096.
   * - ``tx_buf_size``
     - U32
     - driverinit
     - Set tx spare buf size.

       * The size is setted for tx bounce feature.

The ``hns3`` driver supports reloading via ``DEVLINK_CMD_RELOAD``

Info versions
=============

The ``hns3`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 10 10 80

   * - Name
     - Type
     - Description
   * - ``fw``
     - running
     - Used to represent the firmware version.
