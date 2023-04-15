KERNAL_DIR ?= /lib/modules/$(shell uname -r)/build
PWD :=$(shell pwd)
appname += kernel_hello
obj-m := $(appname).o

modules:
	$(MAKE) -C $(KERNAL_DIR) M=$(PWD) modules
	@make clear
clear:
	@rm -f *.o *.cmd *.mod *.mod.c
	@rm -rf *~ core .depend .tmp_versions Module.symvers modules.order -f
	@rm -f .*ko.cmd .*.o.cmd .*.o.d
	@rm -f *.unsigned

clean:
	@rm -f *.ko
