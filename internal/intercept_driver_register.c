#include "intercept_driver_register.h"
#include "../common.h"
#include "override/override_symbol.h"
#include <linux/platform_device.h> //platform_bus_type

#define MAX_WATCHERS 5 //can be increased as-needed
#define WATCH_FUNCTION "driver_register"

struct driver_watcher_instance {
    watch_dr_callback *cb;
    bool notify_coming:1;
    bool notify_live:1;
    char name[];
};

static override_symbol_inst *ov_driver_register = NULL;
static driver_watcher_instance *watchers[MAX_WATCHERS] = { NULL };

/**
 * Finds a registered watcher based on the driver name
 *
 * @return pointer to the spot on the list containing driver_watcher_instance or NULL if not found
 */
static driver_watcher_instance **match_watcher(const char *name)
{
    for (int i=0; i < MAX_WATCHERS; ++i) {
        if (!watchers[i])
            continue; //there could be "holes" due to unwatch calls

        if(strcmp(name, watchers[i]->name) == 0)
            return &watchers[i];
    }

    return NULL;
}

/**
 * Finds an empty spot in the watchers list
 *
 * @return pointer to the spot on the list which is empty or NULL if not found
 */
static driver_watcher_instance **watcher_list_spot(void)
{
    for (int i=0; i < MAX_WATCHERS; ++i) {
        if (!watchers[i])
            return &watchers[i];
    }

    return NULL;
}

/**
 * Checks if there any watchers registered (to determine if it makes sense to still shim the driver_register())
 */
static bool has_any_watchers(void)
{
    for (int i=0; i < MAX_WATCHERS; ++i) {
        if (watchers[i])
            return true;
    }

    return false;
}

/**
 * Calls the original driver_register() with error handling
 *
 * @return 0 on success, -E on error
 */
static int call_original_driver_register(struct device_driver *drv)
{
    int driver_register_out, org_call_out;
    org_call_out = call_overridden_symbol(driver_register_out, ov_driver_register, drv);

    if (unlikely(org_call_out != 0)) {
        pr_loc_err("Failed to call original %s (error=%d)", WATCH_FUNCTION, org_call_out);
        return org_call_out;
    }

    return driver_register_out;
}

/**
 * Replacement for driver_register(), executing registered hooks
 */
static int driver_register_shim(struct device_driver *drv)
{
    driver_watcher_instance **watcher_lptr = match_watcher(drv->name);
    int driver_load_result;
    bool driver_register_fulfilled = false;

    if (unlikely(!watcher_lptr)) {
        pr_loc_dbg("%s() interception active - no handler observing \"%s\" found, calling original %s()",
                   WATCH_FUNCTION, drv->name, WATCH_FUNCTION);
        return call_original_driver_register(drv);
    }

    pr_loc_dbg("%s() interception active - calling handler %pF<%p> for \"%s\"", WATCH_FUNCTION, (*watcher_lptr)->cb,
               (*watcher_lptr)->cb, drv->name);

    if ((*watcher_lptr)->notify_coming) {
        pr_loc_dbg("Calling for DWATCH_STATE_COMING");
        switch ((*watcher_lptr)->cb(drv, DWATCH_STATE_COMING)) {
            //CONTINUE and DONE cannot use fall-through as we cannot unregister watcher before calling it (as if this is the
            // last watcher the whole override will be stopped
            case DWATCH_NOTIFY_CONTINUE:
                pr_loc_dbg("Calling original %s() & leaving watcher active", WATCH_FUNCTION);
                driver_load_result = call_original_driver_register(drv);
                driver_register_fulfilled = true;
                break;
            case DWATCH_NOTIFY_DONE:
                pr_loc_dbg("Calling original %s() & removing watcher", WATCH_FUNCTION);
                driver_load_result = call_original_driver_register(drv);
                unwatch_driver_register(*watcher_lptr); //regardless of the call result we unregister
                return driver_load_result; //we return here as the watcher doesn't want to be bothered anymore
            case DWATCH_NOTIFY_ABORT_OK:
                pr_loc_dbg("Faking OK return of %s() per callback request", WATCH_FUNCTION);
                driver_load_result = 0;
                driver_register_fulfilled = true;
                break;
            case DWATCH_NOTIFY_ABORT_BUSY:
                pr_loc_dbg("Faking BUSY return of %s() per callback request", WATCH_FUNCTION);
                driver_load_result = -EBUSY;
                driver_register_fulfilled = true;
                break;
            default: //This should never happen if the callback is correct
                pr_loc_bug("%s callback %pF<%p> returned invalid status value during DWATCH_STATE_COMING",
                           WATCH_FUNCTION, (*watcher_lptr)->cb, (*watcher_lptr)->cb);
        }
    }

    if (!driver_register_fulfilled)
        driver_load_result = call_original_driver_register(drv);

    if (driver_load_result != 0) {
        pr_loc_err("%s driver failed to load - not triggering STATE_LIVE callbacks", drv->name);
        return driver_load_result;
    }

    if ((*watcher_lptr)->notify_live) {
        pr_loc_dbg("Calling for DWATCH_STATE_LIVE");
        if ((*watcher_lptr)->cb(drv, DWATCH_STATE_LIVE) == DWATCH_NOTIFY_DONE)
            unwatch_driver_register(*watcher_lptr);
    }

    return driver_load_result;
}

/**
 * Enables override of driver_register() to watch for new drivers registration
 *
 * @return 0 on success, or -E on error
 */
static int start_watching(void)
{
    if (unlikely(ov_driver_register)) {
        pr_loc_bug("Watching is already enabled!");
        return 0;
    }

    pr_loc_dbg("Starting intercept of %s()", WATCH_FUNCTION);
    ov_driver_register = override_symbol(WATCH_FUNCTION, driver_register_shim);
    if (unlikely(IS_ERR(ov_driver_register))) {
        pr_loc_err("Failed to intercept %s() - error=%ld", WATCH_FUNCTION, PTR_ERR(ov_driver_register));
        ov_driver_register = NULL;
        return -EINVAL;
    }
    pr_loc_dbg("%s() is now intercepted", WATCH_FUNCTION);

    return 0;
}

/**
 * Disables override of driver_register(), started by start_watching()
 *
 * @return 0 on success, or -E on error
 */
static int stop_watching(void)
{
    if (unlikely(!ov_driver_register)) {
        pr_loc_bug("Watching is NOT enabled");
        return 0;
    }

    pr_loc_dbg("Stopping intercept of %s()", WATCH_FUNCTION);
    int out = restore_symbol(ov_driver_register);
    if (unlikely(out != 0)) {
        pr_loc_err("Failed to restore %s() - error=%ld", WATCH_FUNCTION, PTR_ERR(ov_driver_register));
        return out;
    }
    pr_loc_dbg("Intercept of %s() stopped", WATCH_FUNCTION);

    return 0;
}

driver_watcher_instance *watch_driver_register(const char *name, watch_dr_callback *cb, int event_mask)
{
    driver_watcher_instance **watcher_lptr = match_watcher(name);
    if (unlikely(watcher_lptr)) {
        pr_loc_err("Watcher for %s already exists (callback=%pF<%p>)", name, (*watcher_lptr)->cb, (*watcher_lptr)->cb);
        return ERR_PTR(-EEXIST);
    }

    watcher_lptr = watcher_list_spot();
    if (unlikely(!watcher_lptr)) {
        pr_loc_bug("There are no free spots for a new watcher");
        return ERR_PTR(-ENOSPC);
    }

    kmalloc_or_exit_ptr(*watcher_lptr, sizeof(driver_watcher_instance) + strsize(name));
    strcpy((*watcher_lptr)->name, name);
    (*watcher_lptr)->cb = cb;
    (*watcher_lptr)->notify_coming = ((event_mask & DWATCH_STATE_COMING) == DWATCH_STATE_COMING);
    (*watcher_lptr)->notify_live = ((event_mask & DWATCH_STATE_LIVE) == DWATCH_STATE_LIVE);
    pr_loc_dbg("Registered %s() watcher for \"%s\" driver (coming=%d, live=%d)", WATCH_FUNCTION, name,
               (*watcher_lptr)->notify_coming ? 1 : 0, (*watcher_lptr)->notify_live ? 1 : 0);

    if (!ov_driver_register) {
        pr_loc_dbg("Registered the first driver_register watcher - starting watching");
        int out = start_watching();
        if (unlikely(out != 0))
            return ERR_PTR(out);
    }

    return *watcher_lptr;
}

int unwatch_driver_register(driver_watcher_instance *instance)
{
    driver_watcher_instance **matched_lptr = match_watcher(instance->name);
    if (unlikely(!matched_lptr)) {
        //This means it could be a double-unwatch situation and this will prevent a double-kfree (but the lack of crash
        // is not guaranteed as match_watcher() already touched the memory)
        pr_loc_bug("Watcher %p for %s couldn't be found in the watchers list", instance, instance->name);
        return -ENOENT;
    }

    if (unlikely(*matched_lptr != instance)) {
        pr_loc_bug("Watcher %p for %s was found but the instance on the list %p (@%p) isn't the same (?!)", instance,
                   instance->name, *matched_lptr, matched_lptr);
        return -EINVAL;
    }

    pr_loc_dbg("Removed %pF<%p> subscriber for \"%s\" driver", (*matched_lptr)->cb, (*matched_lptr)->cb,
               (*matched_lptr)->name);
    kfree(*matched_lptr);
    *matched_lptr = NULL;

    if (!has_any_watchers()) {
        pr_loc_dbg("Removed last %s() subscriber - unshimming %s()", WATCH_FUNCTION, WATCH_FUNCTION);
        int out;
        if ((out = stop_watching()) != 0)
            return out;
    }

    return 0;
}

int is_driver_registered(const char *name, struct bus_type *bus)
{
    if (!bus)
        bus = &platform_bus_type;

    struct device_driver *drv = driver_find(name, bus);
    if (IS_ERR(drv))
        return PTR_ERR(drv);

    return drv ? 1:0;
}