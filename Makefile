ifeq ($(LINUX_SRC),)
LINUX_SRC := "./linux-3.10.x-bromolow-25426"
endif

PWD   := $(shell pwd)

SRCS-$(DBG_EXECVE) += debug/debug_execve.c
ccflags-$(DBG_EXECVE) += -DRPDBG_EXECVE

SRCS-y  += compat/string_compat.c \
		   internal/override_symbol.c internal/intercept_execve.c internal/call_protected.c \
		   internal/stealth/sanitize_cmdline.c internal/stealth.c internal/virtual_pci.c \
		   internal/uart/vuart_virtual_irq.c internal/uart/virtual_uart.c \
		   config/cmdline_delegate.c config/runtime_config.c \
		   shim/boot_device_shim.c shim/bios/rtc_proxy.c shim/bios/bios_shims_collection.c shim/bios_shim.c \
		   shim/block_fw_update_shim.c shim/disable_exectutables.c shim/pci_shim.c shim/uart_fixer.c \
	       redpill_main.c
OBJS   = $(SRCS-y:.c=.o)
#this module name CAN NEVER be the same as the main file (or it will get weird ;)) and the main file has to be included
# in object file. So here we say the module file(s) which will create .ko(s) is "redpill.o" and that other objects which
# must be linked (redpill-objs variable)
obj-m += redpill.o
redpill-objs := $(OBJS)
ccflags-y += -std=gnu99 -fgnu89-inline -Wno-declaration-after-statement -g -fno-inline

ifneq ($(STEALTH_MODE),)
ccflags-y += -DSTEALTH_MODE=$(STEALTH_MODE)
endif

all:
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) modules
clean:
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) clean
