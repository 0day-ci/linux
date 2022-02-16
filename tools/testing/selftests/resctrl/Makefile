CFLAGS += -g -Wall -O2 -D_FORTIFY_SOURCE=2

TEST_GEN_PROGS := resctrl_tests
SRCS := $(wildcard *.c)

all: $(TEST_GEN_PROGS)

$(TEST_GEN_PROGS): $(SRCS)
include ../lib.mk
