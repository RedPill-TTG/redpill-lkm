/*
 * This shim is responsible for making the hardware<>DSM glue (aka mfg BIOS) happy by providing nullified
 * implementations of hardware-specific calls.
 *
 * The process relies on the fact that the original BIOS module keeps a vtable table in memory. That vtable contains
 * pointers to various functions used to communicate with the hardware. The most tricky part here is finding the vtable
 * and replacing calls in it with our own. Original ELF contains unscrambled symbols for the table under "synobios_ops"
 * name (see: readelf --syms /usr/lib/modules/synobios-dis.ko | grep 'synobios_ops'). However this is NOT a symbol which
 * gets exported to the kernel.
 *
 * When the Linux kernel loads a module it does a couple of things after loading the .ko file. From the important ones
 * here it reads the ELF, loads the .symtab (all symbols), processes all relocations, and then does a cleanup of stuff
 * which is not needed after module is loaded. The earliest hook normally available for other modules is the access
 * through modules notification API. It will provide access to the module as soon as its binary is loaded and init
 * function is executing. However:
 *  - we only get the access to the "struct module"
 *  - the data available contains kallsyms
 *  - at this point all non-kernel symbols are discarded from memory (see kernel/module.c:simplify_symbols())
 *
 *  While the symbols exist in the memory the symbol table cannot be accessed (short of loading the ELF again and
 *  re-parsing the binary... which is way too complex). Most of the ELF parsing routines in the kernel are implemented
 *  in kernel/module.c in `static` functions. This unfortunately means they aren't really replaceable as they are
 *  inlined and mangled. However, there's one place where CPU architecture-dependent step happens: relocation of
 *  symbols. When module.c:apply_relocations() is called on x86_64 it calls the
 *  arch/x86/kernel/module.c:apply_relocate_add(). Since this function is external it can be "gently" replaced.
 *
 * During the lifetime of apply_relocate_add(), which is redirected to _apply_relocate_add() here, the full ELF with
 * symbol table is available and thus the vtable can be located using process_bios_symbols().  However, it cannot be
 * just like that modified at this moment (remember: we're way before module init is called) as 1) functions it points
 * to may be relocated still, and 2) it's hardware-dependent (as seen by doing print_debug_symbols() before & after
 * init). We need to hook to the module notification API and shim what's needed AFTER module started initializing.
 *
 * So in summary:
 *  1. Redirect apply_relocate_add() => _apply_relocate_add() using internal/override_symbol.h
 *  2. Setup module notifier
 *  3. Look for "*_synobios" module in _apply_relocate_add() and if found iterate through symbols
 *  4. Find "synobios_ops" in full symbols table and save it's start & end addresses; disable override from [1]
 *  5. Wait until notified by the kernel about module started loaded (see bios_module_notifier_handler())
 *  6. Replace what's needed (see bios/bios_shims_collection.c:shim_bios_module())
 *  7. Wait until notified by the kernel about module fully loaded (and replace what was broken since 5.)
 *  8. Drink a beer
 *
 * Additionally, this module also handles replacement of some kernel structures called by the mfgBIOS:
 *  - see bios_shims_collection.c:shim_disk_leds_ctrl()
 *
 *  References:
 *   - https://en.wikipedia.org/wiki/Virtual_method_table
 */
#include "bios_shim.h"
#include "../common.h"
#include "../internal/override_symbol.h"
#include "../internal/call_protected.h" //kernel_has_symbol()
#include "bios/bios_shims_collection.h" //shim_bios_module(), unshim_bios_module(), shim_bios_disk_leds_ctrl()
#include <linux/notifier.h> //module notification
#include <linux/module.h> //struct module

static bool bios_shimmed = false;
static bool module_notify_registered = false;
static unsigned long *vtable_start = NULL;
static unsigned long *vtable_end = NULL;
static const struct hw_config *hw_config = NULL;
static inline int enable_symbols_capture(void);
static inline int disable_symbols_capture(void);

/********************************************* Shimming of mfgBIOS module *********************************************/
/**
 * Unified way to determine if a given module is a bios module (as this is not a simple == check)
 */
static inline bool is_bios_module(const char *name)
{
    char *separator_pos = strrchr(name, '_'); //bios will be named e.g. bromolow_synobios - find's the last _

    //Check if it's synobios or sth else really
    return (separator_pos && strcmp(separator_pos, "_synobios") == 0);
}

/**
 * Handles notifications regarding modules loading. It will only perform actions on modules matching is_bios_module()
 *
 * This is constantly loaded to provide useful error information in case the bios module goes away (it shouldn't). In
 * non-dev builds it can probably just go away.
 *
 * @return NOTIFY_* const
 */
static int bios_module_notifier_handler(struct notifier_block * self, unsigned long state, void * data)
{
    struct module *mod = data;

    if (!is_bios_module(mod->name))
        return NOTIFY_OK;

    if (state == MODULE_STATE_GOING) {
        //So this is actually not a problem with RP but rather with the bios module - it cannot be unloaded at will.
        //As soon as you try it will cause a circular error with page faults and the kernel will demand a reboot
        //We're not unregistering notifier in case one day this is fixed by the bios module ¯\_(ツ)_/¯
        pr_loc_err("%s BIOS went away - you may get a kernel panic if YOU unloaded it", mod->name);
        bios_shimmed = false;
        vtable_start = vtable_end = NULL;
        enable_symbols_capture();
        flush_bios_shims_history();

        return NOTIFY_OK;
    }

    if (bios_shimmed)
        return NOTIFY_OK;

    //So, this is really tricky actually. Some parts of the vtable are populated AND USED during init and some are
    // populated in init but used later. This means we need to try to shim twice - as fast as possible after init call
    // and just after init call finished.

    //We shouldn't react to MODULE_STATE_LIVE multiple times (can it happen? hmm...) nor do anything before init
    // of the module finished changing the vtable
    if (!shim_bios_module(hw_config, mod, vtable_start, vtable_end)) {
        bios_shimmed = false;
        return NOTIFY_OK;
    }

    if (state == MODULE_STATE_LIVE) {
        bios_shimmed = true;
        pr_loc_inf("%s BIOS *fully* shimmed", mod->name);
    } else {
        pr_loc_inf("%s BIOS *early* shimmed", mod->name);
    }

    return NOTIFY_OK;
}

static struct notifier_block bios_notifier_block = {
    .notifier_call = bios_module_notifier_handler
};
/**
 * Registers module notifier to modify vtable as soon as module finishes loading
 *
 * @return 0 on success, -E on failure
 */
static int register_bios_module_notifier(void)
{
    if (unlikely(module_notify_registered)) {
        pr_loc_bug("%s called while notifier already registered", __FUNCTION__);
        return -EALREADY;
    }

    //Check if the bios module is already present in the system. If it is we have a problem as the vtable must be
    // patched as it loads. It's unclear if it can be patched after it's loaded but most certainly we don't have the
    // address of the table. That's why this is an error. If by any chance we have an address we can try patching but
    // this scenario is unlikely to work (and re-loading of the bios is not possible as it KPs). There's also no EASY
    // way of accessing list of modules (and the bios module name depends on platform etc...)
    //This symbol is chosen semi-randomly (i.e. it should be stable over time) but it shouldn't be present anywhere else
    if (unlikely(kernel_has_symbol("synobios_ioctl"))) {
        pr_loc_err("BIOS module is already loaded (did you load this module too late?) - cannot recover!");
        return -EDEADLOCK;
    }

    int out = register_module_notifier(&bios_notifier_block);
    if(unlikely(out != 0)) {
        pr_loc_err("Failed to register module notifier"); //Currently it's impossible to happen... currently
        return out;
    }

    module_notify_registered = true;
    pr_loc_dbg("Registered bios module notifier");

    return 0;
}

/**
 * Reverses what register_bios_module_notifier did
 *
 * @return 0 on success, -E on failure
 */
static int unregister_bios_module_notifier(void)
{
    if (unlikely(!module_notify_registered)) {
        pr_loc_bug("%s called while notifier not yet registered", __FUNCTION__);
        return -ENOMEDIUM;
    }

    int out = unregister_module_notifier(&bios_notifier_block);
    if(unlikely(out != 0)) {
        pr_loc_err("Failed to unregister module notifier");
        return out;
    }

    module_notify_registered = false;
    pr_loc_dbg("Unregistered bios module notifier");

    return 0;
}

#define BIOS_CALLTABLE "synobios_ops"
/**
 * Scans module ELF headers for BIOS_CALLTABLE and saves its address
 */
static void process_bios_symbols(Elf64_Shdr *sechdrs, const char *strtab, unsigned int symindex, struct module *mod)
{
    Elf64_Shdr *symsec = &sechdrs[symindex];
    pr_loc_dbg("Symbol section <%p> @ vaddr<%llu> size[%llu]", symsec, symsec->sh_addr, symsec->sh_size);

    Elf64_Sym *sym;
    Elf64_Sym *vtable = NULL;
    sym = (void *)symsec->sh_addr; //First symbol in the table

    unsigned int i;
    for (i = 0; i < symsec->sh_size / sizeof(Elf64_Sym); i++) {
        const char *symname = strtab + sym[i].st_name;
        pr_loc_dbg("Symbol #%d in mfgBIOS \"%s\" {%s}<%p>", i, mod->name, symname, (void *)sym[i].st_value);

        //There are more than one, we're looking for THE table (not a pointer)
        if (strncmp(symname, BIOS_CALLTABLE, sizeof(BIOS_CALLTABLE)) == 0 && sym[i].st_size > sizeof(void *)) {
            pr_loc_dbg("Found vtable - size %llu", sym[i].st_size);
            vtable = &sym[i];
            break;
        }
    }

    //That, to my knowledge, shouldn't happen
    if (unlikely(!vtable)) {
        pr_loc_wrn("Didn't find \"%s\" in \"%s\" this time - that's weird?", BIOS_CALLTABLE, mod->name);
        return;
    }

    vtable_start = (unsigned long *)vtable->st_value;
    vtable_end = vtable_start + vtable->st_size;
    pr_loc_dbg("Found \"%s\" in \"%s\" @ <%p =%llu=> %p>", (strtab + vtable->st_name), mod->name, vtable_start,
               vtable->st_size, vtable_end);
    disable_symbols_capture();
}

/**************************************************** Entrypoints *****************************************************/
int register_bios_shim(const struct hw_config *hw)
{
    int out;
    hw_config = hw;

    if (
            (out = shim_disk_leds_ctrl(hw)) != 0 ||
            (out = enable_symbols_capture()) != 0 ||
            (out = register_bios_module_notifier()) != 0
       ) {
        return out;
    }

    pr_loc_inf("mfgBIOS shim registered");

    return 0;
}

int unregister_bios_shim(void)
{
    int out;

    if (likely(bios_shimmed)) {
        if (!unshim_bios_module(vtable_start, vtable_end))
            return -EINVAL;
    }

    out = unregister_bios_module_notifier();
    if (unlikely(out != 0))
        return out;

    out = disable_symbols_capture();
    if (unlikely(out != 0))
        return out;

    unshim_disk_leds_ctrl(); //this will be noop if nothing was registered

    hw_config = NULL;
    pr_loc_inf("mfgBIOS shim unregistered");

    return 0;
}

/************************************************** Internal Helpers **************************************************/
/**
 * A modified arch/x86/kernel/module.c:apply_relocate_add() from Linux v3.10.108 to save synobios_ops address
 *
 * This is taken straight from Linux v3.10 and modified:
 *  - added call to process_bios_symbols
 *  - commented-out DEBUGP
 * Original author notice: Copyright (C) 2001 Rusty Russell
 */
static int _apply_relocate_add(Elf64_Shdr *sechdrs, const char *strtab, unsigned int symindex, unsigned int relsec, struct module *me)
{
    unsigned int i;
    Elf64_Rela *rel = (void *)sechdrs[relsec].sh_addr;
    Elf64_Sym *sym;
    void *loc;
    u64 val;

    //Well, this is here because there isn't a good place to plug-in into modules loading to get the full symbols table
    //Later on kernel removes "useless" symbols (see module.c:simplify_symbols())... but we need them
    //After vtable address is found this override of apply_relocate_add() is removed
    if (!vtable_start && is_bios_module(me->name))
        process_bios_symbols(sechdrs, strtab, symindex, me);

//    DEBUGP("Applying relocate section %u to %u\n",
//           relsec, sechdrs[relsec].sh_info);
    for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
        /* This is where to make the change */
        loc = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
              + rel[i].r_offset;

        /* This is the symbol it is referring to.  Note that all
           undefined symbols have been resolved.  */
        sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
              + ELF64_R_SYM(rel[i].r_info);

//        DEBUGP("type %d st_value %Lx r_addend %Lx loc %Lx\n",
//               (int)ELF64_R_TYPE(rel[i].r_info),
//               sym->st_value, rel[i].r_addend, (u64)loc);

        val = sym->st_value + rel[i].r_addend;

        switch (ELF64_R_TYPE(rel[i].r_info)) {
            case R_X86_64_NONE:
                break;
            case R_X86_64_64:
                *(u64 *)loc = val;
                break;
            case R_X86_64_32:
                *(u32 *)loc = val;
                if (val != *(u32 *)loc)
                    goto overflow;
                break;
            case R_X86_64_32S:
                *(s32 *)loc = val;
                if ((s64)val != *(s32 *)loc)
                goto overflow;
                break;
            case R_X86_64_PC32:
                val -= (u64)loc;
                *(u32 *)loc = val;
                break;
            default:
                pr_err("%s: Unknown rela relocation: %llu\n",
                       me->name, ELF64_R_TYPE(rel[i].r_info));
                return -ENOEXEC;
        }
    }
    return 0;

    overflow:
    pr_err("overflow in relocation type %d val %Lx\n",
           (int)ELF64_R_TYPE(rel[i].r_info), val);
    pr_err("`%s' likely not compiled with -mcmodel=kernel\n",
           me->name);
    return -ENOEXEC;
}

static override_symbol_inst *ov_apply_relocate_add = NULL;
/**
 * Enables override of apply_relocate_add() to redirect it to _apply_relocate_add() in order to plug into a moment where
 * process_bios_symbols() can extract the data.
 *
 * @return 0 on success, -E on failure
 */
static inline int enable_symbols_capture(void)
{
    if (unlikely(ov_apply_relocate_add))
        return 0; //Technically it's working so it's a non-error scenario (and it may happen with modules notification)

    ov_apply_relocate_add = override_symbol("apply_relocate_add", _apply_relocate_add);
    if (unlikely(IS_ERR(ov_apply_relocate_add))) {
        int out = PTR_ERR(ov_apply_relocate_add);
        ov_apply_relocate_add = NULL;
        pr_loc_err("Failed to override apply_relocate_add, error=%d", out);
        return out;
    }

    return 0;
}

/**
 * Disables override of apply_relocate_add() if enabled.
 *
 * @return 0 on success/noop, -E on failure
 */
static inline int disable_symbols_capture(void)
{
    if (!ov_apply_relocate_add) //may have been restored before
        return 0;

    int out = restore_symbol(ov_apply_relocate_add);
    ov_apply_relocate_add = NULL;

    return out;
}