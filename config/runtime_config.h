#ifndef REDPILLLKM_RUNTIME_CONFIG_H
#define REDPILLLKM_RUNTIME_CONFIG_H

#include <linux/types.h> //bool

#define MAX_NET_IFACES 8
#define MAC_ADDR_LEN 12

#define VID_PID_EMPTY 0x0000
#define VID_PID_MAX   0xFFFF

#define MODEL_MAX_LENGTH 10
#define SN_MAX_LENGTH 13

typedef unsigned short device_id;
typedef char syno_hw[MODEL_MAX_LENGTH + 1];
typedef char mac_address[MAC_ADDR_LEN + 1];
typedef char serial_no[SN_MAX_LENGTH + 1];

struct boot_media {
    device_id vid; //Vendor ID of device containing the loader.       Default: empty <valid, use first>
    device_id pid; //Product ID of device containing the loader.      Default: empty <valid, use first>
};

struct runtime_config {
    syno_hw hw; //used to determine quirks.                                Default: empty <invalid>
    serial_no sn; //Used to validate it and warn the user.                 Default: empty <invalid>
    struct boot_media boot_media;
    bool mfg_mode; //emulate mfg mode.                                     Default: false <valid>
    bool port_thaw; //Currently unknown.                                   Default: true  <valid>
    unsigned short netif_num; //Number of eth interfaces.                  Default: 0     <invalid>
    mac_address *macs[MAX_NET_IFACES]; //MAC addresses of eth interfaces.  Default: []    <invalid>
};
extern struct runtime_config current_config;

bool validate_runtime_config(const struct runtime_config *config);

void free_runtime_config(struct runtime_config *config);

#endif //REDPILLLKM_RUNTIME_CONFIG_H
