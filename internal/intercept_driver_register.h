#ifndef REDPILL_DRIVER_WATCHER_H
#define REDPILL_DRIVER_WATCHER_H

#include <linux/device.h> //struct device_driver, driver_find (in .c)

/**
 * Codes which the callback call on watch can return
 */
typedef enum {
    DWATCH_NOTIFY_CONTINUE,     //callback processed the data and allows for the chain to continue
    DWATCH_NOTIFY_DONE,         //callback processed the data, allows for the chain to continue but wants to unregister
    DWATCH_NOTIFY_ABORT_OK,     //callback processed the data and determined that fake-OK should be returned to the original caller (DWATCH_STATE_COMING only)
    DWATCH_NOTIFY_ABORT_BUSY,   //callback processed the data and determined that fake-EBUSY should be returned to the original caller (DWATCH_STATE_COMING only)
} driver_watch_notify_result;

/**
 * Controls when the callback for loaded driver is called
 */
typedef enum {
    DWATCH_STATE_COMING = 0b100, //driver is loading, you can intercept the process using (DWATCH_NOTIFY_ABORT_*) and change data
    DWATCH_STATE_LIVE = 0b010, //driver just loaded
} driver_watch_notify_state;

typedef struct driver_watcher_instance driver_watcher_instance;
typedef driver_watch_notify_result (watch_dr_callback)(struct device_driver *drv, driver_watch_notify_state event);

/**
 * Start watching for a driver registration
 *
 * Note: if the driver is already loaded this will do nothing, unless the driver is removed and re-registers. You should
 * probably call is_driver_registered() first.
 *
 * @param name Name of the driver you want to observe
 * @param cb Callback called on an event
 * @param event_mask ORed driver_watch_notify_state flags to when the callback is called
 *
 * @return 0 on success, -E on error
 */
driver_watcher_instance *watch_driver_register(const char *name, watch_dr_callback *cb, int event_mask);

/**
 * Undoes what watch_driver_register() did
 *
 * @return 0 on success, -E on error
 */
int unwatch_driver_register(driver_watcher_instance *instance);

/**
 * Checks if a given driver exists
 *
 * Usually if the driver exists already it doesn't make sense to watch for it as the event will never be triggered
 * (unless the driver unregisters and registers again). If the bus is not specified here (NULL) a platform-driver will
 * be looked up (aka legacy driver).
 *
 * @return 0 if the driver is not registered, 1 if the driver is registered, -E on lookup error
 */
int is_driver_registered(const char *name, struct bus_type *bus);

#endif //REDPILL_DRIVER_WATCHER_H
