#ifndef REDPILLLKM_COMMON_H
#define REDPILLLKM_COMMON_H

/******************************************** Available whole-module flags ********************************************/
//This (shameful) flag disables shims which cannot be properly unloaded to make debugging of other things easier
//#define DBG_DISABLE_UNLOADABLE

//disabled uart unswapping even if needed (useful for hand-loading while running)
//#define DBG_DISABLE_UART_SWAP_FIX

//Whether to cause a kernel panic when module fails to load internally (which should be normally done on production)
#define KP_ON_LOAD_ERROR

//Print A LOT of vUART debug messages
//#define VUART_DEBUG_LOG

//Enabled printing of all ioctl() calls (hooked or not)
//#define DBG_SMART_PRINT_ALL_IOCTL

//Normally GetHwCapability calls (checking what hardware supports) are responded internally. Setting this DBG adds log
// of all requests & responses for hardware capabilities (and there're frquent but not overwhelming). Additionally this
// option turns on additional calls to the original GetHwCapability and logs compared values. Some values are ALWAYS
// proxied to the original GetHwCapability
//#define DBG_HWCAP

//Debug all hardware monitoring features (shim/bios/bios_hwmon_shim.c)
//#define DBG_HWMON
/**********************************************************************************************************************/

#include "internal/stealth.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h> //kmalloc
#include <linux/string.h>
#include "compat/string_compat.h"
#include <linux/types.h> //bool & others

/************************************************** Strings handling **************************************************/
#define get_static_name(variable) #variable
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define strlen_static(param) (sizeof(param)-1) //gets the size of a string minus trailing nullbyte (useful for partial matches)
#define strlen_to_size(len) (sizeof(char) * ((len)+1)) //useful for static strings, use strsize() for dynamic ones
#define strsize(param) strlen_to_size(strlen(param)) //strlen including NULLbyte; useful for kmalloc-ing
/**********************************************************************************************************************/

/****************************************** Dynamic memory allocation helpers *****************************************/
//[internal] Cleans up & provides standard reporting and return
#define __kalloc_err_report_clean(variable, size, exit) \
    (variable) = NULL; \
    pr_loc_crt("kernel memory alloc failure - tried to allocate %ld bytes for %s", \
               (long)(size), get_static_name(variable)); \
    return exit;

//Use these if you need to do a manual malloc with some extra checks but want to return a consistant message
#define kalloc_error_int(variable, size) do { __kalloc_err_report_clean(variable, size, -ENOMEM); } while(0)
#define kalloc_error_ptr(variable, size) do { __kalloc_err_report_clean(variable, size, ERR_PTR(-ENOMEM)); } while(0)

//[internal] Reserves memory & checks result
#define __kalloc_or_exit(type, variable, size, exit_type) \
    (variable) = (type)(size, GFP_KERNEL); \
    if (unlikely(!(variable))) { kalloc_error_ ## exit_type (variable, size); }

//Use these to do a standard malloc with error reporting
#define kmalloc_or_exit_int(variable, size) do { __kalloc_or_exit(kmalloc, variable, size, int); } while(0)
#define kmalloc_or_exit_ptr(variable, size) do { __kalloc_or_exit(kmalloc, variable, size, ptr); } while(0)
#define kzalloc_or_exit_int(variable, size) do { __kalloc_or_exit(kzalloc, variable, size, int); } while(0)
#define kzalloc_or_exit_ptr(variable, size) do { __kalloc_or_exit(kzalloc, variable, size, ptr); } while(0)
#define try_kfree(variable) do { if(variable) { kfree(variable); } } while(0)
/**********************************************************************************************************************/

/****************************************************** Logging *******************************************************/
#define _pr_loc_crt(fmt, ...) pr_crit( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define _pr_loc_err(fmt, ...) pr_err ( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define _pr_loc_wrn(fmt, ...) pr_warn( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define _pr_loc_inf(fmt, ...) pr_info( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define _pr_loc_dbg(fmt, ...) pr_info( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define _pr_loc_dbg_raw(fmt, ...) printk(fmt, ##__VA_ARGS__)
#define _pr_loc_bug(fmt, ...)                                                                                  \
    do {                                                                                                       \
        pr_err("<%s/%s:%d> !!BUG!! " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__); \
        WARN(1, "BUG log triggered");                                                                          \
    } while(0)

#if STEALTH_MODE >= STEALTH_MODE_FULL //all logs will be disabled in full
#define pr_loc_crt(fmt, ...)
#define pr_loc_err(fmt, ...)
#define pr_loc_wrn(fmt, ...)
#define pr_loc_inf(fmt, ...)
#define pr_loc_dbg(fmt, ...)
#define pr_loc_dbg_raw(fmt, ...)
#define pr_loc_bug(fmt, ...)
#define DBG_ALLOW_UNUSED(var) ((void)var) //in debug modes some variables are seen as unused (as they're only for dbg)

#elif STEALTH_MODE >= STEALTH_MODE_NORMAL //in normal mode we only warnings/errors/etc.
#define pr_loc_crt _pr_loc_crt
#define pr_loc_err _pr_loc_err
#define pr_loc_wrn _pr_loc_wrn
#define pr_loc_inf(fmt, ...)
#define pr_loc_dbg(fmt, ...)
#define pr_loc_dbg_raw(fmt, ...)
#define pr_loc_bug _pr_loc_bug
#define DBG_ALLOW_UNUSED(var) ((void)var) //in debug modes some variables are seen as unused (as they're only for dbg)

#else
#define pr_loc_crt _pr_loc_crt
#define pr_loc_err _pr_loc_err
#define pr_loc_inf _pr_loc_inf
#define pr_loc_wrn _pr_loc_wrn
#define pr_loc_dbg _pr_loc_dbg
#define pr_loc_dbg_raw _pr_loc_dbg_raw
#define pr_loc_bug _pr_loc_bug
#define DBG_ALLOW_UNUSED(var) //when debug logs are enables we don't silence unused variables warnings

#endif //STEALTH_MODE
/**********************************************************************************************************************/

#ifndef RP_MODULE_TARGET_VER
#error "The RP_MODULE_TARGET_VER is not defined - it is required to properly set VTKs"
#endif

//Before you change that you need to go and check all usages of RP_MODULE_TARGET_VER
#if RP_MODULE_TARGET_VER != 6 && RP_MODULE_TARGET_VER != 7
#error "The RP_MODULE_TARGET_VER value is invalid"
#endif

#endif //REDPILLLKM_COMMON_H
