Kernel_Dir := /home/noah/imx6ull/bsp/kernel/linux-imx/
CROSS_COMPILE := /opt/gcc-arm-10.3-2021.07/bin/arm-none-linux-gnueabihf-
TFTP_DIR := /home/noah/tftp/
PWD := $(shell pwd)
obj-m := i2c_sht20.o

module:
	$(MAKE) -C $(Kernel_Dir) M=$(PWD) modules
	$(CROSS_COMPILE)gcc i2c_sys_App.c -o i2c_sys_App
	@make clear
	cp i2c_sht20.ko i2c_sys_App $(TFTP_DIR) -f

clear:
	@rm -f *.o *.cmd *.mod *.mod.c
	@rm -rf *~ core .depend .tmp_versions Module.symvers modules.order -f
	@rm -f .*ko.cmd .*.o.cmd .*.o.d
	@rm -f *.unsigned .*.*.cmd

clean:
	@rm -f *.ko






