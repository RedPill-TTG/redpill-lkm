/**
 * A crazy attempt to use SATA disks as proper boot devices on systems without SATA DOM support
 *
 * BACKGROUND
 * The syno-modifed SCSI driver (sd.c) contains support for so-called boot disks. It is a logical designation for drives
 * separated from normal data disks. Normally that designation is based on vendor & model of the drive. The native SATA
 * boot shim uses that fact to modify user-supplied drive to match that vendor-model and be considered bootable.
 * Likewise similar mechanism exists for USB boot media. Both are completely separate and work totally differently.
 * While both USB storage and SATA are SCSI-based systems they different in the ways devices are identified and pretty
 * much in almost everything else except the protocol.
 *
 *
 * HOW DOES IT WORK?
 * This shim performs a nearly surgical task of grabbing a SATA disk (similarly to native SATA boot shim) and modifying
 * its descriptors to look like a USB drive for a short while. The descriptors cannot be left in such state for too
 * long, and have to be reverted as soon as the disk type is determined by the "sd.c" driver. This is because other
 * processes actually need to read & probe the drive as a SATA one (as you cannot communicate with a SATA device like
 * you do with a USB stick).
 * In a birds-eye view the descriptors are modified just before the sd_probe() is called and removed when ida_pre_get()
 * is called by the sd_probe(). The ida_pre_get() is nearly guaranteed [even if the sd.c code changes] to be called
 * very early in the process as the ID allocation needs to be done for anything else to use structures created within.
 *
 *
 * HERE BE DRAGONS
 * This code is highly experimental and may explode at any moment. We previously thought we cannot do anything with
 * SATA boot due to lack of kernel support for it (and userland method being broken now). This crazy idea actually
 * worked and after many tests on multiple platforms it seems to be stable. However, we advise against using it if USB
 * is an option. Code here has many safety cheks and safeguards but we will never consider it bullet-proof.
 *
 *
 * References:
 *  - https://www.kernel.org/doc/html/latest/core-api/idr.html (IDs assignment in the kernel)
 *  - drivers/scsi/sd.c in syno kernel GPL sources (look at sd_probe() and syno_disk_type_get())
 */
#include "fake_sata_boot_shim.h"
#include "boot_shim_base.h" //set_shimmed_boot_dev(), get_shimmed_boot_dev(), scsi_is_shim_target(), usb_shim_as_boot_dev()
#include "../shim_base.h" //shim_*
#include "../../common.h"
#include "../../internal/scsi/scsi_toolbox.h" //scsi_force_replug()
#include "../../internal/scsi/scsi_notifier.h" //waiting for the drive to appear
#include "../../internal/call_protected.h" //ida_pre_get()
#include "../../internal/override/override_symbol.h" //overriding ida_pre_get()
#include <scsi/scsi_device.h> //struct scsi_device
#include <scsi/scsi_host.h> //struct Scsi_Host, SYNO_PORT_TYPE_*
#include <linux/usb.h> //struct usb_device
#include <../drivers/usb/storage/usb.h> //struct us_data

#define SHIM_NAME "fake SATA boot device"

static const struct boot_media *boot_dev_config = NULL; //passed to scsi_is_shim_target() & usb_shim_as_boot_dev()
static struct scsi_device *camouflaged_sdp = NULL; //set when ANY device is under camouflage
static struct usb_device *fake_usbd = NULL; //ptr to our fake usb device scaffolding
static int org_port_type = 0; //original port type of the device which registered
static override_symbol_inst *ida_pre_get_ovs = NULL; //trap override
static unsigned long irq_flags = 0; //saved flags when IRQs are disabled (to prevent rescheduling)

//They call each other, see their own docblocks
static int camouflage_device(struct scsi_device *sdp);
static int uncamouflage_device(struct scsi_device *sdp);

struct ida;
/**
 * Called by the sd_probe() very early after disk type is determined. We can restore the disk to its original shape
 */
static int ida_pre_get_trap(struct ida *ida, gfp_t gfp_mask)
{
    //This can happen if the kernel decides to reschedule and/or some device appears JUST between setting up the trap
    // and disabling of rescheduling. We cannot reverse the order as setting up the trap requires flushing CPU caches
    // which isn't really feasible in non-preempt & IRQ-disabled state... a catch-22
    //It is also possible that it happens during uncamouflage_device - this is why we force-restore here and just call
    // it.
    if (unlikely(!camouflaged_sdp)) {
        pr_loc_bug("Hit ida_pr_get() trap without sdp saved - removing trap and calling original");
        restore_symbol(ida_pre_get_ovs);
        return _ida_pre_get(ida, gfp_mask);
    }

    pr_loc_dbg("Hit ida_pre_get() trap! Removing camouflage...");
    uncamouflage_device(camouflaged_sdp);

    pr_loc_dbg("Calling original ida_pre_get()");
    return _ida_pre_get(ida, gfp_mask);
}

/**
 * Checks if the device passes is "camouflaged" as a USB device
 */
static bool is_camouflaged(struct scsi_device *sdp)
{
    return likely(camouflaged_sdp) && camouflaged_sdp == sdp;
}

/**
 * Alters a SATA device to look like a USB boot disk
 *
 * Order of operations in camouflage/uncamouflage is VERY particular. We make sure we CANNOT fail (at least not without
 * a KP resulting from pagefault) once we disable preemption & irqs AND that no changes before preemption is disabled
 * are overriding anything external (as we can be rescheduled and we cannot leave stuff half-replaced)
 *
 * @param sdp A valid SATA disk (it's assumed it passed through scsi_is_boot_dev_target() already) to disguise as USB
 *
 * @return 0 on success, -E on error
 */
static int camouflage_device(struct scsi_device *sdp)
{
    //This is very serious - it means something went TERRIBLY wrong. The camouflage should last only through the
    // duration of probing. If we got here again before camouflaging it means there's a device floating around which
    // is a SATA device but with broken USB descriptors. This should never ever happen as it may lead to data loss and
    // crashes at best.
    if (unlikely(camouflaged_sdp)) {
        pr_loc_crt("Attempting to camouflage when another device is undergoing camouflage");
        return -EEXIST;
    }

    //Here's the kicker: most of the subsystems save a pointer to some driver-related data into sdp->host->hostdata.
    // Unfortunately usb-storage saves a whole us_data structure there. It can do that as it allows them to use some
    // neat container_of() tricks later on. However, it means that we must fake that arrangement. This means we have to
    // practically go over the boundaries of the struct memory passed as ->pusb_dev is simply +40 bytes over the struct
    // (+ 8 bytes to save the ptr). USUALLY it should be safe as there's spare empty space due to memory fragmentation.
    // Since we're doing this only for a short moment it shouldn't be a problem but we are making sure here the memory
    // is indeed empty where we want to make a change. There's no guarantees that we don't damage anything but with all
    // the safeguards here the chance is minimal.
    if (unlikely(host_to_us(sdp->host)->pusb_dev)) {
        pr_loc_crt("Cannot camouflage - space on pointer not empty");
        return -EINVAL;
    }

    if (unlikely(get_shimmed_boot_dev())) {
        pr_loc_wrn("Refusing to camouflage. Boot device was already shimmed but a new matching device appeared again - "
                   "this may produce unpredictable outcomes! Ignoring - check your hardware");
        return -EEXIST;
    }

    pr_loc_dbg("Camouflaging SATA disk vendor=\"%s\" model=\"%s\" to look like a USB boot device", sdp->vendor,
               sdp->model);

    pr_loc_dbg("Generating fake USB descriptor");
    kzalloc_or_exit_int(fake_usbd, sizeof(struct usb_device));
    usb_shim_as_boot_dev(boot_dev_config, fake_usbd);

    pr_loc_dbg("Setting-up ida_pre_get() trap");
    ida_pre_get_ovs = override_symbol("ida_pre_get", ida_pre_get_trap);
    if (unlikely(IS_ERR(ida_pre_get_ovs))) {
        pr_loc_err("Failed to override ida_pre_get - error=%ld", PTR_ERR(ida_pre_get_ovs));
        ida_pre_get_ovs = NULL;
        kfree(fake_usbd);
        return PTR_ERR(ida_pre_get_ovs);
    }

    pr_loc_dbg("Disabling rescheduling");
    preempt_disable();
    local_irq_save(irq_flags);

    pr_loc_dbg("Changing port type %d => %d", sdp->host->hostt->syno_port_type, SYNO_PORT_TYPE_USB);
    org_port_type = sdp->host->hostt->syno_port_type;
    sdp->host->hostt->syno_port_type = SYNO_PORT_TYPE_USB;

    pr_loc_dbg("Faking ptr to usb_device at %p", &host_to_us(sdp->host)->pusb_dev);
    host_to_us(sdp->host)->pusb_dev = fake_usbd;

    camouflaged_sdp = sdp;
    set_shimmed_boot_dev(sdp);

    return 0;
}

/**
 * Undoes what camouflage_device() does; i.e. restores device to its normal SATA-view
 *
 * @param sdp Previously camouflaged device
 *
 * @return 0 on success, -E on error
 */
static int uncamouflage_device(struct scsi_device *sdp)
{
    int out = 0;
    pr_loc_dbg("Uncamouflaging SATA disk vendor=\"%s\" model=\"%s\"", sdp->vendor, sdp->model);

    if (unlikely(host_to_us(sdp->host)->pusb_dev != fake_usbd)) {
                pr_loc_bug("Fake USB device in the scsi_device is not the same as our fake one - something changed it");
        return -EINVAL;
    }

    camouflaged_sdp = NULL;

    pr_loc_dbg("Removing fake usb_device ptr at %p", &host_to_us(sdp->host)->pusb_dev);
    host_to_us(sdp->host)->pusb_dev = NULL;

    pr_loc_dbg("Restoring port type %d => %d", sdp->host->hostt->syno_port_type, org_port_type);
    sdp->host->hostt->syno_port_type = org_port_type;
    org_port_type = 0;

    pr_loc_dbg("Re-enabling scheduling");
    local_irq_restore(irq_flags);
    preempt_enable();

    if (likely(ida_pre_get_ovs)) { //scheduling race condition may have removed that already in ida_pre_get_trap()
        pr_loc_dbg("Removing ida_pre_get() trap");
        if ((out = restore_symbol(ida_pre_get_ovs)) != 0)
        pr_loc_err ("Failed to restore original ida_pre_get() - error=%d", out);
        ida_pre_get_ovs = NULL;
    }

    pr_loc_dbg("Cleaning fake USB descriptor");
    kfree(fake_usbd);
    fake_usbd = NULL;

    return out;
}

/**
 * Called for every existing SCSI disk to determine if any of them is a candidate to be a boot device.
 *
 * If a given device is a SATA drive which matches shim criteria it will be unplugged & replugged to be shimmed.
 *
 * @param sdp This "struct device" should already be guaranteed to be an scsi_device with type=TYPE_DISK (i.e. returning
 *            "true" from is_scsi_disk()). It will be re-checked anyway but there's no point in passing anything which
 *            is not a SCSI disk.
 *
 * @return 0 means "continue calling me" while any other value means "I found what I was looking for, stop calling me".
 *         This convention is based on how bus_for_each_dev() works
 */
static int on_existing_scsi_disk_device(struct scsi_device *sdp)
{
    if (!scsi_is_boot_dev_target(boot_dev_config, sdp))
        return 0;

    pr_loc_dbg("Found a shimmable SCSI device - reconnecting to trigger shimming");
    scsi_force_replug(sdp);

    return 1;
}

/**
 * Called for every new (or recently forcefully re-plugged) device to camouflage it as a USB boot disk
 */
static int scsi_disk_probe_handler(struct notifier_block *self, unsigned long state, void *data)
{
    struct scsi_device *sdp = data;

    switch (state) {
        case SCSI_EVT_DEV_PROBING:
            if (unlikely(camouflaged_sdp)) {
                pr_loc_bug("Got device probe when other one is camouflaged - surprise reschedule happened?");
                uncamouflage_device(camouflaged_sdp);
                return NOTIFY_OK;
            }

            if (scsi_is_boot_dev_target(boot_dev_config, data))
                camouflage_device(sdp);

            return NOTIFY_OK;

        case SCSI_EVT_DEV_PROBED_OK:
        case SCSI_EVT_DEV_PROBED_ERR:
            if (is_camouflaged(sdp)) { //camouflage is expected to be removed by the ida_pre_get() trap
                pr_loc_bug("Probing finished but device is still camouflages - something went terribly wrong");
                uncamouflage_device(sdp);
            }

            return NOTIFY_OK;

        default:
            pr_loc_dbg("Not interesting SCSI EVT %lu - ignoring", state);
            return NOTIFY_DONE;
    }

}

static struct notifier_block scsi_disk_nb = {
    .notifier_call = scsi_disk_probe_handler,
    .priority = INT_MIN, //we want to be FIRST so that we other things can get the correct drive type
};

int register_fake_sata_boot_shim(const struct boot_media *config)
{
    shim_reg_in();

#ifdef NATIVE_SATA_DOM_SUPPORTED
    pr_loc_wrn("This platform supports native SATA DoM - usage of %s is highly discouraged", SHIM_NAME);
#else
    pr_loc_inf("This %s is a prototype - if stability is desired use USB boot media instead", SHIM_NAME);
#endif

    int out;
    boot_dev_config = config;

    pr_loc_dbg("Registering for new devices notifications");
    out = subscribe_scsi_disk_events(&scsi_disk_nb);
    if (unlikely(out != 0)) {
        pr_loc_err("Failed to register for SCSI disks notifications - error=%d", out);
        boot_dev_config = NULL;
        return out;
    }

    pr_loc_dbg("Iterating over existing devices");
    out = for_each_scsi_disk(on_existing_scsi_disk_device);
    if (unlikely(out != 0 && out != -ENXIO)) {
        pr_loc_err("Failed to enumerate current SCSI disks - error=%d", out);
        boot_dev_config = NULL;
        return out;
    }

    shim_reg_ok();
    return 0;
}

int unregister_fake_sata_boot_shim(void)
{
    shim_ureg_in();

    unsubscribe_scsi_disk_events(&scsi_disk_nb);
    boot_dev_config = NULL;

    shim_ureg_ok();
    return 0; //noop
}