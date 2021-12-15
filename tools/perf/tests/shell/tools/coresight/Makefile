# SPDX-License-Identifier: GPL-2.0-only
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021
include ../../../../../../tools/scripts/Makefile.include
include ../../../../../../tools/scripts/Makefile.arch
include ../../../../../../tools/scripts/utilities.mak

SUBDIRS = \
	asm_pure_loop \
	thread_loop \
	bubble_sort \
	bubble_sort_thread \
        memcpy \
        memcpy_thread \
        unroll_loop_thread

all: $(SUBDIRS)
$(SUBDIRS):
	$(Q)$(MAKE) -C $@

INSTALLDIRS = $(SUBDIRS:%=install-%)

install-tests: $(INSTALLDIRS)
$(INSTALLDIRS):
	$(Q)$(MAKE) -C $(@:install-%=%) install-tests

CLEANDIRS = $(SUBDIRS:%=clean-%)

clean: $(CLEANDIRS)
$(CLEANDIRS):
	$(Q)$(MAKE) -C $(@:clean-%=%) clean >/dev/null

.PHONY: all clean $(SUBDIRS) $(CLEANDIRS) $(INSTALLDIRS)

