MODULE_NAME := alcor
SRC_FILES := misc.o npt.o mem.o main.o asm.o

ifneq ($(KERNELRELEASE),)
    obj-m := $(MODULE_NAME).o
    $(MODULE_NAME)-y := $(SRC_FILES)
    ccflags-y := -g -DDEBUG -Wno-declaration-after-statement
else
    KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
    PWD := $(shell pwd)

.PHONY: all clean load unload info

all:
	@echo "  BUILD   $(MODULE_NAME) against $(KERNEL_DIR)"
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	@echo "  CLEAN   $(MODULE_NAME)"
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	@rm -rf .cache

load: all
	@echo "  LOAD    $(MODULE_NAME).ko"
	@sudo insmod $(MODULE_NAME).ko

unload:
	@echo "  UNLOAD  $(MODULE_NAME)"
	-@sudo rmmod $(MODULE_NAME) 2>/dev/null || true

info: all
	modinfo $(MODULE_NAME).ko

endif
