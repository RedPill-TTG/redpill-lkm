/**
 * This rather simple shim prevents execution of a firmware update program when done in a one specific way
 *
 * During the OS installation process one of the steps executes a command "./H2OFFT-Lx64". This is a board firmware
 * update program. When executed under KVM it will crash the virtual CPU (and I wasn't brave enough to try it on bare
 * metal). All in all the execution must succeed from the perspective of the user-space and the file cannot be modified
 * due to checksum check.
 *
 * This shim hooks a execve() syscall and filter it through shim_sys_execve(). This in turn is self-explanatory for the
 * most part - it simply fakes successful execution without invoking anything. While such trickery can be detected (as
 * the real process is not really replaced) it is good enough for this case.
 *
 * Additionally, to make the firmware picking happy we need to pass a sanity check (which is presumably done to ensure
 * flasher doesn't accidentally brick an incorrect board) using DMI data. This is handled here by overriding one string
 * in the DMI data array (as the kernel API lacks any way of changing that).
 *
 * References:
 *  - https://linux.die.net/man/3/execve
 *  - https://0xax.gitbooks.io/linux-insides/content/SysCall/linux-syscall-4.html
 *  - https://help.ubuntu.com/community/FimwareUpgrade/Insyde
 */
#include "block_fw_update_shim.h"
#include "../common.h"
#include "../internal/intercept_execve.h"
#include <linux/dmi.h> //dmi_get_system_info(), DMI_*

#define DMI_MAX_LEN 512
#define FW_BOARD_NAME "\x53\x79\x6e\x6f\x64\x65\x6e"
#define FW_UPDATE_PATH "./H2OFFT-Lx64"

static char dmi_product_name_backup[DMI_MAX_LEN] = { '\0' };
static void patch_dmi(void)
{
    char *ptr = (char *)dmi_get_system_info(DMI_PRODUCT_NAME);
    size_t org_len = strlen(ptr);
    if (org_len > DMI_MAX_LEN)
        pr_loc_wrn("DMI field longer than %zu - restoring on module unload will be limited to that length", org_len);

    strncpy((char *)&dmi_product_name_backup, ptr, DMI_MAX_LEN);
    dmi_product_name_backup[DMI_MAX_LEN-1] = '\0';
    pr_loc_dbg("Saved backup DMI: %s", dmi_product_name_backup);

    //This TECHNICALLY can cause overflow but DMI has buffer for such a short string
    if (org_len < sizeof_str_chunk(FW_BOARD_NAME))
        pr_loc_bug("Shimmed DMI field will be longer than original!");

    strcpy(ptr, FW_BOARD_NAME);
}

static void unpatch_dmi(void)
{
    if (dmi_product_name_backup[0] == '\0') {
        pr_loc_dbg("Skipping %s - DMI not patched", __FUNCTION__);
        return;
    }

    strcpy((char *)dmi_get_system_info(DMI_PRODUCT_NAME), dmi_product_name_backup);
    pr_loc_dbg("DMI unpatched");
}

int register_fw_update_shim(void)
{
    int out = add_blocked_execve_filename(FW_UPDATE_PATH);
    if (out != 0)
        return out;

    patch_dmi();

    pr_loc_inf("Firmware updater blocker registered");

    return 0;
}

int unregister_fw_update_shim(void)
{
    //Do not remove execve here - it will be cleared in one sweep during unregister of interceptor

    unpatch_dmi();

    pr_loc_inf("Firmware updater blocker unregistered");

    return 0;
}