# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_NET_DSA_MSCC_FELIX) += mscc_felix.o
obj-$(CONFIG_NET_DSA_MSCC_OCELOT_SPI) += mscc_ocelot_spi.o
obj-$(CONFIG_NET_DSA_MSCC_SEVILLE) += mscc_seville.o

mscc_felix-objs := \
	felix.o \
	felix_vsc9959.o

mscc_ocelot_spi-objs := \
	felix.o \
	felix_mdio.o \
	ocelot_vsc7512_spi.o

mscc_seville-objs := \
	felix.o \
	felix_mdio.o \
	seville_vsc9953.o
