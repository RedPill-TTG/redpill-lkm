#ifndef REDPILLLKM_CMDLINE_DELEGATE_H
#define REDPILLLKM_CMDLINE_DELEGATE_H

#include "runtime_config.h"

#define CMDLINE_MAX 1024 //Max length of cmdline expected/processed; if longer a warning will be emitted
#define CMDLINE_SEP "\t\n "

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

//Standard Linux cmdline tokens
#define CMDLINE_KT_LOGLEVEL  "loglevel="
#define CMDLINE_KT_ELEVATOR  "elevator="
#define CMDLINE_KT_EARLY_PK  "earlyprintk"

//Syno-specific cmdline tokens
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
 * Provides an easy access to kernel cmdline
 *
 * Internally in the kernel code it is available as "saved_command_line". However that variable is not accessible for
 * modules. This function populates a char buffer with the cmdline extracted using other methods.
 *
 * @param cmdline_out A pointer to your buffer to save the cmdline
 * @param maxlen Your buffer space (in general you should use CMDLINE_MAX)
 * @return cmdline length on success or -E on error
 */
long get_kernel_cmdline(char *cmdline_out, unsigned long maxlen);

/**
 * Extracts & processes parameters from kernel cmdline
 *
 * Note: it's not guaranteed that the config will be valid. Check runtime_config.h.
 *
 * @param config pointer to save configuration
 */
int extract_config_from_cmdline(struct runtime_config *config);

#endif //REDPILLLKM_CMDLINE_DELEGATE_H
