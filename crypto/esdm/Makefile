# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Entropy Source and DRNG Manager.
#

obj-y					+= esdm_es_mgr.o esdm_drng_mgr.o \
					   esdm_es_aux.o
obj-$(CONFIG_CRYPTO_ESDM_SHA256)	+= esdm_sha256.o

obj-$(CONFIG_CRYPTO_ESDM_DRNG_KCAPI)	+= esdm_drng_kcapi.o
