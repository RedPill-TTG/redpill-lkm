/**
 * Notification chain implementation for SCSI devices
 *
 * Linux kernel contains a subsystem responsible for delivering notifications about asynchronous events. It implements a
 * pub/sub model. As many subsystems predate existence of the so-called Notification Chains these subsystems usually
 * lack any pub/sub functionality. SCSI is no exception. SCSI layer/driver is ancient and huge. It does not have any way
 * of delivering events to other parts of the system. This submodule retrofits notification chains to the SCSI layer to
 * notify about new devices being added to the system. It can be easily extended to notify about removed devices as
 * well.
 *
 * Before using this submodule you should read the notice below + the gitbooks article if you have never worked with
 * Linux notification chains.
 *
 * !! READ ME - THIS IS IMPORTANT !!
 * The core notifier.h contains the following return constants:
 *   - NOTIFY_DONE: "don't care about that event really"
 *   - NOTIFY_OK: "good, processed" (really the same as DONE; semantic is defined by a particular publisher[!])
 *   - NOTIFY_BAD: "stop the notification chain! I veto that action!"
 *   - NOTIFY_STOP: "stop the notification chain. It's all good."
 * This SCSI notifier defines them as such:
 *   - NOTIFY_DONE, NOTIFY_OK: processed, continue calling other
 *   - NOTIFY_BAD:
 *      scsi_event=SCSI_EVT_DEV_PROBING: stop sd_probe() with EBUSY error; subscribers with lower priority will not exec
 *      scsi_event=SCSI_EVT_DEV_PROBED_OK: subscribers with lower priority will not exec
 *      scsi_event=SCSI_EVT_DEV_PROBED_ERR: subscribers with lower priority will not exec
 *   - NOTIFY_STOP:
 *      scsi_event=SCSI_EVT_DEV_PROBING: stop sd_probe() with 0 err-code; subscribers with lower priority will not exec
 *      scsi_event=SCSI_EVT_DEV_PROBED_OK: subscribers with lower priority will not exec
 *      scsi_event=SCSI_EVT_DEV_PROBED_ERR: subscribers with lower priority will not exec
 *
 * SUPPORTED DEVICES
 * Currently only SCSI disks are supported. This isn't a technical limitation but rather a practical one - we don't want
 * to trigger notifications for all-all SCSI devices (which include hosts, buses, etc). If needed a new set of functions
 * subscribe_.../ubsubscribe_... can easily be added which don't filter by type.
 *
 * TODO
 * This notifier does not support notifying about disconnection of the device. It should as we need to know if device
 * disappeared (e.g. while processing shimming of boot devices).
 *
 * ADDITIONAL TOOLS
 * It is highly recommended to use scsi_toolbox when subscribing to notifications from the SCSI subsystem.
 *
 * References:
 *  - https://0xax.gitbooks.io/linux-insides/content/Concepts/linux-cpu-4.html (about notification chains subsystem)
 */
#include "scsi_notifier.h"
#include "../../common.h"
#include "../notifier_base.h" //notifier_*()
#include "scsi_notifier_list.h"
#include "scsi_toolbox.h"
#include "../intercept_driver_register.h" //watching for sd driver loading
#include <scsi/scsi_device.h> //to_scsi_device()

#define NOTIFIER_NAME "SCSI device"

/*********************************** Interacting with an active/loaded SCSI driver ************************************/
static driver_watcher_instance *driver_watcher = NULL;
static int (*org_sd_probe) (struct device *dev) = NULL; //set during register

/**
 * Main notification routine hooking sd_probe()
 */
static int sd_probe_shim(struct device *dev)
{
    pr_loc_dbg("Probing SCSI device using %s", __FUNCTION__);
    if (!is_scsi_leaf(dev)) {
        pr_loc_dbg("%s: new SCSI device connected - not a leaf, ignoring", __FUNCTION__);
        return org_sd_probe(dev);
    }

    struct scsi_device *sdp = to_scsi_device(dev);
    if (!is_scsi_disk(sdp)) {
        pr_loc_dbg("%s: new SCSI device connected - not a disk, ignoring", __FUNCTION__);
        return org_sd_probe(dev);
    }

    pr_loc_dbg("Triggering SCSI_EVT_DEV_PROBING notifications");
    int out = notifier_to_errno(blocking_notifier_call_chain(&rp_scsi_notify_list, SCSI_EVT_DEV_PROBING, sdp));
    if (unlikely(out == NOTIFY_STOP)) {
        pr_loc_dbg("After SCSI_EVT_DEV_PROBING a callee stopped chain with non-error condition. Faking probe-ok.");
        return 0;
    } else if (unlikely(out == NOTIFY_BAD)) {
        pr_loc_dbg("After SCSI_EVT_DEV_PROBING a callee stopped chain with non-error condition. Faking probe-ok.");
        return -EIO; //some generic error
    }

    pr_loc_dbg("Calling original sd_probe()");
    out = org_sd_probe(dev);
    scsi_event evt = (out == 0) ? SCSI_EVT_DEV_PROBED_OK : SCSI_EVT_DEV_PROBED_ERR;

    pr_loc_dbg("Triggering SCSI_EVT_DEV_PROBED notifications - sd_probe() exit=%d", out);
    blocking_notifier_call_chain(&rp_scsi_notify_list, evt, sdp);

    return out;
}

/**
 * Overrides sd_probe() to provide notifications via sd_probe_shim()
 *
 * @param drv "sd" driver instance
 */
static inline void install_sd_probe_shim(struct device_driver *drv)
{
    pr_loc_dbg("Overriding %pf()<%p> with %pf()<%p>", drv->probe, drv->probe, sd_probe_shim, sd_probe_shim);
    org_sd_probe = drv->probe;
    drv->probe = sd_probe_shim;
}

/**
 * Removes override of sd_probe(), installed by install_sd_probe_shim()
 *
 * @param drv "sd" driver instance
 */
static inline void uninstall_sd_probe_shim(struct device_driver *drv)
{
    if (unlikely(!org_sd_probe)) {
        pr_loc_wrn(
                "Cannot %s - original drv->probe is not saved. It was either never installed or it's a bug. "
                "The current drv->probe is %pf()<%p>",
                __FUNCTION__, drv->probe, drv->probe);
        return;
    }

    pr_loc_dbg("Restoring %pf()<%p> to %pf()<%p>", drv->probe, drv->probe, org_sd_probe, org_sd_probe);
    drv->probe = org_sd_probe;
    org_sd_probe = NULL;
}

/**
 * Watches for the sd driver to load in order to shim it. The driver registration is modified before the driver loads.
 */
static driver_watch_notify_result sd_load_watcher(struct device_driver *drv, driver_watch_notify_state event)
{
    if (unlikely(event != DWATCH_STATE_COMING))
        return DWATCH_NOTIFY_CONTINUE;

    pr_loc_dbg("%s driver loaded - triggering sd_probe shim installation", SCSI_DRV_NAME);
    install_sd_probe_shim(drv);

    driver_watcher = NULL; //returning DWATCH_NOTIFY_DONE causes automatic unwatching
    return DWATCH_NOTIFY_DONE;
}

/******************************************** Public API of the notifier **********************************************/
extern struct bus_type scsi_bus_type;

int subscribe_scsi_disk_events(struct notifier_block *nb)
{
    notifier_sub(nb);
    return blocking_notifier_chain_register(&rp_scsi_notify_list, nb);
}

int unsubscribe_scsi_disk_events(struct notifier_block *nb)
{
    notifier_unsub(nb);
    return blocking_notifier_chain_unregister(&rp_scsi_notify_list, nb);
}

// We need an additional flag as depending on which method of sd_probe override (watcher vs. existing driver find &
// switch)
static bool notifier_registered = false;
int register_scsi_notifier(void)
{
    notifier_reg_in();

    if (unlikely(notifier_registered)) {
        pr_loc_bug("%s notifier is already registered", NOTIFIER_NAME);
        return -EEXIST;
    }

    struct device_driver *drv = find_scsi_driver();

    if(unlikely(drv < 0)) { //some error occurred while looking for the driver
        return PTR_ERR(drv); //find_scsi_driver() should already log what went wrong
    } else if(drv) { //the driver is already loaded - driver watcher cannot help us
        pr_loc_wrn(
                "The %s driver was already loaded when %s notifier registered - some devices may already be registered",
                SCSI_DRV_NAME, NOTIFIER_NAME);
        install_sd_probe_shim(drv);
    } else { //driver not yet loaded - driver watcher will trigger sd_probe_shim installation when driver loads
        pr_loc_dbg("The %s driver is not ready to dispatch %s notifier events - awaiting driver", SCSI_DRV_NAME,
                   NOTIFIER_NAME);
        driver_watcher = watch_scsi_driver_register(sd_load_watcher, DWATCH_STATE_COMING);
        if (unlikely(IS_ERR(driver_watcher))) {
            pr_loc_err("Failed to register driver watcher for driver %s", SCSI_DRV_NAME);
            return PTR_ERR(driver_watcher);
        }
    }

    notifier_registered = true;

    notifier_reg_ok();
    return 0;
}

int unregister_scsi_notifier(void)
{
    notifier_ureg_in();

    if (unlikely(!notifier_registered)) {
        pr_loc_bug("%s notifier is not registered", NOTIFIER_NAME);
        return -ENOENT;
    }

    bool is_error = false;
    int out = -EINVAL;

    //Check if we're watching sd driver (i.e. SCSI notifier was registered and is now being unregistered before the
    // driver had a chance to load)
    if (unlikely(driver_watcher)) {
        pr_loc_dbg("%s notifier is still observing %s driver - stopping observer", NOTIFIER_NAME, SCSI_DRV_NAME);
        out = unwatch_driver_register(driver_watcher);
        if (unlikely(out != 0)) {
            pr_loc_err("Failed to unregister driver watcher - error=%d", out);
            is_error = true;
        }
    }

    //sd_probe() was replaced either after watching for the driver or on-the-spot after the driver was already loaded
    if (likely(org_sd_probe)) {
        struct device_driver *drv = find_scsi_driver();
        if (unlikely(IS_ERR(drv))) {
            return PTR_ERR(drv); //find_scsi_driver() should already log what went wrong
        } else if(likely(drv)) {
            uninstall_sd_probe_shim(drv);
        } else { //that is almost impossible as sd is built-in, but we if it happens there's nothing to recover
            pr_loc_wrn("%s driver went away (?!)", SCSI_DRV_NAME);
            is_error = true;
        }
    }

    notifier_registered = false;
    if (unlikely(is_error)) {
        return out;
    } else {
        notifier_ureg_ok();
        return 0;
    }
}
