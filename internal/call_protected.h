#ifndef REDPILLLKM_CALL_PROTECTED_H
#define REDPILLLKM_CALL_PROTECTED_H

#include <linux/version.h> //LINUX_VERSION_CODE, KERNEL_VERSION
#include <linux/types.h> //bool

bool kernel_has_symbol(const char *name);

// ************************** Exports of normally protected functions ************************** //

//A usual macros to make defining them easier & consistent with .c implementation
#define CP_LIST(...) __VA_ARGS__ //used to pass a list of arguments as a single argument
#define CP_DECLARE_SHIM(return_type, org_function_name, call_args) return_type _##org_function_name(call_args);


#include <linux/seq_file.h>
CP_DECLARE_SHIM(int, cmdline_proc_show, CP_LIST(struct seq_file *m, void *v));

//See https://github.com/torvalds/linux/commit/6bbb614ec478961c7443086bdf7fd6784479c14a
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
CP_DECLARE_SHIM(int, set_memory_ro, CP_LIST(unsigned long addr, int numpages));
CP_DECLARE_SHIM(int, set_memory_rw, CP_LIST(unsigned long addr, int numpages));
#else
#define _set_memory_ro(...) set_memory_ro(__VA_ARGS__)
#define _set_memory_rw(...) set_memory_rw(__VA_ARGS__)
#endif

//We only need these for intercept_execve() on newer kernels
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
#include <linux/fs.h>
CP_DECLARE_SHIM(int, do_execve, CP_LIST(struct filename *filename,
        const char __user *const __user *__argv,
        const char __user *const __user *__envp));
CP_DECLARE_SHIM(struct filename *, getname, CP_LIST(const char __user *));
#endif

typedef struct uart_port *uart_port_p;
CP_DECLARE_SHIM(int, early_serial_setup, CP_LIST(struct uart_port *port));
CP_DECLARE_SHIM(int, update_console_cmdline, CP_LIST(char *name, int idx, char *name_new, int idx_new, char *options));

#include <linux/notifier.h>
void _usb_register_notify(struct notifier_block *nb);
void _usb_unregister_notify(struct notifier_block *nb);
#endif //REDPILLLKM_CALL_PROTECTED_H
