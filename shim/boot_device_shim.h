#ifndef REDPILLLKM_BOOT_DEVICE_SHIM_H
#define REDPILLLKM_BOOT_DEVICE_SHIM_H

#include <linux/types.h> //bool type
#include "../config/runtime_config.h" //boot_media type

void register_boot_shim(const struct boot_media *real_boot_device, const bool *run_mfg_mode);
void unregister_boot_shim(void);

#endif //REDPILLLKM_BOOT_DEVICE_SHIM_H
