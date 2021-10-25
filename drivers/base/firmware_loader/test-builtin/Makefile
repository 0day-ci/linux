# SPDX-License-Identifier: GPL-2.0
include $(srctree)/drivers/base/firmware_loader/builtin/lib.Makefile

extra-y := test-builtin-firmware.bin
$(obj)/test-builtin-firmware.bin: FORCE
	@$(kecho) "  GEN     $@"
	@(set -e;			\
	(				\
		echo 'ABCD0123';	\
	) > $@)

# Create $(fwdir) from $(CONFIG_TEST_FIRMWARE_BUILTIN_DIR) -- if it doesn't
# have a leading /, it's relative to $(srctree).
fwdir := $(subst $(quote),,$(CONFIG_TEST_FIRMWARE_BUILTIN_DIR))
fwdir := $(addprefix $(srctree)/,$(filter-out /%,$(fwdir)))$(filter /%,$(fwdir))

firmware  := $(addsuffix .gen.o, $(subst $(quote),,$(CONFIG_TEST_FIRMWARE_BUILTIN_FILES)))
obj-y += $(firmware)
