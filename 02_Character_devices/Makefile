KERNAL_DIR := /home/noah/imx6ull/bsp/kernel/linux-imx
CROSS_COMPILE := /opt/gcc-arm-10.3-2021.07/bin/arm-none-linux-gnueabihf-
TFTP_DIR := /home/noah/tftp/
PWD := $(shell pwd)
obj-m := chrdevbase.o

modules:
	$(MAKE) -C $(KERNAL_DIR) M=$(PWD) modules
	$(CROSS_COMPILE)gcc chrdevbaseApp.c -o chrdevbaseApp
	@make clear
	cp chrdevbase.ko chrdevbaseApp $(TFTP_DIR) -f

clear:
	@rm -f *.o *.cmd *.mod *.mod.c
	@rm -rf *~ core .depend .tmp_versions Module.symvers modules.order -f
	@rm -f .*ko.cmd .*.o.cmd .*.o.d
	@rm -f *.unsigned

clean:
	@rm -f *.ko


