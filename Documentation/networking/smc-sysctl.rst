.. SPDX-License-Identifier: GPL-2.0

=========
SMC Sysctl
=========

/proc/sys/net/smc/* Variables
==============================

wmem_default - INTEGER
    Initial size of send buffer used by SMC sockets.
    The default value inherits from net.ipv4.tcp_wmem[1].

    Default: 16K

rmem_default - INTEGER
    Initial size of receive buffer (RMB) used by SMC sockets.
    The default value inherits from net.ipv4.tcp_rmem[1].

    Default: 131072 bytes.
