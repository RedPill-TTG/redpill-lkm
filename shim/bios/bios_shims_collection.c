#include "bios_shims_collection.h"
#include "../../common.h"

static unsigned long shimmed_entries[VTK_SIZE] = { '\0' };

static unsigned long shim_null_zero_ulong(void) { return 0; }
static void shim_null_void(void) { }
static unsigned long shim_null_zero_ulong_trace(void) { dump_stack(); return 0; }
static unsigned long shim_get_gpio_pin_usable(int *pin) { pin[1] = 0; return 0; }

static void inline shim_entry(unsigned long *vtable_start, const unsigned int idx, const void *new_sym_ptr)
{
    if (unlikely(idx > VTK_SIZE-1)) {
        pr_loc_bug("Attempted shim on index %d - out of range", idx);
        return;
    }

    //@todo OK, this is buggy - if you shim an entry which is not present in the original vtable (i.e. 0000000000000000)
    // you will never be able to recover it using unshim... but you shouldn't touch such entries anyway
    if (unlikely(shimmed_entries[idx])) {
        pr_loc_wrn("Index %d already shimmed - will be replaced (possible bug?)", idx);
    } else {
        shimmed_entries[idx] = vtable_start[idx]; //Only save original-original entry (not the override one)
    }

    pr_loc_dbg("mfgBIOS vtable [%d] originally %ps<%p> will now be %ps<%p>", idx, (void *) shimmed_entries[idx],
               (void *) shimmed_entries[idx], new_sym_ptr, new_sym_ptr);
    vtable_start[idx] = (unsigned long) new_sym_ptr;
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
 * @return true when shimming succeeded, false otherwise
 */
bool shim_bios(struct module *mod, unsigned long *vtable_start, unsigned long *vtable_end)
{
    if (unlikely(!vtable_start)) {
        pr_loc_bug("%s called without vtable start populated?!", __FUNCTION__);
        return false;
    }

    print_debug_symbols(vtable_start, vtable_end);
    shim_entry(vtable_start, VTK_GET_FAN_STATUS, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_FAN_STATUS, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_DISK_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_PWR_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_PWR_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_GPIO_PIN, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_GET_GPIO_PIN, shim_get_gpio_pin_usable);
    shim_entry(vtable_start, 17, shim_null_zero_ulong_trace);
    shim_entry(vtable_start, VTK_SET_ALR_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_GET_BUZ_CLR, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_BUZ_CLR, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_CPU_FAN_STATUS, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_PHY_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_HDD_ACT_LED, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_GET_MICROP_ID, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_SET_MICROP_ID, shim_null_zero_ulong);

    //DS918+ only [but it should be safe to set them anyway ;)]
    shim_entry(vtable_start, VTK_RTC_SET_APWR, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_RTC_GET_APWR, shim_null_zero_ulong);
    shim_entry(vtable_start, VTK_RTC_INT_APWR, shim_null_void);
    shim_entry(vtable_start, VTK_RTC_UINT_APWR, shim_null_void);
//    shim_entry(vtable_start, VTK_RTC_GET_TIME, TODO);
//    shim_entry(vtable_start, VTK_RTC_SET_TIME, TODO);

    print_debug_symbols(vtable_start, vtable_end);

    return true;
}

bool unshim_bios(unsigned long *vtable_start, unsigned long *vtable_end)
{
    for (int i = 0; i < VTK_SIZE; i++) {
        if (!shimmed_entries[i])
            continue;

        pr_loc_dbg("Restoring vtable [%d] from %ps<%p> to %ps<%p>", i, (void *) vtable_start[i],
                   (void *) vtable_start[i], (void *) shimmed_entries[i], (void *) shimmed_entries[i]);
        vtable_start[i] = shimmed_entries[i];
    }

    return true;
}

void clean_shims_history(void)
{
    memset(shimmed_entries, 0, sizeof(shimmed_entries));
}