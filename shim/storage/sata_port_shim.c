/**
 * Allows for usage of SCSI-based storage devices like they were bare standard SATA ones
 *
 * WHY THIS SHIM?
 * Normally Linux doesn't care if something is an SCSI device or a SATA one, as SATA is a subset of SCSI (technically
 * speaking SATA is an interface using SCSI protocol). However, the syno-modified SCSI driver (drivers/scsi/sd.c) adds
 * a layer of logical disk types. These types determine what the disk actually is, so that the NAS can know what should
 * be done with them.
 * For example SYNO_DISK_USB, SYNO_DISK_SYNOBOOT, SYNO_DISK_SATA, and SYNO_DISK_ISCSI are all normally visible in the
 * system as /dev/sdX and are all SCSI-based drives. However, you can only use RAID on SATA drives and not on USB ones.
 * The "SYNO_DISK_SATA" is kind-of a catch-all type for all disks which are used for storing data, even if they're not
 * really SATA disks. One of the exceptions set by the sd.c driver is that if VirtIO driver is used all disks connected
 * via that method are treated as SYNO_DISK_SATA. Unfortunately that, very logical and useful, assumption is made ONLY
 * when the kernel is compiled with CONFIG_SYNO_KVMX64 (which is a special platform for VDSM). On all other platforms
 * disks connected to VirtIO will be slightly broken in old versions and unusable in newer ones (as their tpe is set to
 * SYNO_DISK_UNKNOWN). This shim brings the functionality available on CONFIG_SYNO_KVMX64 to all platforms.
 * In addition, it changes SAS ports to be SATA as well as syno reserves SYNO_DISK_SAS for usage with just a few FS
 * devices and external enclosures.
 *
 * HOW DOES IT WORK?
 * It simply plugs into the SCSI driver (via SCSI notifier) and waits for a new drive. When a new drive is connected it
 * checks if it was connected via the VirtIO driver or through a SAS card driver and changes the port type to
 * SYNO_PORT_TYPE_SATA, which will later force the driver to assume the drive is indeed a "SATA" drive (SYNO_DISK_SATA).
 * While the ports can be enumerated and changed all at once, it's safer to do it per-drive basis as drivers allow for
 * ports to be dynamically reconfigured and thus the type may change. This is also why we make no effort of
 * restoring port types after this shim is unregistered.
 *
 * References
 *   - drivers/scsi/sd.c in Linux sources
 */
#include "sata_port_shim.h"
#include "../shim_base.h"
#include "../../common.h"
#include "../../internal/scsi/scsi_toolbox.h" //scsi_force_replug()
#include "../../internal/scsi/scsi_notifier.h"
#include <scsi/scsi_device.h> //struct scsi_device
#include <scsi/scsi_host.h> //struct Scsi_Host, SYNO_PORT_TYPE_*

#define SHIM_NAME "SATA port emulator"
#define VIRTIO_HOST_ID "Virtio SCSI HBA"

/**
 * Checks if we should fix a given device or ignore it
 */
static bool is_fixable(struct scsi_device *sdp)
{
    return sdp->host->hostt->syno_port_type == SYNO_PORT_TYPE_SAS ||
           (sdp->host->hostt->syno_port_type != SYNO_PORT_TYPE_SATA &&
            strcmp(sdp->host->hostt->name, VIRTIO_HOST_ID) == 0);
}

/**
 * Processes any new devices connected to the system AND existing devices which were forcefully reconnected
 *
 * When a device which is deemed fixable it will replace its port to SATA to make it work as a standard SATA drive.
 *
 * @return 0 on success, -E on error
 */
static int on_new_scsi_disk_device(struct scsi_device *sdp)
{
    if (!is_fixable(sdp))
        return 0;

    pr_loc_dbg("Found new disk vendor=\"%s\" model=\"%s\" connected to \"%s\" HBA over non-SATA port (type=%d) - "
               "fixing to SATA port (type=%d)", sdp->vendor, sdp->model, sdp->host->hostt->name,
               sdp->host->hostt->syno_port_type, SYNO_PORT_TYPE_SATA);

    sdp->host->hostt->syno_port_type = SYNO_PORT_TYPE_SATA;

    return 0;
}

/**
 * Called for every existing SCSI-based disk to determine if there are any fixable devices which are already connected
 *
 * Every device which is fixable but still connected it will be forcefully re-connected, as this is the only way to fix
 * existing device properly.
 *
 * @return 0 on success, -E on error
 */
static int on_existing_scsi_disk_device(struct scsi_device *sdp)
{
    if (!is_fixable(sdp))
        return 0;

    pr_loc_dbg(
            "Found initialized disk vendor=\"%s\" model=\"%s\" connected to \"%s\" HBA over non-SATA port (type=%d)."
            " It must be auto-replugged to fix it.", sdp->vendor, sdp->model, sdp->host->hostt->name,
            sdp->host->hostt->syno_port_type);

    //After that it will land in on_new_scsi_disk_device()
    scsi_force_replug(sdp);

    return 0;
}

/**
 * Tiny shim to direct SCSI notifications to on_existing_scsi_disk_device() before it's probed
 */
static int scsi_disk_probe_handler(struct notifier_block *self, unsigned long state, void *data)
{
    if (state != SCSI_EVT_DEV_PROBING)
        return NOTIFY_DONE;

    on_new_scsi_disk_device(data);
    return NOTIFY_OK;
}

static struct notifier_block scsi_disk_nb = {
    .notifier_call = scsi_disk_probe_handler,
    .priority = INT_MIN, //we want to be FIRST so that we other things can get the correct drive type
};

int register_sata_port_shim(void)
{
    shim_reg_in();

    int out;

    pr_loc_dbg("Registering for new devices notifications");
    out = subscribe_scsi_disk_events(&scsi_disk_nb);
    if (unlikely(out != 0)) {
        pr_loc_err("Failed to register for SCSI disks notifications - error=%d", out);
        return out;
    }

    pr_loc_dbg("Iterating over existing devices");
    out = for_each_scsi_disk(on_existing_scsi_disk_device);
    if (unlikely(out != 0 && out != -ENXIO)) {
        pr_loc_err("Failed to enumerate current SCSI disks - error=%d", out);
        return out;
    }
    
    shim_reg_ok();
    return 0;
}

int unregister_sata_port_shim(void)
{
    shim_ureg_in();

    unsubscribe_scsi_disk_events(&scsi_disk_nb);

    shim_ureg_ok();
    return 0; //noop
}