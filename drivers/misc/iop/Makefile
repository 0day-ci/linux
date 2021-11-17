#
# Makefile for the IOP module drivers.
#

	# call from kernel build system

	obj-$(CONFIG_SUNPLUS_IOP) += sunplus_iop.o
	obj-$(CONFIG_SUNPLUS_IOP) += hal_iop.o
	obj-$(CONFIG_SUNPLUS_IOP) += iopnormal.o
	obj-$(CONFIG_SUNPLUS_IOP) += iopstandby.o

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions