# SPDX-License-Identifier: GPL-2.0+

CFLAGS += -Wall -O2
CFLAGS += -DPFRU_HEADER='"../../../../include/uapi/linux/pfru.h"'
BUILD_OUTPUT	:= $(CURDIR)

ifeq ("$(origin O)", "command line")
	BUILD_OUTPUT := $(O)
endif

pfru : pfru.c

%: %.c
	@mkdir -p $(BUILD_OUTPUT)
	$(CC) $(CFLAGS) $< -o $(BUILD_OUTPUT)/$@ $(LDFLAGS) -luuid

.PHONY : clean
clean :
	@rm -f $(BUILD_OUTPUT)/pfru

install : pfru
	install -d  $(DESTDIR)$(PREFIX)/bin
	install $(BUILD_OUTPUT)/pfru $(DESTDIR)$(PREFIX)/bin/pfru
	install -d  $(DESTDIR)$(PREFIX)/share/man/man8
	install -m 644 pfru.8 $(DESTDIR)$(PREFIX)/share/man/man8
