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
 * References:
 *  - https://linux.die.net/man/3/execve
 *  - https://0xax.gitbooks.io/linux-insides/content/SysCall/linux-syscall-4.html
 */
#include "block_fw_update_shim.h"
#include "../common.h"
#include "../internal/override_symbol.h" //override_syscall()
#include <generated/uapi/asm/unistd_64.h> //syscalls numbers

#define FW_UPDATE_PATH "./H2OFFT-Lx64"

//These definitions must match SYSCALL_DEFINE3(execve) as in fs/exec.c
asmlinkage long (*org_sys_execve)(const char __user *filename,
                           const char __user *const __user *argv,
                           const char __user *const __user *envp);

static asmlinkage long shim_sys_execve(const char __user *filename,
                                        const char __user *const __user *argv,
                                        const char __user *const __user *envp)
{
    pr_loc_dbg("%s has been called with filename: %s %s", __FUNCTION__, filename, argv[0]);

    if (strncmp(filename, FW_UPDATE_PATH, sizeof(FW_UPDATE_PATH)) == 0) {
        pr_loc_inf("Blocked %s from running (firmware update process)", FW_UPDATE_PATH);

        //We cannot just return 0 here - execve() *does NOT* return on success, but replaces the currect process ctx
        do_exit(0);
    }

    return org_sys_execve(filename, argv, envp);
}

int register_fw_update_shim(void)
{
    int out;

    //This, according to many sources (e.g. https://stackoverflow.com/questions/8372912/hooking-sys-execve-on-linux-3-x)
    // should NOT work. It does work as we're not calling the sys_execve() directly but through the expected ASM stub...
    //I *think* that's why it work (or I failed to find a scenario where it doesn't yet :D)
    out = override_syscall(__NR_execve, shim_sys_execve, (void *)&org_sys_execve);
    if (out != 0)
        return out;

    pr_loc_inf("Firmware updater blocker registered");

    return 0;
}

int unregister_fw_update_shim(void)
{
    int out;

    out = restore_syscall(__NR_execve);
    if (out != 0)
        return out;

    pr_loc_inf("Firmware updater blocker unregistered");

    return 0;
}