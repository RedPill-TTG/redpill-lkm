#ifndef REDPILLLKM_COMMON_H
#define REDPILLLKM_COMMON_H

/******************************************** Available whole-module flags ********************************************/
//This (shameful) flag disables shims which cannot be properly unloaded to make debugging of other things easier
#define DBG_DISABLE_UNLOADABLE

//disabled uart unswapping even if needed (useful for hand-loading while running)
#define DBG_DISABLE_UART_SWAP_FIX

//Whether to cause a kernel panic when module fails to load internally (which should be normally done on production)
#define KP_ON_LOAD_ERROR
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

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#if STEALTH_MODE >= STEALTH_MODE_NORMAL
#define pr_loc_crt(fmt, ...)
#define pr_loc_err(fmt, ...)
#define pr_loc_inf(fmt, ...)
#define pr_loc_wrn(fmt, ...)
#define pr_loc_dbg(fmt, ...)
#define pr_loc_bug(fmt, ...)

#else //STEALTH_MODE

#define pr_loc_crt(fmt, ...) pr_crit( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define pr_loc_err(fmt, ...) pr_err ( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define pr_loc_inf(fmt, ...) pr_info( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define pr_loc_wrn(fmt, ...) pr_warn( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define pr_loc_dbg(fmt, ...) pr_info( "<%s/%s:%d> " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)

#define pr_loc_bug(fmt, ...) pr_err ( "<%s/%s:%d> !!BUG!! " pr_fmt(fmt) "\n", KBUILD_MODNAME, __FILENAME__, __LINE__, ##__VA_ARGS__)
#endif //STEALTH_MODE

#define sizeof_str_chunk(param) sizeof(param)-1 //gets the size of a string minus trailing nullbyte (useful for partial matches)

#endif //REDPILLLKM_COMMON_H
