#include "call_protected.h"
#include "../common.h"
#include <linux/errno.h> //common exit codes
#include <linux/kallsyms.h> //kallsyms_lookup_name()
#include <linux/module.h> //symbol_get()/put

//This will eventually stop working (since Linux >=5.7.0 has the kallsyms_lookup_name() removed)
//Workaround will be needed: https://github.com/xcellerator/linux_kernel_hacking/issues/3

//BELOW ARE ALL RE-DEFINED PRIVATE FUNCTIONS
//All functions headers are the same as their normal counterparts except:
// 1. marked as inline
// 2. _ prefixed
// 3. non-static

#define __VOID_RETURN__
#define UNEXPORTED_ASML(name, type, ...) extern asmlinkage type name(__VA_ARGS__)
#define UNEXPORTED_TYPD(name) typedef typeof(name) *name##__ret
#define UNEXPORTED_ADDR(name, fail_return) \
    unsigned long name##__addr = kallsyms_lookup_name(#name); \
    if (name##__addr == 0) { \
        pr_loc_bug("Failed to fetch %s() syscall address", #name); \
        return fail_return; \
    } \
    pr_loc_dbg("Got addr %lx for %s", name##__addr, #name);

#define DYNAMIC_ADDR(name, fail_return) \
    name##__ret name##__ptr = (name##__ret)__symbol_get(#name); \
    if (!name##__ptr) { \
        pr_loc_bug("Failed to fetch %s() symbol (is that module loaded?)", #name); \
        return fail_return; \
    } \
    pr_loc_dbg("Got ptr %p for %s", name##__ptr, #name); \
    __symbol_put(#name); //Doing this BEFORE calling the func. creates a TINY window where the symbol may "escape"
//********************************************************************************************************************//

bool kernel_has_symbol(const char *name) {
    if (__symbol_get(name)) {
        __symbol_put(name);

        return true;
    }

    return false;
}

UNEXPORTED_ASML(cmdline_proc_show, int, struct seq_file *m, void *v);
UNEXPORTED_TYPD(cmdline_proc_show);
int _cmdline_proc_show(struct seq_file *m, void *v)
{
    UNEXPORTED_ADDR(cmdline_proc_show, -EFAULT);
    return ((cmdline_proc_show__ret)cmdline_proc_show__addr)(m, v);
}

UNEXPORTED_ASML(usb_register_notify, void, struct notifier_block *nb);
UNEXPORTED_TYPD(usb_register_notify);
void _usb_register_notify(struct notifier_block *nb)
{
    DYNAMIC_ADDR(usb_register_notify, __VOID_RETURN__);
    ((usb_register_notify__ret)usb_register_notify__ptr)(nb);
}

UNEXPORTED_ASML(usb_unregister_notify, void, struct notifier_block *nb);
UNEXPORTED_TYPD(usb_unregister_notify);
void _usb_unregister_notify(struct notifier_block *nb)
{
    DYNAMIC_ADDR(usb_unregister_notify, __VOID_RETURN__);
    ((usb_unregister_notify__ret)usb_unregister_notify__ptr)(nb);
}