#ifndef REDPILLLKM_BIOS_SHIM_H
#define REDPILLLKM_BIOS_SHIM_H

struct hw_config;
int register_bios_shim(const struct hw_config *hw);
int unregister_bios_shim(void);

#endif //REDPILLLKM_BIOS_SHIM_H
