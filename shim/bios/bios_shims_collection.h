#ifndef REDPILL_BIOS_SHIMS_COLLECTION_H
#define REDPILL_BIOS_SHIMS_COLLECTION_H

#include <linux/types.h> //bool
#include <linux/module.h> //struct module


typedef struct hw_config hw_config_bios_shim_col;
/**
 * Insert all the shims to the mfgBIOS
 */
bool shim_bios_module(const hw_config_bios_shim_col *hw, struct module *mod, unsigned long *vtable_start, unsigned long *vtable_end);

/**
 * Removes all shims from the mfgBIOS & uninitializes all components used to shim bios module
 */
bool unshim_bios_module(unsigned long *vtable_start, unsigned long *vtable_end);

/**
 * Forcefully forgets all original calls used to do unshim_bios() & cleans-up all other components
 *
 * This function is useful when the BIOS unloads without this module being unloaded - then there's no point in keeping
 * stale entries. This will also prevent warning regarding already-shimmed BIOS when it reloads.
 */
void reset_bios_shims(void);

/**
 * Nullifies manual disks LED control
 *
 * The underlying reason for this isn't known but sometimes the manual LED control for disks (presumably used to blink
 * to identify disks from the UI) will cause a kernel panic pointing to the internals of mfgBIOS. The functionality is
 * implemented in the kernel (funcSYNOSATADiskLedCtrl) but it delegates the task to mfgBIOS via ioctl()
 * To prevent the crash we replace the manual LED altering subsystem in models which support it (because not all do).
 *
 * The kernel panic is most likely caused by the gap between early and full bios shimming where the bios will be early
 * shimmed, continue setting something and in between gets an ioctl to the LED api
 *
 * @return 0 on success or -E on error
 */
int shim_disk_leds_ctrl(const struct hw_config *hw);

/**
 * Reverses what shim_disk_leds_ctrl did
 *
 * You CAN call this function any time, if shims weren't registered (yet) it will be a noop-call.
 *
 * @return 0 on success or -E on error
 */
int unshim_disk_leds_ctrl(void);

/**
 * Used by mfgBIOS sub-shims. Should NOT be called from ANY other context as it depends on the internal state.
 */
void _shim_bios_module_entry(unsigned int idx, const void *new_sym_ptr);

#endif //REDPILL_BIOS_SHIMS_COLLECTION_H
