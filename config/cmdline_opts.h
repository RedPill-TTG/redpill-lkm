#ifndef REDPILL_CMDLINE_OPTS_H
#define REDPILL_CMDLINE_OPTS_H

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
#define CMDLINE_CT_MFG "mfg" //VID & PID override will use force-reinstall VID/PID combo
#define CMDLINE_CT_DOM_SZMAX "dom_szmax=" //Max size of SATA device (MiB) to be considered a DOM (usually you should NOT use this)

//Standard Linux cmdline tokens
#define CMDLINE_KT_ELEVATOR  "elevator=" //Sets I/O scheduler (we use it to load RP LKM earlier than normally possible)
#define CMDLINE_KT_LOGLEVEL  "loglevel="
#define CMDLINE_KT_PK_BUFFER "log_buf_len=" //Length of the printk ring buffer (should usually be increased for debug)
#define CMDLINE_KT_EARLY_PK  "earlyprintk"

//Syno-specific cmdline tokens
#define CMDLINE_KT_HW        "syno_hw_version="
#define CMDLINE_KT_THAW      "syno_port_thaw=" //??

//0|1 - whether to use native SATA Disk-on-Module for boot drive (syno); 2 - use fake/emulated SATA DOM (rp)
#define CMDLINE_KT_SATADOM   "synoboot_satadom="
#  define CMDLINE_KT_SATADOM_DISABLED '0'
#  define CMDLINE_KT_SATADOM_NATIVE   '1'
#  define CMDLINE_KT_SATADOM_FAKE     '2'

#define CMDLINE_KT_SN        "sn="
#define CMDLINE_KT_NETIF_NUM "netif_num="
#define CMDLINE_KT_MACS      "macs="
//You CANNOT simply add more macN= - DSM kernel only uses 4. If they ever support >4 you need to modify cmdline handling
#define CMDLINE_KT_MAC1      "mac1="
#define CMDLINE_KT_MAC2      "mac2="
#define CMDLINE_KT_MAC3      "mac3="
#define CMDLINE_KT_MAC4      "mac4="

#endif //REDPILL_CMDLINE_OPTS_H
