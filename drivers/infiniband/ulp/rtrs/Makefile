# SPDX-License-Identifier: GPL-2.0-or-later

rtrs-client-y := rtrs-clt.o \
		  rtrs-clt-stats.o \
		  rtrs-clt-sysfs.o
rtrs-client-$(CONFIG_FAULT_INJECTION_DEBUG_FS) += rtrs-fault.o

rtrs-server-y := rtrs-srv.o \
		  rtrs-srv-stats.o \
		  rtrs-srv-sysfs.o
rtrs-server-$(CONFIG_FAULT_INJECTION_DEBUG_FS) += rtrs-fault.o

rtrs-core-y := rtrs.o

obj-$(CONFIG_INFINIBAND_RTRS)        += rtrs-core.o
obj-$(CONFIG_INFINIBAND_RTRS_CLIENT) += rtrs-client.o
obj-$(CONFIG_INFINIBAND_RTRS_SERVER) += rtrs-server.o
