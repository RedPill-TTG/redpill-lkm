#ifndef REDPILL_VPCI_LIMITS_H
#define REDPILL_VPCI_LIMITS_H

#include "../shim/pci_shim.h" //pci_shim_device_type

//Defines below are experimentally determined to be sufficient but can often be changed
#define MAX_VPCI_BUSES 8 //adjust if needed, max 256
#define MAX_VPCI_DEVS 16 //adjust if needed, max 256*32=8192

struct vpci_device_stub {
    enum pci_shim_device_type type;
    u8 bus;
    u8 dev;
    u8 fn;
    bool multifunction:1;
};

#endif //REDPILL_VPCI_LIMITS_H
