#include "stealth.h"
#include "stealth/sanitize_cmdline.h"
#include <linux/module.h> //struct module (for list_del)

//TODO:
//https://github.com/xcellerator/linux_kernel_hacking/blob/master/3_RootkitTechniques/3.0_hiding_lkm/rootkit.c
//remove file which was used for insmod
//remove kernel taint
//remove module loading from klog
//delete module file from ramdisk

int initialize_stealth(void *config2)
{
    struct runtime_config *config = config2;

    int error = 0;
#if STEALTH_MODE <= STEALTH_MODE_OFF
    //STEALTH_MODE_OFF shortcut
    return error;
#endif

#if STEALTH_MODE > STEALTH_MODE_OFF
    //These are STEALTH_MODE_BASIC ones
    if ((error = register_stealth_sanitize_cmdline(config->cmdline_blacklist)) != 0)
        return error;
#endif

#if STEALTH_MODE > STEALTH_MODE_BASIC
    //These will be STEALTH_MODE_NORMAL ones
#endif

#if STEALTH_MODE > STEALTH_MODE_NORMAL
    //These will be STEALTH_MODE_FULL ones
    list_del(&THIS_MODULE->list);
#endif

    return error;
}

int uninitialize_stealth(void)
{
    int error;

#if STEALTH_MODE > STEALTH_MODE_NORMAL
    //These will be STEALTH_MODE_FULL ones
#endif

#if STEALTH_MODE > STEALTH_MODE_BASIC
    //These will be STEALTH_MODE_NORMAL ones
#endif

#if STEALTH_MODE > STEALTH_MODE_OFF
    //These are STEALTH_MODE_BASIC ones
    if ((error = unregister_stealth_sanitize_cmdline()) != 0)
        return error;
#endif

    //Mode set to STEALTH_MODE_OFF or nothing failed before
    return error;
}