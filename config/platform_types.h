#ifndef REDPILL_PLATFORM_TYPES_H
#define REDPILL_PLATFORM_TYPES_H

#include "vpci_types.h" //vpci_device_stub, MAX_VPCI_DEVS

struct hw_config {
    const char *name; //the longest so far is "RR36015xs+++" (12+1)

    const struct vpci_device_stub pci_stubs[MAX_VPCI_DEVS];

    //All custom flags
    const bool emulate_rtc:1;
    const bool swap_serial:1; //Whether ttyS0 and ttyS1 are swapped (reverses CONFIG_SYNO_X86_SERIAL_PORT_SWAP)
    const bool reinit_ttyS0:1; //Should the ttyS0 be forcefully re-initialized after module loads
    const bool fix_disk_led_ctrl:1; //Disabled libata-scsi bespoke disk led control (which often crashes some v4 platforms)
};

#endif //REDPILL_PLATFORM_TYPES_H
