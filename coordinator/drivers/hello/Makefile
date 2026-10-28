KDIR := /home/rk/rk356x-linux/kernel
PWD  := $(shell pwd)

obj-m := hello.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean