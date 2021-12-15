# SPDX-License-Identifier: GPL-2.0-only
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021
include ../../../../../tools/scripts/Makefile.include
include ../../../../../tools/scripts/Makefile.arch
include ../../../../../tools/scripts/utilities.mak

SUBDIRS = \
	coresight

all: $(SUBDIRS)
$(SUBDIRS):
	$(Q)$(MAKE) -C $@

INSTALLDIRS = $(SUBDIRS:%=install-%)

install-tests: all $(INSTALLDIRS)
$(INSTALLDIRS):
	$(Q)$(MAKE) -C $(@:install-%=%) install-tests

CLEANDIRS = $(SUBDIRS:%=clean-%)

clean: $(CLEANDIRS)
$(CLEANDIRS):
	$(Q)$(MAKE) -C $(@:clean-%=%) O=$(OUTPUT) clean >/dev/null

.PHONY: all clean install-tests $(SUBDIRS) $(CLEANDIRS) $(INSTALLDIRS)
