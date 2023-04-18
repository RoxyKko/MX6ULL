KERNEL_DIR := /home/noah/imx6ull/bsp/kernel/linux-imx 
PWD :=$(shell pwd)
appname += add_sub
obj-m := $(appname).o
PRINC_INC = $(PWD)
EXTRA_CFLAGS += -I $(PRINC_INC) 

modules:
	$(MAKE) -I $(PRINC_INC) -C $(KERNEL_DIR) M=$(PWD) modules
	@make clear

clear:
	@rm -f *.o *.cmd *.mod *.mod.c
	@rm -rf *~ core .depend .tmp_versions  modules.order -f
	@rm -f .*ko.cmd .*.o.cmd .*.o.d
	@rm -f *.unsigned

clean:
	@rm -f *.ko
