#ifndef REDPILL_USB_BOOT_SHIM_H
#define REDPILL_USB_BOOT_SHIM_H

struct boot_media;
int register_usb_boot_shim(const struct boot_media *boot_dev_config);
int unregister_usb_boot_shim(void);

#endif //REDPILL_USB_BOOT_SHIM_H
