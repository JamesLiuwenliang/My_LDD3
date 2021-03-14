KERN_DIR = /usr/src/linux-headers-4.15.0-136-generic

all:
    make -C $(KERN_DIR) M=`pwd` modules
clean:
    make -C $(KERN_DIR) M=`pwd` modules clean
    rm -rf modules.order

obj-m := hellop.o
# ifeq ($(KERNELRELEASE),)
#     KERNELDIR ?= /lib/modules/$(shell uname -r)/build
#     # The current directory is passed to sub-makes as argument
#     PWD := $(shell pwd)

# modules:
# 	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

# else
# 	obj-m := hello.o


# endif