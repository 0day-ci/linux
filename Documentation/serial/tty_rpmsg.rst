.. SPDX-License-Identifier: GPL-2.0

=========
RPMsg TTY
=========

The rpmsg tty driver implements serial communication on the RPMsg bus to makes possible for
user-space programs to send and receive rpmsg messages as a standard tty protocol.

The remote processor can instantiate a new tty by requesting a "rpmsg-tty" RPMsg service.

The "rpmsg-tty" service is directly used for data exchange. No flow control is implemented.

Information related to the RPMsg and associated tty device is available in
/sys/bus/rpmsg/devices/.
