/**
 * Boot device shim ensures that DSM assigns a proper /dev/ device to our USB stick or SATA DOM
 *
 * WHY IS THIS NEEDED?
 * In short the DSM has multiple types of SCSI devices (boot device, USB drive, eSATA drive etc). The boot device is
 * always mounted to /dev/synoboot (with respective partitions at /dev/synobootN). The determination what to place there
 * is made based on different factors depending on the type of device used to boot (see drivers/scsi/sd.c):
 *  1) Standard USB stick
 *      - it has to be connected via a real USB port (i.e. not a fake-usb-over-sata like ESXi tries to do)
 *      - it must have VID/PID combo of 0xf400/0xf400
 *      - allows for normal boot when OS is installed, or triggers OS install/repair screen
 *  2) Force-install USB stick
 *      - it has to be connected via a real USB port
 *      - it must have VID/PID combo of 0xf401/0xf401
 *      - always triggers OS install/repair screen
 *  3) SATA DOM (Disk-on-Module)
 *      - is a real SATA (i.e. not SCSI/iSCSI/VirtIO) device
 *      - has platform dependent vendor/model strings of CONFIG_SYNO_SATA_DOM_VENDOR/CONFIG_SYNO_SATA_DOM_MODEL
 *      - has platform dependent vendor/model strings of CONFIG_SYNO_SATA_DOM_VENDOR_SECOND_SRC/CONFIG_SYNO_SATA_DOM_MODEL_SECOND_SRC
 *      - SATA DOM *cannot* be used to force-reinstall (as there isn't an equivalent of USB's VID/PID of 0xf401/0xf401)
 *
 * There are other special ones (e.g. iSCSI) which aren't supported here. These only apply to small subset of platforms.
 *
 * HOW IT WORKS?
 * Depending on the runtime configuration this shim will either engage USB-based shim or SATA-based one. See respective
 * implementations in shim/boot_dev/.
 *
 * References:
 *  - See drivers/scsi/sd.c in Linux sources (especially sd_probe() method)
 */
#include "boot_device_shim.h"
#include "../common.h"
#include "../config/runtime_config.h"
#include "boot_dev/usb_boot_shim.h"
#include "boot_dev/sata_boot_shim.h"

#define BOOT_MEDIA_SHIM_NULL (-1)

static int registered_type = BOOT_MEDIA_SHIM_NULL;
int register_boot_shim(const struct boot_media *boot_dev_config)
{
    if (unlikely(registered_type != BOOT_MEDIA_SHIM_NULL)) {
        pr_loc_bug("Boot shim is already registered with type=%d", registered_type);
        return -EEXIST;
    }

    int out;
    switch (boot_dev_config->type) {
        case BOOT_MEDIA_USB:
            out = register_usb_boot_shim(boot_dev_config);
            break;
        case BOOT_MEDIA_SATA:
            out = register_sata_boot_shim(boot_dev_config);
            break;
        default:
            pr_loc_bug("Failed to %s - unknown type=%d", __FUNCTION__, boot_dev_config->type);
            return -EINVAL;
    }

    if (out != 0)
        return out; //individual shims should print what went wrong

    registered_type = boot_dev_config->type;
    pr_loc_inf("Boot shim registered (type=%d)", registered_type);
    return 0;
}

int unregister_boot_shim(void)
{
    int out;
    switch (registered_type) {
        case BOOT_MEDIA_USB:
            out = unregister_usb_boot_shim();
            break;
        case BOOT_MEDIA_SATA:
            out = unregister_sata_boot_shim();
            break;
        case BOOT_MEDIA_SHIM_NULL:
            pr_loc_bug("Boot shim is no registered");
            return -ENOENT;
        default: //that cannot happen unless register_boot_shim() is broken
            pr_loc_bug("Failed to %s - unknown type=%d", __FUNCTION__, registered_type);
            return -EINVAL;
    }

    if (out != 0)
        return out; //individual shims should print what went wrong

    pr_loc_inf("Boot shim unregistered (type=%d)", registered_type);
    registered_type = BOOT_MEDIA_SHIM_NULL;
    return 0;
}