#ifndef REDPILLLKM_CALL_PROTECTED_H
#define REDPILLLKM_CALL_PROTECTED_H

#include <linux/version.h> //LINUX_VERSION_CODE, KERNEL_VERSION
#include <linux/types.h> //bool
#include <linux/kernel.h> //system_states & system_state

// *************************************** Useful macros *************************************** //
//Check if the system is still in booting stage (useful when you want to call __init functions as they're deleted)
#define is_system_booting() (system_state == SYSTEM_BOOTING)

// ************************** Exports of normally protected functions ************************** //

//A usual macros to make defining them easier & consistent with .c implementation
#define CP_LIST(...) __VA_ARGS__ //used to pass a list of arguments as a single argument
#define CP_DECLARE_SHIM(return_type, org_function_name, call_args) return_type _##org_function_name(call_args);

struct seq_file;
CP_DECLARE_SHIM(int, cmdline_proc_show, CP_LIST(struct seq_file *m, void *v)); //extracts kernel cmdline
CP_DECLARE_SHIM(void, flush_tlb_all, CP_LIST(void)); //used to flush caches in memory.c operations

/* Thanks Jeff... https://groups.google.com/g/kernel-meetup-bangalore/c/rvQccTl_3kc/m/BJCnnXGCAgAJ
 * In case the link disappears: Jeff Layton from RedHat decided to just nuke the getname() API after 7 years of it being
 *  exposed in the kernel. So in practice we need to use kallsyms to get it on kernels >=3.14 (up to current 5.14)
 * See https://github.com/torvalds/linux/commit/9115eac2c788c17b57c9256cb322fa7371972ddf
 * Another unrelated change which happened in v3.14 was that when "struct filename*" is passed the callee is responsible
 *  for freeing it (using putname()). However, in older versions we (the caller) needs to free it
 * See https://github.com/torvalds/linux/commit/c4ad8f98bef77c7356aa6a9ad9188a6acc6b849d
 *
 * This whole block deals with functions needed for execve() shimming
 */
struct filename;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
CP_DECLARE_SHIM(int, do_execve, CP_LIST(const char *filename,
        const char __user *const __user *__argv,
        const char __user *const __user *__envp));

#include <linux/fs.h>
#define _getname(...) getname(__VA_ARGS__)

//If syscall audit is disabled putname is an alias to final_putname(), see include/linux/fs.h; later on this was changed
// but this branch needs to handle only <3.14 as we don't need (and shouldn't use!) putname() in >=3.14
#ifndef CONFIG_AUDITSYSCALL //if CONFIG_AUDITSYSCALL disabled we unexport final_putname and add putname define line fs.h
#define _putname(name) _final_putname(name)
CP_DECLARE_SHIM(void, final_putname, CP_LIST(struct filename *name));
#else //if the CONFIG_AUDITSYSCALL is enabled we need to proxy to traced putname to make sure references are counted
CP_DECLARE_SHIM(void, putname, CP_LIST(struct filename *name));
#endif
#else
CP_DECLARE_SHIM(int, do_execve, CP_LIST(struct filename *filename,
        const char __user *const __user *__argv,
        const char __user *const __user *__envp));
CP_DECLARE_SHIM(struct filename *, getname, CP_LIST(const char __user *name));
#endif

//The following functions are used by vUART and uart_fixer
typedef struct uart_port *uart_port_p;
CP_DECLARE_SHIM(int, early_serial_setup, CP_LIST(struct uart_port *port));
CP_DECLARE_SHIM(int, serial8250_find_port, CP_LIST(struct uart_port *p));

//Exported so that we can forcefully rescan the SCSI host in scsi_toolbox. This operation is normally available in
// userland when you're a root, but somehow they missed an export for kernel code (which according to kernel rules is a
// bug, but probably nobody asked before)
struct Scsi_Host;
CP_DECLARE_SHIM(int, scsi_scan_host_selected,
                CP_LIST(struct Scsi_Host *shost, unsigned int channel, unsigned int id, u64 lun, int rescan));

struct ida;
CP_DECLARE_SHIM(int, ida_pre_get, CP_LIST(struct ida *ida, gfp_t gfp_mask));

//Used for fixing I/O scheduler if module was loaded using elevator= and broke it
CP_DECLARE_SHIM(int, elevator_setup, CP_LIST(char *str));

struct notifier_block;
CP_DECLARE_SHIM(void, usb_register_notify, CP_LIST(struct notifier_block *nb));
CP_DECLARE_SHIM(void, usb_unregister_notify, CP_LIST(struct notifier_block *nb));
#endif //REDPILLLKM_CALL_PROTECTED_H
