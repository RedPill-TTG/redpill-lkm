#include "boot_shim_base.h"
#include "../../common.h"
#include "../../config/runtime_config.h" //struct boot_media
#include <linux/usb.h> //struct usb_device

//Definition of known VID/PIDs for USB-based shims
#define SBOOT_RET_VID 0xf400 //Retail boot drive VID
#define SBOOT_RET_PID 0xf400 //Retail boot drive PID
#define SBOOT_MFG_VID 0xf401 //Force-reinstall boot drive VID
#define SBOOT_MFG_PID 0xf401 //Force-reinstall boot drive PID

void *mapped_shim_data = NULL;

void set_shimmed_boot_dev(void *private_data)
{
    mapped_shim_data = private_data;
}

void *get_shimmed_boot_dev(void)
{
    return mapped_shim_data;
}

void usb_shim_as_boot_dev(const struct boot_media *boot_dev_config, struct usb_device *usb_device)
{
    if (boot_dev_config->mfg_mode) {
        usb_device->descriptor.idVendor = cpu_to_le16(SBOOT_MFG_VID);
        usb_device->descriptor.idProduct = cpu_to_le16(SBOOT_MFG_PID);
    } else {
        usb_device->descriptor.idVendor = cpu_to_le16(SBOOT_RET_VID);
        usb_device->descriptor.idProduct = cpu_to_le16(SBOOT_RET_PID);
    }
}