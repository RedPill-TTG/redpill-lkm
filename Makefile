LINUX_SRC := "./linux-3.10.x-bromolow-25426"
PWD   := $(shell pwd)
SRCS   = internal/call_protected.c internal/override_symbol.c internal/stealth.c \
		 config/cmdline_delegate.c config/runtime_config.c \
		 shim/boot_device_shim.c shim/bios/bios_shims_collection.c shim/bios_shim.c shim/block_fw_update_shim.c \
	     redpill_main.c
OBJS   = $(SRCS:.c=.o)
#this module name CAN NEVER be the same as the main file (or it will get weird ;)) and the main file has to be included
# in object file. So here we say the module file(s) which will create .ko(s) is "redpill.o" and that other objects which
# must be linked (redpill-objs variable)
obj-m += redpill.o
redpill-objs := $(OBJS)
ccflags-y := -std=gnu99 -fgnu89-inline -Wno-declaration-after-statement -g -fno-inline

all:
	make -C $(LINUX_SRC) M=$(PWD) modules
clean:
	make -C $(LINUX_SRC) M=$(PWD) clean
