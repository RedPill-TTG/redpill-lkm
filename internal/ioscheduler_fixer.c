/**
 * This very simple submodule which prevents kernel log from being flooded with "I/O scheduler elevator not found"
 *
 * When this shim is loaded as a I/O scheduler (to load very early) it is being set as a I/O scheduler. As we later
 * remove the module file the system will constantly try to load now non-existing module "elevator-iosched". By
 * resetting the "chosen_elevator" using the same function called by "elevator=" handler we can pretend no custom
 * I/O scheduler was ever set (so that the system uses default one and stops complaining)
 */
#include "ioscheduler_fixer.h"
#include "../common.h"
#include "call_protected.h" //is_system_booting(), elevator_setup()
#include <linux/kernel.h> //system_state

#define SHIM_NAME "I/O scheduler fixer"

int reset_elevator(void)
{
    if (!is_system_booting()) {
        pr_loc_wrn("Cannot reset I/O scheduler / elevator= set - system is past booting stage (state=%d)",
                   system_state);
        return 0; //This is not an error technically speaking
    }

    pr_loc_dbg("Resetting I/O scheduler to default");
    return _elevator_setup("") == 1 ? 0 : -EINVAL;
}