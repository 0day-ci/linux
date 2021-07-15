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
     - Set rx BD buffer size, now only support setting 2048 and 4096.

       * The feature is used to change the buffer size of each BD of Rx ring
         between 2KB and 4KB, then do devlink reload operation to take effect.
   * - ``tx_buf_size``
     - U32
     - driverinit
     - Set tx bounce buf size.

       * The size is setted for tx bounce feature. Tx bounce buffer feature is
         used for small size packet or frag. It adds a queue based tx shared
         bounce buffer to memcpy the small packet when the len of xmitted skb is
         below tx_copybreak(value to distinguish small size and normal size),
         and reduce the overhead of dma map and unmap when IOMMU is on.

The ``hns3`` driver supports reloading via ``DEVLINK_CMD_RELOAD``.

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
