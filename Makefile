PWD   := $(shell pwd)

ifeq ($(LINUX_SRC),)
LINUX_SRC := "$(PWD)/../linux-3.10.x-bromolow-25426"
endif

SRCS-$(DBG_EXECVE) += debug/debug_execve.c
ccflags-$(DBG_EXECVE) += -DRPDBG_EXECVE
SRCS-y  += compat/string_compat.c \
		   \
		   internal/helper/math_helper.c internal/helper/memory_helper.c internal/helper/symbol_helper.c \
		   internal/scsi/scsi_toolbox.c internal/scsi/scsi_notifier_list.c internal/scsi/scsi_notifier.c \
		   internal/override_symbol.c internal/intercept_execve.c internal/call_protected.c \
		   internal/intercept_driver_register.c internal/stealth/sanitize_cmdline.c internal/stealth.c \
		   internal/virtual_pci.c internal/uart/uart_swapper.c internal/uart/vuart_virtual_irq.c \
		   internal/uart/virtual_uart.c internal/ioscheduler_fixer.c \
		   \
		   config/cmdline_delegate.c config/runtime_config.c \
		   \
		   shim/boot_dev/boot_shim_base.c shim/boot_dev/usb_boot_shim.c shim/boot_dev/fake_sata_boot_shim.c \
		   shim/boot_dev/native_sata_boot_shim.c shim/boot_device_shim.c \
		   \
		   shim/storage/smart_shim.c shim/storage/virtio_storage_shim.c \
		   shim/bios/bios_shims_collection.c shim/bios/rtc_proxy.c shim/bios_shim.c shim/block_fw_update_shim.c \
		   shim/disable_exectutables.c shim/pci_shim.c shim/pmu_shim.c shim/uart_fixer.c \
		   \
	       redpill_main.c
OBJS   = $(SRCS-y:.c=.o)
#this module name CAN NEVER be the same as the main file (or it will get weird ;)) and the main file has to be included
# in object file. So here we say the module file(s) which will create .ko(s) is "redpill.o" and that other objects which
# must be linked (redpill-objs variable)
obj-m += redpill.o
redpill-objs := $(OBJS)
ccflags-y += -std=gnu99 -fgnu89-inline -Wno-declaration-after-statement
ccflags-y += -I$(src)/compat/toolkit/include

ifndef RP_VERSION_POSTFIX
RP_VERSION_POSTFIX := $(shell git rev-parse --is-inside-work-tree 1>/dev/null 2>/dev/null && echo -n "git-" && git log -1 --pretty='%h' 2>/dev/null || echo "???")
endif
ccflags-y += -DRP_VERSION_POSTFIX="\"$(RP_VERSION_POSTFIX)\""

# Optimization settings per-target. Since LKM makefiles are evaluated twice (first with the specified target and second
# time with target "modules") we need to set the custom target variable during first parsing and based on that variable
# set additional CC-flags when the makefile is parsed for the second time
ifdef RP_MODULE_TARGET
ccflags-dev = -g -fno-inline -DDEBUG
ccflags-test = -O3
ccflags-prod = -O3
ccflags-y += -DRP_MODULE_TARGET_VER=${RP_MODULE_TARGET_VER} # this is assumed to be defined when target is specified

$(info RP-TARGET SPECIFIED AS ${RP_MODULE_TARGET} v${RP_MODULE_TARGET_VER})

# stealth mode can always be overridden but there are sane per-target defaults (see above)
ifneq ($(STEALTH_MODE),)
$(info STEATLH MODE OVERRIDE: ${STEALTH_MODE})
ccflags-y += -DSTEALTH_MODE=$(STEALTH_MODE)
else
ccflags-dev += -DSTEALTH_MODE=1
ccflags-test += -DSTEALTH_MODE=2
ccflags-prod += -DSTEALTH_MODE=3
endif

ccflags-y += ${ccflags-${RP_MODULE_TARGET}}
else
# during the first read of the makefile we don't get the RP_MODULE_TARGET - if for some reason we didn't get it during
# the actual build phase it should explode (and it will if an unknown GCC flag is specified). We cannot sue makefile
# error here as we don't know if the file is parsed for the first time or the second time. Just Kbuild peculiarities ;)
ccflags-y = --bogus-flag-which-should-not-be-called-NO_RP_MODULE_TARGER_SPECIFIED
endif

# this MUST be last after all other options to force GNU89 for the file being a workaround for GCC bug #275674
# see internal/scsi/scsi_notifier_list.h for detailed explanation
CFLAGS_scsi_notifier_list.o += -std=gnu89

# do NOT move this target - make <3.80 doesn't have a way to specify default target and takes the first one found
default_error:
	$(error You need to specify one of the following targets: dev-v6, dev-v7, test-v6, test-v7, prod-v6, prod-v7, clean)

# All v6 targets
dev-v6: # kernel running in v6.2+ OS, all symbols included, debug messages included
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) RP_MODULE_TARGET="dev" RP_MODULE_TARGET_VER="6" modules
test-v6: # kernel running in v6.2+ OS, fully stripped with only warning & above (no debugs or info)
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) RP_MODULE_TARGET="test" RP_MODULE_TARGET_VER="6" modules
prod-v6: # kernel running in v6.2+ OS, fully stripped with no debug messages
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) RP_MODULE_TARGET="prod" RP_MODULE_TARGET_VER="6" modules

# All v7 targets
dev-v7: # kernel running in v6.2+ OS, all symbols included, debug messages included
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) RP_MODULE_TARGET="dev" RP_MODULE_TARGET_VER="7" modules
test-v7: # kernel running in v6.2+ OS, fully stripped with only warning & above (no debugs or info)
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) RP_MODULE_TARGET="test" RP_MODULE_TARGET_VER="7" modules
prod-v7: # kernel running in v6.2+ OS, fully stripped with no debug messages
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) RP_MODULE_TARGET="prod" RP_MODULE_TARGET_VER="7" modules

clean:
	$(MAKE) -C $(LINUX_SRC) M=$(PWD) clean
