#ifndef REDPILL_BOOT_SHIM_BASE_H
#define REDPILL_BOOT_SHIM_BASE_H

#include <linux/types.h> //bool

struct boot_media;
struct usb_device;
struct scsi_device;

/**
 * Modify given USB device instance to conform to syno kernel boot device specification
 *
 * This function takes into consideration the boot_media configuration regarding mfg vs retail mode and will change the
 * device descriptors accordingly. It is safe to call this function multiple times on the same object.
 *
 * @param boot_dev_config Configuration to determine boot mode
 * @param usb_device Device to change
 */
void usb_shim_as_boot_dev(const struct boot_media *boot_dev_config, struct usb_device *usb_device);

/**
 * Save a free-form pointer into a global storage to mark boot device as shimmed
 *
 * Other subsystems can determine if the boot device has been shimmed by calling get_shimmed_boot_dev(). However, the
 * data passed to this function is opaque by design and only makes sense to the submodule which originally set it.
 *
 * @param private_data Any non-null pointer or value castable to a pointer type (e.g. unsigned long number)
 */
void set_shimmed_boot_dev(void *private_data);

/**
 * Shortcut to remove previously marked as shimmed boot device. It is an equivalent of simply calling set with NULL ptr.
 */
#define reset_shimmed_boot_dev() set_shimmed_boot_dev(NULL);

/**
 * Gets shimmed boot device private data (if any)
 *
 * The caller should not try to interpret the value returned beyond NULL vs. non-NULL, unless the caller is the original
 * submodule which set the value using set_shimmed_boot_dev().
 *
 * @return non-NULL pointer if device has been shimmed or NULL ptr if it wasn't
 */
void *get_shimmed_boot_dev(void);

/**
 * Checks if a given SCSI disk can become a boot device
 *
 * To fully understand the rules and intricacies of how it is used in context you should read the file comment for the
 * native SATA DOM shim in shim/boot_dev/sata_boot_shim.c
 *
 * @param boot_dev_config User-controllable configuration with a threshold for considering an SCSI disk a boot device
 * @param sdp SCSI device which ideally should be an SCSI disk (as passing any other ones doesn't make sense)
 */
bool scsi_is_boot_dev_target(const struct boot_media *boot_dev_config, struct scsi_device *sdp);

#endif //REDPILL_BOOT_SHIM_BASE_H
