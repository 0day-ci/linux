#
# Makefile for csky-specific math emulate.
#

ccflags-y := -w

obj-$(CONFIG_CPU_HAS_MATHEMU)		+= math-emu.o

ifneq ($(CONFIG_CPU_CK860),y)
math-emu-y			+= math.o fp810.o
else
math-emu-y			+= math.o fp860.o
endif
