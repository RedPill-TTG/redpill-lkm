/**
 * Implements shimming SATA device to look like a SATA DOM (Disk-on-Module) device supported by the syno kernel
 * If you didn't read the docs for shim/boot_device_shim.c go there and read it first!
 *
 * HOW THE KERNEL ASSIGNS SYNOBOOT TYPE?
 * The determination of what is or isn't the correct synoboot device for SATA is made using vendor and model *names*, as
 * standard SCSI/SATA don't have any VID/PID designation like USB or PCI.
 * Syno kernel uses different vendor/model names depending on the platform. They are taken from the kernel config option
 * pairs CONFIG_SYNO_SATA_DOM_VENDOR/CONFIG_SYNO_SATA_DOM_MODEL and CONFIG_SYNO_SATA_DOM_VENDOR_SECOND_SRC/
 * CONFIG_SYNO_SATA_DOM_MODEL_SECOND_SRC. This gives the following supported matrix at the time of writing:
 *   - vendor-name="SATADOM"  and model-name="TYPE D 3SE" (purley only)
 *   - vendor-name="SATADOM-" and model-name="TYPE D 3SE" (all except purley)
 *   - vendor-name="SATADOM"  and model-name="3SE"        (purley only)
 *   - vendor-name="SATADOM"  and model-name="D150SH"     (all other)
 *
 * HOW THIS SHIM MATCHES DEVICE TO SHIM?
 * The decision is made based on "struct boot_media" (derived from boot config) passed to the register method. The
 * only criterion used is the physical size of the disk. The *first* device which is smaller or equal to
 * boot_media->dom_size_mib will be shimmed. If a consecutive device matching this rule appears a warning will be
 * triggered.
 * This sounds quite unusual. We considered multiple options before going that route:
 *   - Unlike USB we cannot easily match SATA devices using any stable identifier so any VID/PID was out of the window
 *   - S/N sounds like a good candidate unless you realize hypervisors use the same one for all disks
 *   - Vendor/Model names cannot be edited by the user and hypervisors ust the same one for all disks
 *   - Host/Port location can change (and good luck updating it in the boot params every time)
 *   - The only stable factor seems to be size
 *
 * HOW IT WORKS FOR HOT PLUGGED DEVICES?
 * While the USB boot shim depends on a race condition (albeit a pretty stable one) there's no way to use the same
 * method for SATA, despite both of them using SCSI under the hood. This is because true SCSI/SATA devices are directly
 * supported by the drivers/scsi/sd.c which generates no events before the type is determined. Because of this we
 * decided to exploit the dynamic nature of Linux drivers subsystem. All drivers register their buses with the kernel
 * and are automatically informed by the kernel if something appears on these buses (either during boot or via hot plug)
 *
 * This module simply asks the kernel drivers subsystem for the driver registered for "sd" (SCSI) devices. Then it
 * replaces its trigger function pointer. Normally it points to drivers/scsi/sd.c:sd_probe() which "probes" and configs
 * the device. Our sd_probe_shim() first reads the capacity and if criteria are met (see section above) it replaces
 * the vendor & model names and passes the control to the real sd_probe(). If nothing matches it transparently calls
 * the real sd_probe().
 *
 * If you're debugging you can test it without restarting the whole SD by removing and re-adding device. For example for
 * "sd 6:0:0:0: [sdg] 630784 512-byte logical blocks: (322 MB/308 MiB)" you should do:
 *    echo 1 > /sys/block/sdg/device/delete             # change SDG to the correct device
 *    echo "0 0 0" > /sys/class/scsi_host/host6/scan    # host 6 is the same as "sd 6:..." notation in dmesg
 * Warning: rescans and delete hard-yanks the device from controller so DO NOT do this on a disk with important data!
 *
 * HOW IT WORKS FOR EXISTING DEVICES?
 * Unfortunately, our sd_probe() replacement is still a bit of a race condition. However, this time we're racing with
 * SCSI driver loading which usually isn't a module. Because of this we need to expect some (probably all) devices to be
 * already probed. We need to do essentially what's described above (with /sys) but from kernel space.
 * To avoid any crashes and possible data loss we are never touching disks which aren't SATA and matching the size
 * match criterion. In other words this shim will NOT yank a data drive from the system.
 *
 * WHAT IF THIS CODE LOADS BEFORE THE DRIVER?!
 * Despite the SCSI driver being one of the first things loaded by the kernel and something which almost everywhere is
 * baked into to kernel there's a way to load our module earlier (via ioscheduler). In such case we cannot even use
 * driver_find("sd", ...) as it will return NULL (since there's no driver for "sd" *yet*). In such case we can hook
 * "scsi_register_driver()" which is an exported symbol (==it will last) and keep it hooked until we find the
 * registration of "sd" driver in particular (as SCSI also handles CDROMs, USBs, iSCSI and others)
 *
 * THE FINAL PICTURE
 * Ok, it is pretty complex indeed. Here's the decision tree this submodule goes through:
 *  register_native_sata_boot_shim()
 *   => driver_find("sd", ...)
 *      ===FOUND===
 *        + shim sd_probe() to sd_probe_shim()
 *            <will shim vendor/model if appropriate for every newly plugged/re-plugged device>
 *        + probe_existing_devices()
 *            <iterate through all, find if any matches, if so disconnect it (which will prompt it to trigger sd_probe)>
 *      ===NOT FOUND===
 *        + override scsi_register_driver() [using start_scsi_register_driver_watcher()]
 *            <it will "wait" until the driver attempts to register>
 *           ===scsi_register_driver() called & drv->name is "sd"=== [see scsi_register_driver_shim()]
 *            + modify drv->probe to &sd_probe_shim
 *            + stop_scsi_register_driver_watcher()
 *               <we don't want to provide our own implementation of scsi_register_driver()>
 *            + call [now original] scsi_register_driver()
 *               <this will trigger sd_probe_shim() when disk is detected and shimming will happen; no need to call
 *                probe_existing_devices() here as the driver JUST registered>
 *
 * KNOWN LIMITATIONS
 * If you hot-unplug that SATA drive which is used for synoboot it will NOT be shimmed the next time you plug it without
 * rebooting. This is because we were lazy and didn't implement the removal shimming (as this behavior isn't defined
 * anyway with synoboot devices as they're not user-removable).
 *
 * This shim is only supported on kernels compiled with CONFIG_SYNO_BOOT_SATA_DOM enabled. Kernels built without that
 * option will never check for the vendor/model names and will never be considered SYNOBOOT.
 *
 * SOURCES
 *  - Synology's kernel GPL source -> drivers/scsi/sd.c, search for "gSynoBootSATADOM"
 *  - https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf
 */
#include "native_sata_boot_shim.h"
#include "../../common.h"
#include "../../config/runtime_config.h" //consts, NATIVE_SATA_DOM_SUPPORTED

#ifdef NATIVE_SATA_DOM_SUPPORTED
#include "boot_shim_base.h" //set_shimmed_boot_dev(), get_shimmed_boot_dev(), scsi_is_boot_dev_target()
#include "../shim_base.h" //shim_reg_*(), scsi_ureg_*()
#include "../../internal/call_protected.h" //scsi_scan_host_selected()
#include "../../internal/scsi/scsi_toolbox.h" //scsi_force_replug(), for_each_scsi_disk()
#include "../../internal/scsi/scsi_notifier.h" //watching for new devices to shim them as they appear
#include <scsi/scsi_device.h> //struct scsi_device

#define SHIM_NAME "native SATA DOM boot device"

static const struct boot_media *boot_dev_config = NULL; //passed to scsi_is_shim_target()

//Structure for watching for new devices (via SCSI notifier / scsi_notifier.c event system)
static int on_new_scsi_disk(struct notifier_block *self, unsigned long state, void *data);
static struct notifier_block scsi_disk_nb = {
    .notifier_call = on_new_scsi_disk,
    .priority = INT_MAX //We want to be LAST, after all other possible fixes has been already applied
};

/********************************************* Actual shimming routines ***********************************************/
/**
 * Attempts to shim the device passed
 *
 * @return 0 if device was successfully shimmed, -E on error
 */
static int shim_device(struct scsi_device *sdp)
{
    pr_loc_dbg("Trying to shim SCSI device vendor=\"%s\" model=\"%s\"", sdp->vendor, sdp->model);

    if (get_shimmed_boot_dev()) {
        pr_loc_wrn("The device should be shimmed but another device has been already shimmed as boot dev."
                   "Device has been ignored.");
        return -EEXIST;
    }

    pr_loc_dbg("Shimming device to vendor=\"%s\" model=\"%s\"", CONFIG_SYNO_SATA_DOM_VENDOR,
               CONFIG_SYNO_SATA_DOM_MODEL);
    strcpy((char *)sdp->vendor, CONFIG_SYNO_SATA_DOM_VENDOR);
    strcpy((char *)sdp->model, CONFIG_SYNO_SATA_DOM_MODEL);
    set_shimmed_boot_dev(sdp);

    return 0;
}

/**
 * Handles registration of newly plugged SCSI/SATA devices. It's called by the SCSI notifier automatically.
 *
 * @return NOTIFY_*
 */
static __used int on_new_scsi_disk(struct notifier_block *self, unsigned long state, void *data)
{
    if (state != SCSI_EVT_DEV_PROBING)
        return NOTIFY_DONE;

    struct scsi_device *sdp = data;

    pr_loc_dbg("Found new SCSI disk vendor=\"%s\" model=\"%s\": checking boot shim viability", sdp->vendor, sdp->model);
    if (!scsi_is_boot_dev_target(boot_dev_config, sdp))
        return NOTIFY_OK;

    int err = shim_device(data);
    if (unlikely(err != 0)) {
        //If we let the device register it may be misinterpreted as a normal disk and possibly formatted
        pr_loc_err("Shimming process failed with error=%d - "
                   "preventing the device from appearing in the OS to avoid possible damage", err);
        return NOTIFY_BAD;
    }

    return NOTIFY_OK;
}

/**
 * Processes existing device and if it's a SATA drive which matches shim criteria it will be unplugged & replugged to be
 * shimmed
 *
 * @param sdp This "struct device" should already be guaranteed to be an scsi_device with type=TYPE_DISK (i.e. returning
 *            "true" from is_scsi_disk())
 *
 * @return 0 means "continue calling me" while any other value means "I found what I was looking for, stop calling me".
 *         This convention is based on how bus_for_each_dev() works
 */
static int on_existing_scsi_disk(struct scsi_device *sdp)
{
    pr_loc_dbg("Found existing SCSI disk vendor=\"%s\" model=\"%s\": checking boot shim viability", sdp->vendor,
               sdp->model);

    if (!scsi_is_boot_dev_target(boot_dev_config, sdp))
        return 0;

    //So, now we know it's a shimmable target but we cannot just call shim_device() as this will change vendor+model on
    // already connected device, which will change these information but will not trigger syno type change. When we
    // disconnect & reconnect the device it will reappear and go through the on_new_scsi_disk() route.
    pr_loc_inf("SCSI disk vendor=\"%s\" model=\"%s\" is already connected but it's a boot dev. "
               "It will be forcefully reconnected to shim it as boot dev.", sdp->vendor, sdp->model);

    int out = scsi_force_replug(sdp);
    if (out < 0)
        pr_loc_err("Failed to replug the SCSI device (error=%d) - it may not shim as expected", out);
    else
        pr_loc_dbg("SCSI device replug triggered successfully");

    return 1;
}

/****************************************** Standard public API of the shim *******************************************/
static bool shim_registered = false;
int register_native_sata_boot_shim(const struct boot_media *config)
{
    shim_reg_in();

    //Regardless of the method we must set the expected size (in config) as shim may be called any moment from now on
    boot_dev_config = config;
    int out = 0;

    if (unlikely(boot_dev_config->type != BOOT_MEDIA_SATA_DOM)) {
        pr_loc_bug("%s doesn't support device type %d", __FUNCTION__, boot_dev_config->type);
        out = -EINVAL;
        goto fail;
    }

    if (unlikely(shim_registered)) {
        pr_loc_bug("Native SATA boot shim is already registered");
        out = -EEXIST;
        goto fail;
    }

    /* We always set-up watching for new devices, as the SCSI notifier is smart enough to accept new subscribers
     * regardless of the driver state, but if the driver is already loaded we also need to take care of existing devs.
     * Additionally, subscribing for notifications will, in the future, give us info if a device went away.
     */
    out = subscribe_scsi_disk_events(&scsi_disk_nb);
    if (unlikely(out != 0)) {
        pr_loc_err("Failed to register for SCSI disks notifications - error=%d", out);
        goto fail;
    }

    //This will already check if driver is loaded and only iterate if it is
    out = for_each_scsi_disk(on_existing_scsi_disk);
    //0 means "call me again" or "success", 1 means "found what I wanted, stop iterating", -ENXIO is "driver not ready"
    if (unlikely(out < 0 && out != -ENXIO)) {
        pr_loc_dbg("SCSI driver is already loaded but iteration over existing devices failed - error=%d", out);
        goto fail_unwatch;
    }

    shim_registered = true;
    shim_reg_ok();
    return 0;

    fail_unwatch:
    unsubscribe_scsi_disk_events(&scsi_disk_nb); //we keep the original code, so this function return code is ignored
    fail:
    boot_dev_config = NULL;
    return out;
}

int unregister_native_sata_boot_shim(void)
{
    shim_ureg_in();

    if (unlikely(!shim_registered)) {
        pr_loc_bug("Native SATA boot shim is not registered");
        return -ENOENT;
    }

    int out = unsubscribe_scsi_disk_events(&scsi_disk_nb);
    if (out != 0)
        pr_loc_err("Failed to unsubscribe from SCSI events");


    //@todo we are consciously NOT doing reset_shimmed_boot_dev(). It may be registered and we're not doing anything to
    // unregister it

    shim_registered = false;
    shim_ureg_ok();
    return 0;
}
#else //ifdef NATIVE_SATA_DOM_SUPPORTED
int register_native_sata_boot_shim(const struct boot_media *boot_dev_config)
{
    pr_loc_err("Native SATA boot shim cannot be registered in a kernel built without SATA DoM support");
    return -ENODEV;
}

int unregister_native_sata_boot_shim(void)
{
    pr_loc_err("Native SATA boot shim cannot be unregistered in a kernel built without SATA DoM support");
    return -ENODEV;
}
#endif //ifdef else NATIVE_SATA_DOM_SUPPORTED