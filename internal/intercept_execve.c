/*
 * Submodule used to hook the execve() syscall, used by the userland to execute binaries.
 *
 * This submodule can currently block calls to specific binaries and fake a successful return of the execution. In the
 * future, if needed, an option to fake certain response and/or execute a different binary instead can be easily added
 * here.
 *
 * execve() is a rather special syscall. This submodule utilized override_symbool.c:override_syscall() to do the actual
 * ground work of replacing the call. However some syscalls (execve, fork, etc.) use ASM stubs with a non-GCC call
 * convention. Up until Linux v3.18 it wasn't a problem as long as the stub was called back. However, since v3.18 the
 * stub was changed in such a way that calling it using a normal convention from (i.e. from the shim here) will cause
 * IRET imbalance and a crash. This is worked around by skipping the whole stub and calling do_execve() with a filename
 * struct directly. This requires re-exported versions of these functions, so it may be marginally slower.
 * Because of that this trick is only utilized on Linux >v3.18 and older ones call the stub as normal.
 *
 * References:
 *  - https://github.com/torvalds/linux/commit/b645af2d5905c4e32399005b867987919cbfc3ae
 *  - https://my.oschina.net/macwe/blog/603583
 *  - https://stackoverflow.com/questions/8372912/hooking-sys-execve-on-linux-3-x
 */
#include "intercept_execve.h"
#include "../common.h"
#include <linux/limits.h>
#include <linux/fs.h> //struct filename
#include "override/override_syscall.h" //SYSCALL_SHIM_DEFINE3, override_symbol
#include "call_protected.h" //do_execve(), getname(), putname()

#ifdef RPDBG_EXECVE
#include "../debug/debug_execve.h"
#endif

#define MAX_INTERCEPTED_FILES 10

static char * intercepted_filenames[MAX_INTERCEPTED_FILES] = { NULL };

int add_blocked_execve_filename(const char *filename)
{
    if (unlikely(strlen(filename) > PATH_MAX))
        return -ENAMETOOLONG;

    unsigned int idx = 0;
    while (likely(intercepted_filenames[idx])) { //Find free spot
        if (unlikely(strcmp(filename, intercepted_filenames[idx]) == 0)) { //Does it exist already?
            pr_loc_bug("File %s was already added at %d", filename, idx);
            return -EEXIST;
        }

        if(unlikely(++idx >= MAX_INTERCEPTED_FILES)) { //Are we out of indexes?
            pr_loc_bug("Tried to add %d intercepted filename (max=%d)", idx, MAX_INTERCEPTED_FILES);
            return -ENOMEM;
        }
    }

    kmalloc_or_exit_int(intercepted_filenames[idx], strsize(filename));
    strcpy(intercepted_filenames[idx], filename); //Size checked above

    pr_loc_inf("Filename %s will be blocked from execution", filename);
    return 0;
}

SYSCALL_SHIM_DEFINE3(execve,
                     const char __user *, filename,
                     const char __user *const __user *, argv,
                     const char __user *const __user *, envp)
{
    struct filename *path = _getname(filename);

    //this is essentially what do_execve() (or SYSCALL_DEFINE3 on older kernels) will do if the getname ptr is invalid
    if (IS_ERR(path))
        return PTR_ERR(path);

    const char *pathname = path->name;
#ifdef RPDBG_EXECVE
    RPDBG_print_execve_call(pathname, argv);
#endif

    for (int i = 0; i < MAX_INTERCEPTED_FILES; i++) {
        if (!intercepted_filenames[i])
            break;

        if (unlikely(strcmp(pathname, intercepted_filenames[i]) == 0)) {
            pr_loc_inf("Blocked %s from running", pathname);
            //We cannot just return 0 here - execve() *does NOT* return on success, but replaces the current process ctx
            do_exit(0);
        }
    }

//Depending on the version of the kernel do_execve() accepts bare filename (old) or the full struct filename (newer)
//Additionally in older kernels we need to take care of the path lifetime and put it back (it's automatic in newer)
//See: https://github.com/torvalds/linux/commit/c4ad8f98bef77c7356aa6a9ad9188a6acc6b849d
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
    int out = _do_execve(pathname, argv, envp);
    _putname(path);
    return out;
#else
    return _do_execve(path, argv, envp);
#endif
}

static override_symbol_inst *sys_execve_ovs = NULL;
int register_execve_interceptor()
{
    pr_loc_dbg("Registering execve() interceptor");

    if (sys_execve_ovs) {
        pr_loc_bug("Called %s() while execve() interceptor is already registered", __FUNCTION__);
        return -EEXIST;
    }

    override_symbol_or_exit_int(sys_execve_ovs, "SyS_execve", SyS_execve_shim);

    pr_loc_inf("execve() interceptor registered");
    return 0;
}

int unregister_execve_interceptor()
{
    pr_loc_dbg("Unregistering execve() interceptor");

    if (!sys_execve_ovs) {
        pr_loc_bug("Called %s() while execve() interceptor is not registered (yet?)", __FUNCTION__);
        return -ENXIO;
    }

    int out = restore_symbol(sys_execve_ovs);
    if (out != 0)
        return out;
    sys_execve_ovs = NULL;

    //Free all strings duplicated in add_blocked_execve_filename()
    unsigned int idx = 0;
    while (idx < MAX_INTERCEPTED_FILES-1 && intercepted_filenames[idx]) {
        kfree(intercepted_filenames[idx]);
        intercepted_filenames[idx] = NULL;
        idx++;
    }

    pr_loc_inf("execve() interceptor unregistered");
    return 0;
}
