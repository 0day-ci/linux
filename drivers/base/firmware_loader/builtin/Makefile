# SPDX-License-Identifier: GPL-2.0
include $(srctree)/drivers/base/firmware_loader/builtin/lib.Makefile

obj-y  += main.o
obj-$(CONFIG_TEST_BUILTIN_FIRMWARE)  += test-builtin-firmware.bin.gen.o

# Create $(fwdir) from $(CONFIG_FW_LOADER_BUILTIN_DIR) -- if it doesn't have a
# leading /, it's relative to $(srctree).
fwdir := $(subst $(quote),,$(CONFIG_FW_LOADER_BUILTIN_DIR))
fwdir := $(addprefix $(srctree)/,$(filter-out /%,$(fwdir)))$(filter /%,$(fwdir))

firmware  := $(addsuffix .gen.o, $(subst $(quote),,$(CONFIG_FW_LOADER_BUILTIN_FILES)))
obj-y += $(firmware)
