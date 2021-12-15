# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021
include ../Makefile.miniconfig

BIN=memcpy
LIB=

all: $(BIN)

$(BIN): $(BIN).c
ifdef CORESIGHT
ifeq ($(ARCH),arm64)
	$(Q)$(CC) $(BIN).c -o $(BIN) $(LIB)
endif
endif

install-tests: all
ifdef CORESIGHT
ifeq ($(ARCH),arm64)
	$(call QUIET_INSTALL, tests) \
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/tests/shell/tools/$(BIN)'; \
		$(INSTALL) $(BIN) '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/tests/shell/tools/$(BIN)/$(BIN)'
endif
endif

clean:
	$(Q)$(RM) -f $(BIN)

.PHONY: all clean install-tests
