#include "disable_exectutables.h"
#include "../internal/intercept_execve.h"

#define PSTORE_PATH "/usr/syno/bin/syno_pstore_collect"
#define BOOTLOADER_UPDATE1_PATH "uboot_do_upd.sh"
#define BOOTLOADER_UPDATE2_PATH "./uboot_do_upd.sh"

int disable_common_executables(void)
{
    add_blocked_execve_filename(BOOTLOADER_UPDATE1_PATH);
    add_blocked_execve_filename(BOOTLOADER_UPDATE2_PATH);
    return add_blocked_execve_filename(PSTORE_PATH);
}