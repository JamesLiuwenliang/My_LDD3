ifneq ($(KERNELRELEASE),)
	obj-m := scull_main_03.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules



endif


clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.symvers modules.order
