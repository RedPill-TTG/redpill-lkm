#ifndef REDPILLLKM_BIOS_SHIM_H
#define REDPILLLKM_BIOS_SHIM_H

typedef struct hw_config hw_config_bios_shim;
int register_bios_shim(const hw_config_bios_shim *hw);
int unregister_bios_shim(void);

#endif //REDPILLLKM_BIOS_SHIM_H
