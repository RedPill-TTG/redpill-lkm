/**
 * Implements shimming USB device to look like an embedded USB device supported by the syno kernel
 * If you didn't read the docs for shim/boot_device_shim.c go there and read it first!
 *
 * HOW THE KERNEL ASSIGNS SYNOBOOT TYPE?
 * The determination of what is or isn't the correct synoboot device for USBs is made using VID & PID of the device.
 * During normal operation both of them need to equal 0xf400 to be considered a boot device. In a special "mfg" mode the
 * installation is forced with 0xf401 ids instead.
 *
 * HOW THIS SHIM MATCHES DEVICE TO SHIM?
 * The decision is made based on "struct boot_media" (derived from boot config) passed to the register method:
 *  - if vid/pid combo is set (i.e. not VID_PID_EMPTY) it must match the newly detected device
 *  - if vid/pid is not set (i.e. VID_PID_EMPTY) the first device is used (NOT recommended unless you don't use USB)
 *  - if a second device matching any of the criteria above appears a warning is emitted and device is ignored
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
 * References
 *  - Synology's kernel GPL source -> drivers/scsi/sd.c, search for "IS_SYNO_USBBOOT_ID_"
 *  - https://0xax.gitbooks.io/linux-insides/content/Concepts/linux-cpu-4.html
 *  - https://lwn.net/Articles/160501/
 */
#include "usb_boot_shim.h"
#include "boot_shim_base.h" //set_shimmed_boot_dev(), get_shimmed_boot_dev(), usb_shim_as_boot_dev()
#include "../shim_base.h" //shim_*
#include "../../common.h"
#include "../../config/runtime_config.h" //struct boot_device & consts
#include "../../internal/helper/symbol_helper.h" //kernel_has_symbol()
#include "../../internal/call_protected.h" //dynamically calling usb_* functions
#include <linux/notifier.h>
#include <linux/usb.h>
#include <linux/module.h> //struct module

#define SHIM_NAME "USB boot device"

static bool module_notify_registered = false;
static bool device_notify_registered = false;
static const struct boot_media *boot_media = NULL; //passed to usb_shim_as_boot_dev()

/**
 * Responds to USB devices being added/removed
 */
static int device_notifier_handler(struct notifier_block *b, unsigned long event, void *data)
{
    struct usb_device *device = (struct usb_device*)data;
    struct usb_device *prev_device = get_shimmed_boot_dev();

    if (event == USB_DEVICE_ADD) {
        //TODO: Can we even check if it matched mass storage here... (bInterfaceClass == USB_CLASS_MASS_STORAGE)
        if (boot_media->vid == VID_PID_EMPTY || boot_media->pid == VID_PID_EMPTY) {
            pr_loc_wrn("Your boot device VID and/or PID is not set - "
                       "using device found <vid=%04x, pid=%04x> (prev_shimmed=%d)",
                       device->descriptor.idVendor, device->descriptor.idProduct, prev_device ? 1:0);
        } else if (device->descriptor.idVendor != boot_media->vid || device->descriptor.idProduct != boot_media->pid) {
            pr_loc_dbg("Found new device <vid=%04x, pid=%04x> - "
                       "didn't match expected <vid=%04x, pid=%04x> (prev_shimmed=%d)",
                       device->descriptor.idVendor, device->descriptor.idProduct, boot_media->vid, boot_media->pid,
                       prev_device ? 1:0);

            return NOTIFY_OK;
        }

        //This will happen especially when VID+PID weren't set and two USB devices were detected
        if (prev_device) {
            pr_loc_wrn("Boot device was already shimmed but a new matching device appeared again - "
                       "this may produce unpredictable outcomes! Ignoring - check your hardware");
            return NOTIFY_OK;
        }

        usb_shim_as_boot_dev(boot_media, device);
        set_shimmed_boot_dev(device);

        pr_loc_inf("Device <vid=%04x, pid=%04x> shimmed to <vid=%04x, pid=%04x>", boot_media->vid, boot_media->pid,
                   device->descriptor.idVendor, device->descriptor.idProduct);

        return NOTIFY_OK;
    }


    if (prev_device && event == USB_DEVICE_REMOVE && device == prev_device) {
        pr_loc_wrn("Previously shimmed boot device gone away");
        reset_shimmed_boot_dev();
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

static int unregister_device_notifier(void)
{
    if (unlikely(!device_notify_registered)) {
        pr_loc_bug("%s called while notifier not registered", __FUNCTION__);
        return -ENOENT;
    }

    //This has to use dynamic calling to avoid being dependent on usbcore (since we need to load before usbcore)
    _usb_unregister_notify(&device_notifier_block); //has no return value
    device_notify_registered = false;
    pr_loc_dbg("Unregistered USB device notifier");

    return 0;
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
        reset_shimmed_boot_dev();
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
static int register_usbcore_notifier(void)
{
    int error = 0;

    if (unlikely(module_notify_registered)) {
        pr_loc_bug("%s called while notifier already registered", __FUNCTION__);
        return 0; //technically it's not an error
    }

    error = register_module_notifier(&usbcore_notifier_block);
    if(unlikely(error != 0)) {
        pr_loc_err("Failed to register module notifier"); //Currently it's impossible to happen... currently
        return error;
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

    return error;
}

static int unregister_usbcore_notifier(void)
{
    if (unlikely(!module_notify_registered)) { //unregister should be called first if so
        pr_loc_bug("%s called while notifier not registered", __FUNCTION__);
        return -ENOENT;
    }

    int out = unregister_module_notifier(&usbcore_notifier_block);
    if(unlikely(out != 0)) {
        pr_loc_err("Failed to unregister module notifier"); //Currently it's impossible to happen... currently
        return out;
    }

    module_notify_registered = false;
    pr_loc_dbg("Unregistered usbcore module notifier");

    return 0;
}

int register_usb_boot_shim(const struct boot_media *boot_dev_config)
{
    shim_reg_in();

    if (unlikely(boot_dev_config->type != BOOT_MEDIA_USB)) {
        pr_loc_bug("%s doesn't support device type %d", __FUNCTION__, boot_dev_config->type);
        return -EINVAL;
    }

    if (unlikely(boot_media)) {
        pr_loc_bug("USB boot shim is already registered");
        return -EEXIST;
    }

    boot_media = boot_dev_config;

    int out = register_usbcore_notifier(); //it will register device notifier when module loads
    if (out != 0)
        return out;

    shim_reg_ok();
    return out;
}

int unregister_usb_boot_shim(void)
{
    shim_ureg_in();

    if (unlikely(!boot_media)) {
        pr_loc_bug("USB boot shim is not registered");
        return -ENOENT;
    }

    int out = 0;
    if (
            (out = unregister_usbcore_notifier()) != 0
         || (out = unregister_device_notifier()) != 0
    )
        return out;

    boot_media = NULL;

    shim_ureg_ok();
    return out;
}
