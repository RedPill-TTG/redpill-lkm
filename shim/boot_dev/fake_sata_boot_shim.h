#ifndef REDPILL_FAKE_SATA_BOOT_SHIM_H
#define REDPILL_FAKE_SATA_BOOT_SHIM_H

struct boot_media;
int register_fake_sata_boot_shim(const struct boot_media *config);
int unregister_fake_sata_boot_shim(void);

#endif //REDPILL_FAKE_SATA_BOOT_SHIM_H
