#ifndef REDPILL_SATA_BOOT_SHIM_H
#define REDPILL_SATA_BOOT_SHIM_H

struct boot_media;
int register_sata_boot_shim(const struct boot_media *boot_dev_config);
int unregister_sata_boot_shim(void);

#endif //REDPILL_SATA_BOOT_SHIM_H
