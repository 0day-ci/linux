# SPDX-License-Identifier: GPL-2.0+

include ../../Makefile.config

TOOL = pfru
EXTRA_INSTALL = install-man
EXTRA_UNINSTALL = uninstall-man

CFLAGS += -Wall -O2
CFLAGS += -DPFRU_HEADER='"../../../../../include/uapi/linux/pfru.h"'
LDFLAGS += -luuid

TOOL_OBJS = \
	pfru.o

include ../../Makefile.rules

install-man: $(srctree)/man/pfru.8
	$(ECHO) "  INST    " pfru.8
	$(QUIET) $(INSTALL_DATA) -D $< $(DESTDIR)$(mandir)/man8/pfru.8
uninstall-man:
	$(ECHO) "  UNINST  " pfru.8
	$(QUIET) rm -f $(DESTDIR)$(mandir)/man8/pfru.8
