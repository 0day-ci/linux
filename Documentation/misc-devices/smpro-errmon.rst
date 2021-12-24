.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver Ampere(R)'s Altra(R) SMpro errmon
===============================================

Supported chips:

  * Ampere(R) Altra(R)

    Prefix: 'smpro'

    Preference: Altra SoC BMC Interface Specification

Author: Thu Nguyen <thu@os.amperecomputing.com>

Description
-----------

This driver supports hardware monitoring for Ampere(R) Altra(R) SoC's based on the
SMpro co-processor (SMpro).
The following SoC alert/event types are supported by the errmon driver:

* Core CE/UE errors
* Memory CE/UE errors
* PCIe CE/UE errors
* Other CE/UE errors
* Internal SMpro/PMpro errors
* VRD hot
* VRD warn/fault
* DIMM Hot
* DIMM 2x refresh rate

The SMpro interface provides the registers to query the status of the SoC alerts/events
and their data and export to userspace by this driver.

Usage Notes
-----------

SMpro errmon driver creates the sysfs files for each host alert/event type.
Example: ``errors_core_ce`` to get Core CE error type.

To get a host alert/event type, the user will read the corresponding sysfs file.

* If the alert/event is absented, the sysfs file returns empty.
* If the alerts/events are presented, the existing alerts/events will be reported as the error lines.

The format of the error lines is defended on the alert/event type.

1) Type 1 for Core/Memory/PCIe/Other CE/UE alert types::

    <Error Type> <Error SubType> <Instance> <Error Status> <Error Address> <Error Misc 0> <Error Misc 1> <Error Misc2> <Error Misc 3>

    Where:
    * Error Type: The hardwares cause the errors in format of two hex characters.
    * SubType: Sub type of error in the specified hardware error in format of two hex characters.
    * Instance: Combination of the socket, channel, slot cause the error in format of four hex characters.
    * Error Status: Encode of error status in format of eight hex characters.
    * Error Address: The address in device causes the errors in format of sixteen hex characters.
    * Error Misc 0/1/2/3: Addition info about the errors. Each field is in format of sixteen hex characters.

    Example:
    # cat errors_other_ce
    0a 02 0000 000030e4 0000000000000080 0000020000000000 0000000000000000 0000000000000000 0000000000000000
    0a 01 0000 000030e4 0000000000000080 0000020000000000 0000000000000000 0000000000000000 0000000000000000

    The size of the alert buffer for this error type is 8 alerts.
    When the buffer is overflowed, the errmon driver will be added the overflowed alert line to sysfs output.

    ff ff 0000 00000000 0000000000000080 0000000000000000 0000000000000000 0000000000000000 0000000000000000

Below table defines the value of Error types, Sub Types, Sub component and instance:

    ============   ==========    =========   ===============  ================
    Error Group    Error Type    Sub type    Sub component    Instance
    CPM            0             0           Snoop-Logic      CPM #
    CPM            0             2           Armv8 Core 1     CPM #
    MCU            1             1           ERR1             MCU # | SLOT << 11
    MCU            1             2           ERR2             MCU # | SLOT << 11
    MCU            1             3           ERR3             MCU #
    MCU            1             4           ERR4             MCU #
    MCU            1             5           ERR5             MCU #
    MCU            1             6           ERR6             MCU #
    MCU            1             7           Link Error       MCU #
    Mesh           2             0           Cross Point      X | (Y << 5) | NS <<11
    Mesh           2             1           Home Node(IO)    X | (Y << 5) | NS <<11
    Mesh           2             2           Home Node(Mem)   X | (Y << 5) | NS <<11 | device<<12
    Mesh           2             4           CCIX Node        X | (Y << 5) | NS <<11
    2P Link        3             0           N/A              Altra 2P Link #
    GIC            5             0           ERR0             0
    GIC            5             1           ERR1             0
    GIC            5             2           ERR2             0
    GIC            5             3           ERR3             0
    GIC            5             4           ERR4             0
    GIC            5             5           ERR5             0
    GIC            5             6           ERR6             0
    GIC            5             7           ERR7             0
    GIC            5             8           ERR8             0
    GIC            5             9           ERR9             0
    GIC            5             10          ERR10            0
    GIC            5             11          ERR11            0
    GIC            5             12          ERR12            0
    GIC            5             13-21       ERR13            RC# + 1
    SMMU           6             TCU         100              RC #
    SMMU           6             TBU0        0                RC #
    SMMU           6             TBU1        1                RC #
    SMMU           6             TBU2        2                RC #
    SMMU           6             TBU3        3                RC #
    SMMU           6             TBU4        4                RC #
    SMMU           6             TBU5        5                RC #
    SMMU           6             TBU6        6                RC #
    SMMU           6             TBU7        7                RC #
    SMMU           6             TBU8        8                RC #
    SMMU           6             TBU9        9                RC #
    PCIe AER       7             Root        0                RC #
    PCIe AER       7             Device      1                RC #
    PCIe RC        8             RCA HB      0                RC #
    PCIe RC        8             RCB HB      1                RC #
    PCIe RC        8             RASDP       8                RC #
    OCM            9             ERR0        0                0
    OCM            9             ERR1        1                0
    OCM            9             ERR2        2                0
    SMpro          10            ERR0        0                0
    SMpro          10            ERR1        1                0
    SMpro          10            MPA_ERR     2                0
    PMpro          11            ERR0        0                0
    PMpro          11            ERR1        1                0
    PMpro          11            MPA_ERR     2                0
    =============  ==========    =========   ===============  ================


2) Type 2 for the Internal SMpro/PMpro alert types::

    <Error Type> <Error SubType> <Direction> <Error Location> <Error Code> <Error Data>

    Where:
    * Error Type: SMpro/PMpro Error types in format of two hex characters.
      + 1: Warning
      + 2: Error
      + 4: Error with data
    * Error SubType: SMpro/PMpro Image Code in format of two hex characters.
    * Direction: Direction in format of two hex characters.
      + 0: Enter
      + 1: Exit
    * Error Location: SMpro/PMpro Module Location code in format of two hex characters.
    * Error Code: SMpro/PMpro Error code in format of four hex characters.
    * Error Data: Extensive datae in format of eight hex characters.
      All bits are 0 when Error Type is warning or error.

    Example:
    # cat errors_smpro
    01 04 01 08 0035 00000000

3) Type 3 for the VRD hot, VRD /warn/fault, DIMM Hot, DIMM 2x refresh rate event::

    <Event Type> <Event SubType> <Direction> <Event Location> [Event Data]

    Where:
    * Event Type: event type in format of two hex characters.
    * Event SubType: event sub type in format of two hex characters.
    * Direction: Direction in format of two hex characters.
      + 0: Asserted
      + 1: De-asserted
    * Event Location: The index of component cause the alert in format of two hex characters.
    * Event Data: Extensive data if have in format of four hex characters.

    Example:
    #cat event_vr_hot
    00 02 00 00 -> /* DIMM VRD hot event is asserted at channel 0 */
    00 02 01 00 -> /* DIMM VRD hot event is de-asserted at channel 0 */
    00 01 00 03 -> /* Core VRD hot event is asserted at channel 3 */
    00 00 00 00 -> /* SoC  VRD hot event is asserted */
    00 00 00 00 -> /* SoC  VRD hot event is de-asserted */
    00 02 00 06 -> /* DIMM VRD hot event is de-asserted at channel 6 */

Sysfs entries
-------------

The following sysfs files are supported:

* Ampere(R) Altra(R):

Alert Types:

    ================= =============== =========================================================== =======
    Alert Type        Sysfs name      Description                                                 Format
    Core CE Errors    errors_core_ce  Triggered by CPU when Core has an CE error                  1
    Core UE Errors    errors_core_ue  Triggered by CPU when Core has an UE error                  1
    Memory CE Errors  errors_mem_ce   Triggered by CPU when Memory has an CE error                1
    Memory UE Errors  errors_mem_ue   Triggered by CPU when Memory has an UE error                1
    PCIe CE Errors    errors_pcie_ce  Triggered by CPU when any PCIe controller has any CE error  1
    PCIe UE Errors    errors_pcie_ue  Triggered by CPU when any PCIe controller has any UE error  1
    Other CE Errors   errors_other_ce Triggered by CPU when any Others CE error                   1
    Other UE Errors   errors_other_ue Triggered by CPU when any Others UE error                   1
    SMpro Errors      errors_smpro    Triggered by CPU when system have SMpro error               2
    PMpro Errors      errors_pmpro    Triggered by CPU when system have PMpro error               2
    ================= =============== =========================================================== =======

Event Type:

    ============================ ========================== =========== ========================
    Event Type                   Sysfs name                 Event Type  Sub Type
    VRD HOT                      event_vrd_hot              0           0: SoC, 1: Core, 2: DIMM
    VR Warn/Fault                event_vrd_warn_fault       1           0: SoC, 1: Core, 2: DIMM
    DIMM Hot                     event_dimm_hot             2           NA (Default 0)
    DIMM 2x refresh rate status  event_dimm_2x_refresh      3           NA (Default 0)
    ============================ ========================== =========== ========================
