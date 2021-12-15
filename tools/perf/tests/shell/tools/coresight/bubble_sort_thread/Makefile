# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021
include ../Makefile.miniconfig

BIN=bubble_sort_thread
LIB=-pthread

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
		$(INSTALL) $(BIN) '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/tests/shell/tools/$(BIN)/$(BIN)'; \
		$(INSTALL) random_array.txt '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/tests/shell/tools/$(BIN)/random_array.txt'; \
		$(INSTALL) small_array.txt '$(DESTDIR_SQ)$(perfexec_instdir_SQ)/tests/shell/tools/$(BIN)/small_array.txt'
endif
endif

clean:
	$(Q)$(RM) -f $(BIN)

.PHONY: all clean install-tests
