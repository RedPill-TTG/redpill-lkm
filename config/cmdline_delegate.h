#ifndef REDPILLLKM_CMDLINE_DELEGATE_H
#define REDPILLLKM_CMDLINE_DELEGATE_H

#include "runtime_config.h"

/**
 * Kernel command line tokens. For clarity keep them separated.
 *  CT = custom token
 *  KT = kernel token (default or syno)
 *
 * All should be defined in the .h to allow accessing outside for hints in errors.
 */
#define CMDLINE_CT_VID "vid=" //Boot media Vendor ID override
#define CMDLINE_CT_PID "pid=" //Boot media Product ID override
#define CMDLINE_CT_MFG "mfg" //VID & PID override will use force-reinstall VID/PID combo

#define CMDLINE_KT_HW        "syno_hw_version="
#define CMDLINE_KT_THAW      "syno_port_thaw=" //??
#define CMDLINE_KT_SN        "sn="
#define CMDLINE_KT_NETIF_NUM "netif_num="
#define CMDLINE_KT_MACS      "macs="
//You CANNOT simply add more macN= - DSM kernel only uses 4. If they ever support >4 you need to modify cmdline handling
#define CMDLINE_KT_MAC1      "mac1="
#define CMDLINE_KT_MAC2      "mac2="
#define CMDLINE_KT_MAC3      "mac3="
#define CMDLINE_KT_MAC4      "mac4="


/**
 * Extracts & processes parameters from kernel cmdline
 *
 * Note: it's not guaranteed that the config will be valid. Check runtime_config.h.
 *
 * @param config pointer to save configuration
 */
void extract_kernel_cmdline(struct runtime_config *config);

#endif //REDPILLLKM_CMDLINE_DELEGATE_H
