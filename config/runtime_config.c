#include "runtime_config.h"
#include "platforms.h"
#include "../common.h"
#include "cmdline_delegate.h"

struct runtime_config current_config = {
    .hw = { '\0' },
    .sn = { '\0' },
    .boot_media = {
        .vid = VID_PID_EMPTY,
        .pid = VID_PID_EMPTY
    },
    .mfg_mode = false,
    .port_thaw = true,
    .netif_num = 0,
    .macs = { '\0' },
    .cmdline_blacklist = { '\0' },
    .hw_config = NULL,
};

static inline bool validate_sn(const serial_no *sn) {
    if (*sn[0] == '\0') {
        pr_loc_err("Serial number is empty");
        return false;
    }

    //TODO: add more validation here, probably w/model?

    return true;
}

static inline bool validate_vid_pid(const struct boot_media *boot)
{
    if (boot->vid == VID_PID_EMPTY && boot->pid == VID_PID_EMPTY) {
        pr_loc_wrn("Empty/no \"%s\" and \"%s\" specified - first USB storage device will be used", CMDLINE_CT_VID,
                   CMDLINE_CT_PID);
        return true; //this isn't necessarily an error (e.g. running under a VM with only a single USB port)
    }

    if (boot->vid == VID_PID_EMPTY) { //PID=0 is valid, but the VID is not
        pr_loc_err("Empty/no \"%s\" specified", CMDLINE_CT_VID);
        return false;
    }

    return true;
    //not checking for >VID_PID_MAX as vid type is already ushort
}

static inline bool validate_nets(const unsigned short if_num, mac_address *macs[MAX_NET_IFACES])
{
    size_t mac_len;
    unsigned short macs_num = 0;
    for (; macs_num < MAX_NET_IFACES; macs_num++) {
        if (!macs[macs_num])
            break; //You cannot have gaps in macs array

        mac_len = strlen(*macs[macs_num]);
        if (mac_len != MAC_ADDR_LEN) {
            pr_loc_err("MAC address \"%s\" is invalid (expected %d characters, found %zu)", *macs[macs_num], MAC_ADDR_LEN,
                       mac_len);
        } //else if validate if the MAC is actually semi-valid
    }

    bool valid = true;
    if (if_num == 0) {
        pr_loc_err("Number of defined interfaces (\"%s\") is not specified or empty", CMDLINE_KT_NETIF_NUM);
        valid = false;
    }

    if (macs_num == 0) {
        pr_loc_err("No MAC addressed are specified - use \"%s\" or \"%s\"...\"%s\" to set them", CMDLINE_KT_MACS,
                   CMDLINE_KT_MAC1, CMDLINE_KT_MAC4);
        valid = false;
    }

    if (if_num != macs_num) {
        pr_loc_err("Number of defined interfaces (\"%s%d\") is not equal to the number of MAC addresses found (%d)",
                   CMDLINE_KT_NETIF_NUM, if_num, macs_num);
    }

    return valid;
}

static int populate_hw_config(struct runtime_config *config)
{
    //We cannot run with empty model or model which didn't match
    if (config->hw[0] == '\0') {
        pr_loc_crt("Empty model, please set \"%s\" parameter", CMDLINE_KT_HW);
        return -ENOENT;
    }

    for (int i = 0; i < ARRAY_SIZE(supported_platforms); i++) {
        if (strcmp(supported_platforms[i].name, (char *)config->hw) != 0)
            continue;

        pr_loc_dbg("Found platform definition for \"%s\"", config->hw);
        config->hw_config = &supported_platforms[i];
        return 0;
    }

    pr_loc_crt("The model set using \"%s%s\" is not valid", CMDLINE_KT_HW, config->hw);
    return -EINVAL;
}

static bool validate_runtime_config(const struct runtime_config *config)
{
    pr_loc_dbg("Validating runtime config...");
    bool valid = true;

    valid &= validate_sn(&config->sn);
    valid &= validate_vid_pid(&config->boot_media);
    valid &= validate_nets(config->netif_num, config->macs);

    if (valid) {
        pr_loc_dbg("Config validation resulted in %s", valid ? "OK" : "ERR");
        return 0;
    } else {
        pr_loc_err("Config validation FAILED");
        return -EINVAL;
    }
}

int populate_runtime_config(struct runtime_config *config)
{
    int out = 0;

    if ((out = populate_hw_config(config)) != 0 || (out = validate_runtime_config(config)) != 0) {
        pr_loc_err("Failed to populate runtime config!");
        return out;
    }

    pr_loc_inf("Runtime config populated");

    return out;
}

void free_runtime_config(struct runtime_config *config)
{
    for (int i = 0; i < MAX_NET_IFACES; i++) {
        if (config->macs[i]) {
            pr_loc_dbg("Free MAC%d @ %p", i, config->macs[i]);
            kfree(config->macs[i]);
        }
    }

    for (int i = 0; i < MAX_BLACKLISTED_CMDLINE_TOKENS; i++) {
        if (config->cmdline_blacklist[i]) {
            pr_loc_dbg("Free cmdline blacklist entry %d @ %p", i, config->cmdline_blacklist[i]);
            kfree(config->cmdline_blacklist[i]);
        }
    }

    pr_loc_inf("Runtime config freed");
}
