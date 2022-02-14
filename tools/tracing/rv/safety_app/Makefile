NAME	:=	safety_app
VERSION	:=	0.1

# From libtracefs:
# Makefiles suck: This macro sets a default value of $(2) for the
# variable named by $(1), unless the variable has been set by
# environment or command line. This is necessary for CC and AR
# because make sets default values, so the simpler ?= approach
# won't work as expected.
define allow-override
  $(if $(or $(findstring environment,$(origin $(1))),\
            $(findstring command line,$(origin $(1)))),,\
    $(eval $(1) = $(2)))
endef

# Allow setting CC and AR, or setting CROSS_COMPILE as a prefix.
$(call allow-override,CC,$(CROSS_COMPILE)gcc)
$(call allow-override,AR,$(CROSS_COMPILE)ar)
$(call allow-override,STRIP,$(CROSS_COMPILE)strip)
$(call allow-override,PKG_CONFIG,pkg-config)
$(call allow-override,LD_SO_CONF_PATH,/etc/ld.so.conf.d/)
$(call allow-override,LDCONFIG,ldconfig)

INSTALL	=	install
FOPTS	:=	-flto=auto -ffat-lto-objects -fexceptions -fstack-protector-strong \
		-fasynchronous-unwind-tables -fstack-clash-protection
WOPTS	:= 	-Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -Wno-maybe-uninitialized

TRACEFS_HEADERS	:= $$($(PKG_CONFIG) --cflags libtracefs)

CFLAGS	:=	-O -g -DVERSION=\"$(VERSION)\" $(FOPTS) $(WOPTS) $(TRACEFS_HEADERS)
LDFLAGS	:=	-ggdb
LIBS	:=	$$($(PKG_CONFIG) --libs libtracefs)
FILES	:=	Makefile
BINDIR	:=	/usr/bin

OBJ	:=	$(NAME).o

.PHONY:	all
all:	$(OBJ)
	$(CC) -o $(NAME) $(LDFLAGS) $(OBJ) $(LIBS)

.PHONY: install
install:
	$(INSTALL) -d -m 755 $(DESTDIR)$(BINDIR)
	$(INSTALL) $(NAME) -m 755 $(DESTDIR)$(BINDIR)
	$(STRIP) $(DESTDIR)$(BINDIR)/$(NAME)

.PHONY: clean
clean:
	@rm -rf *~ $(OBJ) $(NAME)
