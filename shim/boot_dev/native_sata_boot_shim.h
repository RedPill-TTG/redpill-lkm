#ifndef REDPILL_NATIVE_SATA_BOOT_SHIM_H
#define REDPILL_NATIVE_SATA_BOOT_SHIM_H

struct boot_media;
int register_native_sata_boot_shim(const struct boot_media *config);
int unregister_native_sata_boot_shim(void);

#endif //REDPILL_NATIVE_SATA_BOOT_SHIM_H
