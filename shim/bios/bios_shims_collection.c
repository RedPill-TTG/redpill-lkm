#include "bios_shims_collection.h"
#include "mfgbios_types.h"
#include "../../config/runtime_config.h" //hw_config
#include "rtc_proxy.h"
#include "../../common.h"
#include "../../internal/call_protected.h" //kernel_has_symbol()
#include "../../internal/override_symbol.h" //shimming leds stuff
#include <linux/synobios.h> //SYNO_DISK_LED

/************************************************* mfgBIOS LKM shims **************************************************/
static unsigned long org_shimmed_entries[VTK_SIZE] = { '\0' }; //original entries which were shimmed by custom entries
static unsigned long cust_shimmed_entries[VTK_SIZE] = { '\0' }; //custom entries which were set as shims

static unsigned long shim_null_zero_ulong(void) { return 0; }
static unsigned long shim_null_zero_ulong_trace(void) { dump_stack(); return 0; }
static unsigned long shim_get_gpio_pin_usable(int *pin) { pin[1] = 0; return 0; }

static void inline shim_entry(unsigned long *vtable_start, const unsigned int idx, const void *new_sym_ptr)
{
    if (unlikely(idx > VTK_SIZE-1)) {
        pr_loc_bug("Attempted shim on index %d - out of range", idx);
        return;
    }

    //The vtable entry is either not shimmed OR already shimmed with what we set before OR already *was* shimmed but
    // external (i.e. mfgBIOS) code overrode the shimmed entry.
    //We only save the original entry if it was set by the mfgBIOS (so not shimmed yet or ext. override situation)

    //it was already shimmed and the shim is still there => noop
    if (cust_shimmed_entries[idx] && cust_shimmed_entries[idx] == vtable_start[idx])
        return;

    pr_loc_dbg("mfgBIOS vtable [%d] originally %ps<%p> will now be %ps<%p>", idx, (void *) vtable_start[idx],
               (void *) vtable_start[idx], new_sym_ptr, new_sym_ptr);
    org_shimmed_entries[idx] = vtable_start[idx];
    cust_shimmed_entries[idx] = (unsigned long)new_sym_ptr;
    vtable_start[idx] = cust_shimmed_entries[idx];
}

/**
 * Prints a table of memory between vtable_start and vtable_end, trying to resolve symbols as it goes
 */
static void print_debug_symbols(unsigned long *vtable_start, unsigned long *vtable_end)
{
    if (unlikely(!vtable_start)) {
        pr_loc_dbg("Cannot print - no vtable address");
        return;
    }

    int im = vtable_end - vtable_start; //Should be multiplies of 8 in general (64 bit alignment)
    pr_loc_dbg("Will print %d bytes of memory from %p", im, vtable_start);

    unsigned long *call_ptr = vtable_start;
    unsigned char *byte_ptr = (char *)vtable_start;
    for (int i = 0; i < im; i++, byte_ptr++) {
        printk("%02x ", *byte_ptr);
        if ((i+1) % 8 == 0) {
            printk(" [%02d] 0x%03x \t%p\t%pS\n", i / 8, i-7, (void *) (*call_ptr), (void *) (*call_ptr));
            call_ptr++;
        }
    }
    printk("\n");

    pr_loc_dbg("Finished printing memory at %p", byte_ptr);
}

/**
 * Applies shims to the vtable used by the bios
 *
 * These calls may execute multiple times as the mfgBIOS is loading.
 *
 * @return true when shimming succeeded, false otherwise
 */
bool shim_bios_module(const struct hw_config *hw, struct module *mod, unsigned long *vtable_start, unsigned long *vtable_end)
{
    if (unlikely(!vtable_start)) {
        pr_loc_bug("%s called without vtable start populated?!", __FUNCTION__);
        return false;
    }

    print_debug_symbols(vtable_start, vtable_end);
    shim_entry(vtable_start, VTK_GET_FAN_STATE, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_FAN_STATE, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_DISK_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_PWR_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_PWR_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_GPIO_PIN, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_GET_GPIO_PIN, shim_get_gpio_pin_usable);
    shim_entry(vtable_start, VTK_SET_GPIO_PIN_BLINK, shim_null_zero_ulong_trace);
    shim_entry(vtable_start, VTK_SET_ALR_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_GET_BUZ_CLR, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_BUZ_CLR, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_CPU_FAN_STATUS, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_PHY_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_HDD_ACT_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_GET_MICROP_ID, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_MICROP_ID, shim_null_zero_ulong);

    if (hw->emulate_rtc) {
        pr_loc_dbg("Platform requires RTC proxy - enabling");
        shim_entry(vtable_start, VTK_RTC_GET_TIME, rtc_proxy_get_time);
        shim_entry(vtable_start, VTK_RTC_SET_TIME, rtc_proxy_set_time);
        shim_entry(vtable_start, VTK_RTC_INT_APWR, rtc_proxy_init_auto_power_on);
        shim_entry(vtable_start, VTK_RTC_GET_APWR, rtc_proxy_get_auto_power_on);
        shim_entry(vtable_start, VTK_RTC_SET_APWR, rtc_proxy_set_auto_power_on);
        shim_entry(vtable_start, VTK_RTC_UINT_APWR, rtc_proxy_uinit_auto_power_on);
    } else {
        pr_loc_dbg("Native RTC supported - not enabling proxy (emulate_rtc=%d)", hw->emulate_rtc ? 1:0);
    }

    print_debug_symbols(vtable_start, vtable_end);

    return true;
}

bool unshim_bios_module(unsigned long *vtable_start, unsigned long *vtable_end)
{
    for (int i = 0; i < VTK_SIZE; i++) {
        //make sure to check the cust_ one as org_ may contain NULL ptrs and we should restore them as NULL if they were
        // so originally
        if (!cust_shimmed_entries[i])
            continue;

        pr_loc_dbg("Restoring vtable [%d] from %ps<%p> to %ps<%p>", i, (void *) vtable_start[i],
                   (void *) vtable_start[i], (void *) org_shimmed_entries[i], (void *) org_shimmed_entries[i]);
        vtable_start[i] = org_shimmed_entries[i];
    }

    flush_bios_shims_history();

    return true;
}

void flush_bios_shims_history(void)
{
    memset(org_shimmed_entries, 0, sizeof(org_shimmed_entries));
    memset(cust_shimmed_entries, 0, sizeof(cust_shimmed_entries));
}

/******************************** Kernel-level shims related to mfgBIOS functionality *********************************/
extern void *funcSYNOSATADiskLedCtrl; //if this explodes one day we need to do kernel_has_symbol() on it dynamically

/*
 * Syno kernel has ifdefs for "MY_ABC_HERE" for syno_ahci_disk_led_enable() and syno_ahci_disk_led_enable_by_port() so
 * we need to check if they really exist and we cannot determine it statically
 */
DEFINE_OVSYMBOL_PTRS(funcSYNOSATADiskLedCtrl);
DEFINE_OVSYMBOL_PTRS(syno_ahci_disk_led_enable);
DEFINE_OVSYMBOL_PTRS(syno_ahci_disk_led_enable_by_port);
static int funcSYNOSATADiskLedCtrl_shim(int host_num, SYNO_DISK_LED led)
{
    pr_loc_dbg("Received %s with host=%d led=%d", __FUNCTION__, host_num, led);
    //exit code is not used anywhere in the public code, so this value is an educated guess based on libata-scsi.c
    return 0;
}

int syno_ahci_disk_led_enable_shim(const unsigned short host_num, const int value)
{
    pr_loc_dbg("Received %s with host=%d val=%d", __FUNCTION__, host_num, value);
    return 0;
}

int syno_ahci_disk_led_enable_by_port_shim(const unsigned short port, const int value)
{
    pr_loc_dbg("Received %s with port=%d val=%d", __FUNCTION__, port, value);
    return 0;
}

int shim_disk_leds_ctrl(const struct hw_config *hw)
{
    //we're checking this here to remove knowledge of "struct hw_config" from bios_shim letting others know it's NOT
    //the place to do BIOS shimming decisions
    if (!hw->fix_disk_led_ctrl)
        return 0;

    pr_loc_dbg("Shimming disk led control API");

    int out;
    //funcSYNOSATADiskLedCtrl exists on (almost?) all platforms, but it's null on some... go figure ;)
    if (funcSYNOSATADiskLedCtrl) {
        ALLOC_OVSYMBOL_PTRS(funcSYNOSATADiskLedCtrl);
        out = override_symbol("funcSYNOSATADiskLedCtrl", funcSYNOSATADiskLedCtrl_shim, &funcSYNOSATADiskLedCtrl_addr,
                              funcSYNOSATADiskLedCtrl_code);
        if (out != 0) {
            pr_loc_err("Failed to shim funcSYNOSATADiskLedCtrl");
            goto fail;
        }
    }

    if (kernel_has_symbol("syno_ahci_disk_led_enable")) {
        ALLOC_OVSYMBOL_PTRS(syno_ahci_disk_led_enable);
        out = override_symbol("syno_ahci_disk_led_enable", syno_ahci_disk_led_enable_shim,
                              &syno_ahci_disk_led_enable_addr, syno_ahci_disk_led_enable_code);
        if (out != 0) {
            pr_loc_err("Failed to shim syno_ahci_disk_led_enable");
            goto fail;
        }
    }

    if (kernel_has_symbol("syno_ahci_disk_led_enable_by_port")) {
        ALLOC_OVSYMBOL_PTRS(syno_ahci_disk_led_enable_by_port);
        out = override_symbol("syno_ahci_disk_led_enable_by_port", syno_ahci_disk_led_enable_by_port_shim,
                              &syno_ahci_disk_led_enable_by_port_addr, syno_ahci_disk_led_enable_by_port_code);
        if (out != 0) {
            pr_loc_err("Failed to shim syno_ahci_disk_led_enable_by_port");
            goto fail;
        }
    }

    pr_loc_dbg("Finished %s", __FUNCTION__);
    return 0;

    fail:
    FREE_OVSYMBOL_PTRS(funcSYNOSATADiskLedCtrl);
    FREE_OVSYMBOL_PTRS(syno_ahci_disk_led_enable);
    FREE_OVSYMBOL_PTRS(syno_ahci_disk_led_enable_by_port);
    return out;
}

int unshim_disk_leds_ctrl(void)
{
    pr_loc_dbg("Unshimming disk led control API");

    int out;
    bool failed = false;
    if (funcSYNOSATADiskLedCtrl_addr) {
        out = restore_symbol(funcSYNOSATADiskLedCtrl_addr, funcSYNOSATADiskLedCtrl_code);
        if (out != 0) { //falling through to try to unshim others too
            pr_loc_err("Failed to unshim funcSYNOSATADiskLedCtrl");
            failed = true;
        }
    }

    if (syno_ahci_disk_led_enable_addr) {
        out = restore_symbol(syno_ahci_disk_led_enable_addr, syno_ahci_disk_led_enable_code);
        if (out != 0) { //falling through to try to unshim others too
            pr_loc_err("Failed to unshim syno_ahci_disk_led_enable");
            failed = true;
        }
    }

    if (syno_ahci_disk_led_enable_by_port_addr) {
        out = restore_symbol(syno_ahci_disk_led_enable_by_port_addr, syno_ahci_disk_led_enable_by_port_code);
        if (out != 0) { //falling through to try to unshim others too
            pr_loc_err("Failed to unshim syno_ahci_disk_led_enable_by_port");
            failed = true;
        }
    }

    out = failed ? -EINVAL : 0;
    pr_loc_dbg("Finished %s (exit=%d)", __FUNCTION__, out);

    return out;
}