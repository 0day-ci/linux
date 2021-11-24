.. SPDX-License-Identifier: GPL-2.0

=========================
octeontx2 devlink support
=========================

This document describes the devlink features implemented by the ``octeontx2 AF, PF and VF``
device drivers.

Parameters
==========

The ``octeontx2 PF and VF`` drivers implement the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``mcam_count``
     - u16
     - runtime
     - Select number of match CAM entries to be allocated for an interface.
       The same is used for ntuple filters of the interface. Supported by
       PF and VF drivers.

The ``octeontx2 AF`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``dwrr_mtu``
     - u32
     - runtime
     - Use to set the quantum which hardware uses for scheduling among transmit queues.
       Hardware uses weighted DWRR algorithm to schedule among all transmit queues.
   * - ``tim_capture_timers``
     - u8
     - runtime
     - Trigger capture of cycles count of TIM clock sources. Valid values are:
        * 0 - capture free running cycle count.
        * 1 - capture at the software trigger.
        * 2 - capture at the next rising edge of GPIO.
   * - ``tim_capture_tenns``
     - String
     - runtime
     - Capture cycle count of tenns clock.
   * - ``tim_capture_gpios``
     - String
     - runtime
     - Capture cycle count of gpios clock.
   * - ``tim_capture_gti``
     - String
     - runtime
     - Capture cycle count of gti clock.
   * - ``tim_capture_ptp``
     - String
     - runtime
     - Capture cycle count of ptp clock.
   * - ``tim_capture_sync``
     - String
     - runtime
     - Capture cycle count of sync clock.
   * - ``tim_capture_bts``
     - String
     - runtime
     - Capture cycle count of bts clock.
   * - ``tim_capture_ext_gti``
     - String
     - runtime
     - Capture cycle count of external gti clock.
   * - ``tim_adjust_timers``
     - Boolean
     - runtime
     - Trigger adjustment of all TIM clock sources.
   * - ``tim_adjust_tenns``
     - String
     - runtime
     - Adjustment required in number of cycles for tenns clock.
   * - ``tim_adjust_gpios``
     - String
     - runtime
     - Adjustment required in number of cycles for gpios clock.
   * - ``tim_adjust_gti``
     - String
     - runtime
     - Adjustment required in number of cycles for gti clock.
   * - ``tim_adjust_ptp``
     - String
     - runtime
     - Adjustment required in number of cycles for ptp clock.
   * - ``tim_adjust_bts``
     - String
     - runtime
     - Adjustment required in number of cycles for bts clock.
