# SPDX-License-Identifier: GPL-2.0

CFLAGS += -g -I../../../usr/include/ -I../../include/

libumcg.a: libumcg.o
	ar rc libumcg.a libumcg.o

libumcg.o: libumcg.c

clean :
	rm libumcg.a libumcg.o
