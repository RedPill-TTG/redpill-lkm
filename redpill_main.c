#include "internal/stealth.h"
#include "redpill_main.h"
#include "config/runtime_config.h"
#include "common.h" //commonly used headers in this module
#include "internal/intercept_execve.h" //Handling of execve() replacement
#include "config/cmdline_delegate.h" //Parsing of kernel cmdline
#include "shim/boot_device_shim.h" //Shimming VID/PID of boot device
#include "shim/bios_shim.h" //Shimming various mfgBIOS functions to make them happy
#include "shim/block_fw_update_shim.h" //Prevent firmware update from running
#include "shim/disable_exectutables.h" //Disable common problematic executables

static int __init init_redpill(void)
{
    int error = 0;

    pr_loc_dbg("================================================================================================");
    pr_loc_inf("RedPill loading...");


    if ((error = extract_config_from_cmdline(&current_config)) != 0)
        goto error_out;

    if (!validate_runtime_config(&current_config)) {
        error = -EINVAL;
        goto error_out;
    }

    register_boot_shim(&current_config.boot_media, &current_config.mfg_mode);

    if (
            //Register this reasonably high as other modules can use it blindly
            (error = register_execve_interceptor()) != 0
         || (error = register_bios_shim()) != 0
         || (error = disable_common_executables()) != 0
         || (error = register_fw_update_shim()) != 0

         //This one should be done really late so that if it does hide something it's not hidden from us
         || (error = initialize_stealth(&current_config)) != 0
       )
        goto error_out;

    //All things below MUST be flag-based (either cmdline or device)


    pr_loc_inf("RedPill loaded (stealth=%d)", STEALTH_MODE);

    return 0;

    error_out:
        pr_loc_crt("RedPill cannot be loaded");
        return error;
}

static void __exit cleanup_redpill(void)
{
    pr_loc_inf("RedPill unloading...");

    uninitialize_stealth();
    unregister_fw_update_shim();
    unregister_bios_shim();
    unregister_execve_interceptor();
    unregister_boot_shim();
    free_runtime_config(&current_config);

    pr_loc_inf("RedPill is dead");
    pr_loc_dbg("================================================================================================");
}

MODULE_AUTHOR("TTG");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.5");
module_init(init_redpill);
module_exit(cleanup_redpill);
