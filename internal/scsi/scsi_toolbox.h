#ifndef REDPILL_SCSI_TOOLBOX_H
#define REDPILL_SCSI_TOOLBOX_H

#include <linux/types.h> //bool

typedef struct device device;
typedef struct scsi_device scsi_device;
typedef int (on_scsi_device_cb)(struct scsi_device *sdp);

#define SCSI_DRV_NAME "sd" //useful for triggering watchers
//To use this one import intercept_driver_register.h header (it's not imported here to avoid pollution)
#define watch_scsi_driver_register(callback, event_mask) \
    watch_driver_register(SCSI_DRV_NAME, (callback), (event_mask))

#define IS_SCSI_DRIVER_ERROR(state) (unlikely((state) < 0))
typedef enum {
    SCSI_DRV_NOT_LOADED = 0,
    SCSI_DRV_LOADED = 1,
} scsi_driver_state;

/**
 * From the kernel's pov SCSI devices include SCSI hosts, "leaf" devices, and others - this filters real SCSI devices
 *
 * This is simply an alias for scsi_is_sdev_device() which is more descriptive for people who aren't SCSI wizards.
 *
 * @param dev struct device*
 */
#define is_scsi_leaf(dev) scsi_is_sdev_device(dev)

/**
 * Attempts to read capacity of a device assuming reasonably modern pathway
 *
 * This function (along with scsi_read_cap{10|16}) is loosely based on drivers/scsi/sd.c:sd_read_capacity(). However,
 * this method cuts some corners to be faster as we're expecting rather modern hardware. Additionally, functions from
 * sd.c cannot be used as they're static. Even that some of them can be called using kallsyms they aren't stateless and
 * will cause a KP later on (as they modify the device passed to them).
 * Thus this function should be seen as a way to quickly estimate (as it reports full mebibytes rounded down) the
 * capacity without causing side effects.
 *
 * @param sdp
 * @return capacity in full mebibytes, or -E on error
 */
long long opportunistic_read_capacity(struct scsi_device *sdp);

/**
 * Checks if a SCSI device is a SCSI-complain disk (e.g. SATA, SAS, iSCSI etc)
 *
 * To be 101% sure and proper you should probably call is_scsi_leaf() first
 */
bool is_scsi_disk(struct scsi_device *sdp);

/**
 * Checks if a given generic device is an SCSI disk connected to a SATA port/host controller
 *
 * Every SATA disk, by definition, will also be an SCSI disk (as SATA is a connector carrying SCSI commands)
 */
bool is_sata_disk(struct device *dev);

/**
 * Triggers a re-probe of SCSI leaf device by forcefully "unplugging" and "replugging" the device
 *
 * WARNING: be careful what are you doing - this method is no different than yanking a power cable from a device, so if
 * you do that with a disk which is used data loss may occur!
 *
 * @return 0 on success, -E on error
 */
int scsi_force_replug(scsi_device *sdp);

/**
 * Locates & returns SCSI driver structure if loaded
 *
 * @return driver struct on success, NULL if driver is not loaded, ERR_PTR(-E) on error
 */
struct device_driver *find_scsi_driver(void);

/**
 * Checks if SCSI driver is loaded or not
 *
 * This function is useful to make a decision whether to just watch for new devices or watch for new ones + scan
 * existing ones. You cannot just scan blindly as this will cause an error.
 *
 * @return 0 if not loaded, 1 if loaded, -E on error; see scsi_driver_state enum for constants
 */
int is_scsi_driver_loaded(void);

/**
 * Traverses list of all SCSI devices and calls the callback with every leaf/terminal device found
 *
 * @return 0 on success, -E on failure. -ENXIO is reserved to always mean that the driver is not loaded
 */
int for_each_scsi_leaf(on_scsi_device_cb *cb);

/**
 * Traverses list of all SCSI devices and calls the callback with every SCSCI-complaint disk found
 *
 * @return 0 on success, -E on failure. -ENXIO is reserved to always mean that the driver is not loaded
 */
int for_each_scsi_disk(on_scsi_device_cb *cb);

#endif //REDPILL_SCSI_TOOLBOX_H
