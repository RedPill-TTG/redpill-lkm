#ifndef REDPILL_BIOS_HWMON_SHIM_H
#define REDPILL_BIOS_HWMON_SHIM_H

#include <linux/synobios.h>

struct hw_config;

//This should be called from shim_bios_module() as it depends on the state of the vtable; it can be called many times
int shim_bios_module_hwmon_entries(const struct hw_config *hw);
int reset_bios_module_hwmon_shim(void);

#endif //REDPILL_BIOS_HWMON_SHIM_H
