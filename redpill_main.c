//#define STEALTH_MODE

#include "redpill_main.h"
#include "common.h" //commonly used headers in this module
#include "config/runtime_config.h"
#include "internal/stealth.h" //Handling of stealth mode
#include "internal/intercept_execve.h" //Handling of stealth mode
#include "config/cmdline_delegate.h" //Parsing of kernel cmdline
#include "shim/boot_device_shim.h" //Shimming VID/PID of boot device
#include "shim/bios_shim.h" //Shimming various mfgBIOS functions to make them happy
#include "shim/block_fw_update_shim.h" //Prevent firmware update from running
#include "shim/disable_exectutables.h" //Disable common problematic executables

static int __init init_redpill(void)
{
    pr_loc_dbg("================================================================================================");
    pr_loc_inf("RedPill loading...");

    extract_kernel_cmdline(&current_config);
    if (!validate_runtime_config(&current_config))
        goto error_out;

    register_boot_shim(&current_config.boot_media, &current_config.mfg_mode);

    if (
         register_execve_interceptor() != 0 || //Register this reasonably high as other modules can use it blindly
         register_bios_shim() != 0 ||
         disable_common_executables() != 0 ||
         register_fw_update_shim() != 0
       )
        goto error_out;

    //All things below MUST be flag-based (either cmdline or device)


    pr_loc_inf("RedPill loaded");

    return 0;

    error_out:
        pr_loc_crt("RedPill cannot be loaded");
        return -EINVAL;
}

static void __exit cleanup_redpill(void)
{
    pr_loc_inf("RedPill unloading...");

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
