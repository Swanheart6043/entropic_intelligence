ifeq ($(KERNELRELEASE),)

KERNELDIR ?= /home/swanheart/codes/linux-3.14
ROOTFS ?= /home/swanheart/codes/rootfs
PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules INSTALL_MOD_PATH=$(ROOTFS) modules_install

clean:
	rm -rf  *.o  *.ko  .*.cmd  *.mod.*  modules.order  Module.symvers   .tmp_versions

else
obj-m += led.o

endif
