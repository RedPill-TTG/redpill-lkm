#define SHIM_NAME "common executables disabler"

#include "disable_exectutables.h"
#include "shim_base.h"
#include "../common.h"
#include "../internal/intercept_execve.h"

#define PSTORE_PATH "/usr/syno/bin/syno_pstore_collect"
#define BOOTLOADER_UPDATE1_PATH "uboot_do_upd.sh"
#define BOOTLOADER_UPDATE2_PATH "./uboot_do_upd.sh"
#define SAS_FW_UPDATE_PATH "/tmpData/upd@te/sas_fw_upgrade_tool"

int register_disable_executables_shim(void)
{
    shim_reg_in();

    int out;
    if (
            (out = add_blocked_execve_filename(BOOTLOADER_UPDATE1_PATH)) != 0
         || (out = add_blocked_execve_filename(BOOTLOADER_UPDATE2_PATH)) != 0
         || (out = add_blocked_execve_filename(PSTORE_PATH)) != 0
         || (out = add_blocked_execve_filename(SAS_FW_UPDATE_PATH)) != 0
       ) {
        pr_loc_bug("Failed to disable some executables");
        return out;
    }

    shim_reg_ok();
    return 0;
}

int unregister_disable_executables_shim(void)
{
    //noop - execve entries will be cleared in one sweep during unregister of interceptor (it's much faster this way)
    //this function is kept for consistency
    return 0;
}