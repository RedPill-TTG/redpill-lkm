#ifndef REDPILL_BIOS_SHIMS_COLLECTION_H
#define REDPILL_BIOS_SHIMS_COLLECTION_H

#include <linux/types.h> //bool
#include <linux/module.h> //struct module

//List of known indexes in the mfgBIOS vtable. The table can be recovered by shim/bios_shim.c. Some of its entries are
// replaced by shim/bios/bios_shims_collection.c
//The following indexes were determined based on
// - Jun's module code
// - Looking at the symbols when BIOS is loaded
// - Observing logs from mfgBIOS
#define VTK_CURRENT_OP          0
#define VTK_GET_BRAND           1
#define VTK_GET_MODEL           2
//index #3 is unknown
#define VTK_RTC_GET_TIME        4
#define VTK_RTC_SET_TIME        5
#define VTK_GET_FAN_STATUS      6 //present in: DS918+; not: DS3615xs
#define VTK_SET_FAN_STATUS      7
#define VTK_GET_SYS_TEMP        8 //present in: DS3615xs; not: DS918+
#define VTK_GET_CPU_TEMP        9
#define VTK_SET_DISK_LED        10
#define VTK_SET_PWR_LED         11
//index #12 is unknown
//index #13 is unknown
//index #14 is unknown
#define VTK_SET_GPIO_PIN        15
#define VTK_GET_GPIO_PIN        16
//index #17 is unknown; existing loader shims it to "ulong foo(void) { return 0; }"
#define VTK_RTC_SET_APWR        18 //set auto power on
#define VTK_RTC_GET_APWR        19 //get auto power on
#define VTK_RTC_INT_APWR        20 //initialize auto power on. present in: DS918+; not: DS3615xs
#define VTK_RTC_UINT_APWR       21 //uninitialize auto power on. present in: DS918+; not: DS3615xs
#define VTK_SET_ALR_LED         22 //alarm led
#define VTK_GET_BUZ_CLR         23
#define VTK_SET_BUZ_CLR         24
#define VTK_GET_PWR_STATUS      25
//index #26 is unknown
#define VTK_INT_MOD_TPE         27
#define VTK_UNINIT              28
#define VTK_SET_CPU_FAN_STATUS  29
#define VTK_SET_PHY_LED         30 //present in: DS620; not: DS3615xs, DS918+
#define VTK_SET_HDD_ACT_LED     31
//index #32 is unknown
#define VTK_GET_MICROP_ID       33
#define VTK_SET_MICROP_ID       34
//indexes #35-39 are unknown
#define VTK_GET_CPU_INF         40
#define VTK_SET_HA_LED          41 //present in: RC18015xs+ (and other HA units? don't have other); not: DS3615xs,DS918+
#define VTK_GET_CPY_BTN         42 //present in: DS718+
//indexes #43-44 are unknown
#define VTK_GET_FAN_RPM         45
//index #46 is unknown
#define VTK_GET_VOLT_SENSOR     47 //present in: DS3615xs; not: DS918+
//index #48 is unknown
#define VTK_GET_TEMP_SENSOR     49 //present in: DS3615xs; not: DS918+
//indexes #50-51 are unknown

#define VTK_SIZE 52

/**
 * Insert all the shims to the mfgBIOS
 */
bool shim_bios(struct module *mod, unsigned long *vtable_start, unsigned long *vtable_end);

/**
 * Removes all shims from the mfgBIOS
 */
bool unshim_bios(unsigned long *vtable_start, unsigned long *vtable_end);

/**
 * Forcefully forgets all original calls used to do unshim_bios()
 *
 * This function is useful when the BIOS unloads without this module being unloaded - then there's no point in keeping
 * stale entries. This will also prevent warning regarding already-shimmed BIOS when it reloads.
 */
void clean_shims_history(void);

#endif //REDPILL_BIOS_SHIMS_COLLECTION_H
