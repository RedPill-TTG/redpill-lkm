#ifndef REDPILL_BIOS_SHIMS_COLLECTION_H
#define REDPILL_BIOS_SHIMS_COLLECTION_H

#include <linux/types.h> //bool
#include <linux/module.h> //struct module

/**
 * Insert all the shims to the mfgBIOS
 */
bool shim_bios(struct module *mod, unsigned long *vtable_start, unsigned long *vtable_end);

/**
 * Removes all shims from the mfgBIOS
 */
bool unshim_bios(unsigned long *vtable_start, unsigned long *vtable_end);

/**
 * Forcefully forgets all original calls used to do unshim_bios()
 *
 * This function is useful when the BIOS unloads without this module being unloaded - then there's no point in keeping
 * stale entries. This will also prevent warning regarding already-shimmed BIOS when it reloads.
 */
void clean_shims_history(void);

#endif //REDPILL_BIOS_SHIMS_COLLECTION_H
