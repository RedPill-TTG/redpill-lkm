#include "internal/stealth.h"
#include "redpill_main.h"
#include "config/runtime_config.h"
#include "common.h" //commonly used headers in this module
#include "internal/intercept_execve.h" //Handling of execve() replacement
#include "config/cmdline_delegate.h" //Parsing of kernel cmdline
#include "shim/boot_device_shim.h" //Registering & deciding between boot device shims
#include "shim/bios_shim.h" //Shimming various mfgBIOS functions to make them happy
#include "shim/block_fw_update_shim.h" //Prevent firmware update from running
#include "shim/disable_exectutables.h" //Disable common problematic executables
#include "shim/pci_shim.h" //Handles PCI devices emulation
#include "shim/uart_fixer.h" //Various fixes for UART weirdness

//This (shameful) flag disables shims which cannot be properly unloaded to make debugging of other things easier
//#define DISABLE_UNLOADABLE

//Whether to cause a BUG() when module fails to load internally (which should be normally done on production)
#define BUG_ON_LOAD_ERROR

//Handle versioning stuff
#define RP_VERSION_MAJOR 0
#define RP_VERSION_MINOR 5
#define STRINGIFY(x) #x
#define VERSIONIFY(major,minor,postfix) "v" STRINGIFY(major) "." STRINGIFY(minor) "-" postfix
#define RP_VERSION_STR VERSIONIFY(RP_VERSION_MAJOR, RP_VERSION_MINOR, RP_VERSION_POSTFIX)

static int __init init_redpill(void)
{
    int out = 0;

    pr_loc_dbg("================================================================================================");
    pr_loc_inf("RedPill %s loading...", RP_VERSION_STR);

    if (
            (out = extract_config_from_cmdline(&current_config)) != 0 //This MUST be the first entry
         || (out = populate_runtime_config(&current_config)) != 0 //This MUST be second
         || (out = register_uart_fixer(current_config.hw_config)) != 0 //Fix consoles ASAP
         || (out = register_boot_shim(&current_config.boot_media)) //Make sure we're quick with this one
         || (out = register_execve_interceptor()) != 0 //Register this reasonably high as other modules can use it blindly
         || (out = register_bios_shim(current_config.hw_config)) != 0
         || (out = disable_common_executables()) != 0
         || (out = register_fw_update_shim()) != 0
#ifndef DISABLE_UNLOADABLE
         || (out = register_pci_shim(current_config.hw_config)) != 0
#endif
         //This one should be done really late so that if it does hide something it's not hidden from us
         || (out = initialize_stealth(&current_config)) != 0
       )
        goto error_out;

    pr_loc_inf("RedPill %s loaded (stealth=%d)", RP_VERSION_STR, STEALTH_MODE);
    return 0;

    error_out:
        pr_loc_crt("RedPill %s cannot be loaded, error=%d", RP_VERSION_STR, out);
#ifdef BUG_ON_LOAD_ERROR
        BUG();
#else
        return out;
#endif
}

static void __exit cleanup_redpill(void)
{
    pr_loc_inf("RedPill %s unloading...", RP_VERSION_STR);

    int (*cleanup_handlers[])(void ) = {
        uninitialize_stealth,
#ifndef DISABLE_UNLOADABLE
        unregister_pci_shim,
#endif
        unregister_fw_update_shim,
        unregister_bios_shim,
        unregister_execve_interceptor,
        unregister_boot_shim,
        unregister_uart_fixer
    };

    int out;
    for (int i = 0; i < ARRAY_SIZE(cleanup_handlers); i++) {
        pr_loc_dbg("Calling cleanup handler %p", cleanup_handlers[i]);
        out = cleanup_handlers[i]();
        if (out != 0)
            pr_loc_wrn("Cleanup handler %p failed with code=%d", cleanup_handlers[i], out);
    }

    free_runtime_config(&current_config); //A special snowflake ;)

    pr_loc_inf("RedPill %s is dead", RP_VERSION_STR);
    pr_loc_dbg("================================================================================================");
}

MODULE_AUTHOR("TTG");
MODULE_LICENSE("GPL");
MODULE_VERSION(RP_VERSION_STR);
module_init(init_redpill);
module_exit(cleanup_redpill);
