/*
 * Boot device shim ensures that DSM assigns a proper /dev/ device to our USB stick
 *
 * WHY IS THIS NEEDED?
 * In short the DSM has multiple types of SCSI devices (boot device, USB drive, eSATA drive etc). The boot device is
 * always mounted to /dev/synoboot (with respective partitions at /dev/synobootN). The determination is made using VID &
 * PID of the USB device. During normal operation both of them need to equal 0xf400 to be considered a boot device.
 * In a special "mfg" mode the installation is forced with 0xf401 ids instead.
 *
 *
 * HOW IT WORKS?
 * In order to dynamically change VID & PID of a USB device we need to modify device descriptor just after the device is
 * detected by the USB subsystem. However it has to be done before the device is picked up by the SCSI subsystem, which
 * is responsible for creating /dev/xxx entries.
 * Since the assumption is that the USB stick is present from boot the sequence of events needs to look like that:
 *  0. Kernel starts
 *  1. This LKM is loaded
 *  2. USB subsystem loads
 *  3. Drive is detected
 *  4. This LKM changes VID+PID
 *  5. SCSI subsystem detects the device and creates a /dev/... node for it
 *
 *  This poses several problems. First this module must load before USB subsystem. Then to get the device quicker than
 *  SCSI subsystem can a notification receiver is set up. However we need to wait to do this after the usbcore actually
 *  loads. To make sure it's the case a kernel module watcher is used. That's why symbols from usbcore are loaded
 *  dynamically, as at the moment of this LKM insertion they aren't available. In case usbcore is loaded before this LKM
 *  VID+PID change may not be effective (this scenario is supported pretty much for debugging only).
 *  This sequence is rather time sensitive. It shouldn't fail on any modern multicore system.
 *
 * SOURCES
 *  - Synology's kernel GPL source -> drivers/scsi/sd.c, search for "IS_SYNO_USBBOOT_ID_"
 *  - https://0xax.gitbooks.io/linux-insides/content/Concepts/linux-cpu-4.html
 *  - https://lwn.net/Articles/160501/
 */
#include "boot_device_shim.h"

#include "../common.h"
#include "../config/runtime_config.h"
#include "../internal/call_protected.h" //dynamically calling usb_* functions
#include <linux/notifier.h>
#include <linux/usb.h>
#include <linux/module.h> //struct module


#define SBOOT_RET_VID 0xf400 //Retail boot drive VID
#define SBOOT_RET_PID 0xf400 //Retail boot drive PID
#define SBOOT_MFG_VID 0xf401 //Force-reinstall boot drive VID
#define SBOOT_MFG_PID 0xf401 //Force-reinstall boot drive PID

static bool module_notify_registered = false;
static bool device_notify_registered = false;
static bool device_mapped = false;
static const struct boot_media *boot_media;
static const bool *mfg_mode;

/**
 * Responds to USB devices being added/removed
 */
static int device_notifier_handler(struct notifier_block *b, unsigned long event, void *data)
{
    struct usb_device *device = (struct usb_device*)data;

    if (event == USB_DEVICE_ADD) {
        //TODO: Can we even check if it matched mass storage here... (bInterfaceClass == USB_CLASS_MASS_STORAGE)
        if (boot_media->vid == VID_PID_EMPTY || boot_media->pid == VID_PID_EMPTY) {
            pr_loc_wrn("Your boot device VID and/or PID is not set - using device found <vid=%04x, pid=%04x>",
                       device->descriptor.idVendor, device->descriptor.idProduct);
        } else if (device->descriptor.idVendor != boot_media->vid || device->descriptor.idProduct != boot_media->pid) {
            pr_loc_dbg("Found new device <vid=%04x, pid=%04x> - didn't match expected <vid=%04x, pid=%04x>",
                       device->descriptor.idVendor, device->descriptor.idProduct, boot_media->vid, boot_media->pid);

            return NOTIFY_OK;
        }

        //This will happen especially when VID+PID weren't set and two USB devices were detected
        if (device_mapped) {
            pr_loc_wrn("Boot device was already shimmed but a new matching device appeared again - "
                       "this may produce unpredictable outcomes!");
        }

        if (*mfg_mode) {
            device->descriptor.idVendor = SBOOT_MFG_VID;
            device->descriptor.idProduct = SBOOT_MFG_PID;
        } else {
            device->descriptor.idVendor = SBOOT_RET_VID;
            device->descriptor.idProduct = SBOOT_RET_PID;
        }

        device_mapped = true;

        pr_loc_inf("Device <vid=%04x, pid=%04x> shimmed to <vid=%04x, pid=%04x>", boot_media->vid, boot_media->pid,
                   device->descriptor.idVendor, device->descriptor.idProduct);

        return NOTIFY_OK;
    }

    if (device_mapped && event == USB_DEVICE_REMOVE &&
        (device->descriptor.idVendor == SBOOT_MFG_VID || device->descriptor.idVendor == SBOOT_RET_VID) &&
        (device->descriptor.idProduct == SBOOT_MFG_PID || device->descriptor.idProduct == SBOOT_RET_PID)
       ) {
        pr_loc_wrn("Previously shimmed boot device disconnected!");
        device_mapped = false;

        return NOTIFY_OK;
    }

    return NOTIFY_OK;
}

static struct notifier_block device_notifier_block = {
        .notifier_call = device_notifier_handler,
        .priority = INT_MIN, //We need to be first
};
/**
 * Watches for USB events
 */
static void register_device_notifier(void)
{
    //This should never happen but there's never enough error checking.
    //Even if the module was already loaded register_device_notifier() should not be called twice before module is
    // unloaded and reloaded
    if (unlikely(device_notify_registered)) {
        pr_loc_bug("Device notify re-registration via %s w/o module unload (?!)", __FUNCTION__);
        return;
    }

    //This has to use dynamic calling to avoid being dependent on usbcore (since we need to load before usbcore)
    _usb_register_notify(&device_notifier_block); //has no return value

    device_notify_registered = true;
    pr_loc_dbg("Registered USB device notifier");
}

static void unregister_device_notifier(void)
{
    if (unlikely(!device_notify_registered)) {
        pr_loc_bug("%s called while notifier not registered", __FUNCTION__);
        return;
    }

    //This has to use dynamic calling to avoid being dependent on usbcore (since we need to load before usbcore)
    _usb_unregister_notify(&device_notifier_block); //has no return value
    module_notify_registered = false;
    pr_loc_dbg("Unregistered USB device notifier");
}

/**
 * Responds to "usbcore" [and others] load
 */
static int ubscore_notifier_handler(struct notifier_block * self, unsigned long state, void * data)
{
    struct module *mod = data;
    if (strcmp(mod->name, "usbcore") != 0)
        return NOTIFY_OK;

    if (state == MODULE_STATE_GOING) {
        //TODO: call unregister with some force flag?
        device_notify_registered = false;
        device_mapped = false;
        pr_loc_wrn("usbcore module unloaded - this should not happen normally");
        return NOTIFY_OK;
    }

    //This may need to be changed to MODULE_STATE_LIVE if MODULE_STATE_COMING is too early for device notification
    if (state != MODULE_STATE_LIVE)
        return NOTIFY_OK;

    pr_loc_dbg("usbcore registered, adding device watcher");
    register_device_notifier();

    return NOTIFY_OK;
}

static struct notifier_block usbcore_notifier_block = {
        .notifier_call = ubscore_notifier_handler
};
/**
 * Watches for "usbcore" module load
 */
static void register_usbcore_notifier(void)
{
    if (unlikely(module_notify_registered)) {
        pr_loc_bug("%s called while notifier already registered", __FUNCTION__);
        return;
    }

    if(unlikely(register_module_notifier(&usbcore_notifier_block) != 0)) {
        pr_loc_err("Failed to register module notifier"); //Currently it's impossible to happen... currently
        return;
    }

    module_notify_registered = true;
    pr_loc_dbg("Registered usbcore module notifier");

    //check if usbcore is MAYBE already loaded and give a warning + call register_device_notifier() manually
    // this state is FINE for debugging but IS NOT FINE for production use
    //We're using kernel_has_symbol() to not acquire module mutex needed for module checks
    if (kernel_has_symbol("usb_register_notify")) {
        pr_loc_wrn("usbcore module is already loaded (did you load this module too late?) "
                   "-> registering device notifier right away");
        register_device_notifier();
    }
}

static void unregister_usbcore_notifier(void)
{
    if (unlikely(!module_notify_registered)) { //unregister should be called first if so
        pr_loc_bug("%s called while notifier not registered", __FUNCTION__);
        return;
    }

    if(unlikely(unregister_module_notifier(&usbcore_notifier_block) != 0)) {
        pr_loc_err("Failed to unregister module notifier"); //Currently it's impossible to happen... currently
        return;
    }

    module_notify_registered = false;
    pr_loc_dbg("Unregistered usbcore module notifier");
}

void register_boot_shim(const struct boot_media *real_boot_device, const bool *run_mfg_mode)
{
    boot_media = real_boot_device;
    mfg_mode = run_mfg_mode;

    register_usbcore_notifier(); //it will register device notifier when module loads

    pr_loc_inf("Boot shim registered");
}

void unregister_boot_shim(void)
{
    unregister_usbcore_notifier();
    unregister_device_notifier();

    pr_loc_inf("Boot shim unregistered");
}