.. SPDX-License-Identifier: GPL-2.0+

=============================
Flexcan CAN Controller driver
=============================

Authors: Marc Kleine-Budde <mkl@pengutronix.de>,
Dario Binacchi <dario.binacchi@amarula.solutions.com>

On/off RTR frames reception
===========================

 1. interface down::

      ethtool --set-priv-flags can0 rx-rtr {off|on}

 2. interface up::

      ip link set dev can0 down
      ethtool --set-priv-flags can0 rx-rtr {off|on}
      ip link set dev can0 up

Note. For the Flexcan on i.MX25, i.Mx28, i.MX35 and i.Mx53 SOCs, the reception
of RTR frames is possible only if the controller is configured in RxFIFO mode.
In this mode only 6 of the 64 message buffers are used for reception.
