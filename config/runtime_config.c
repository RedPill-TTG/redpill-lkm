#include "runtime_config.h"
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
    .macs = { '\0' }
};

static inline bool validate_hw(const syno_hw *hw) {
    //TODO: placeholder - it should validate the model against a list

    if (*hw[0] == '\0') {
        pr_loc_err("Empty model, please set \"%s\" parameter", CMDLINE_KT_HW);
        return false;
    }

    return true;
}

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

bool validate_runtime_config(const struct runtime_config *config)
{
    bool valid = true;

    valid &= validate_hw(&config->hw);
    valid &= validate_sn(&config->sn);
    valid &= validate_vid_pid(&config->boot_media);
    valid &= validate_nets(config->netif_num, config->macs);

    return valid;
}

void free_runtime_config(struct runtime_config *config)
{
    //As of now only .macs are pointers which [potentially] will need manual freeing
    for (unsigned short i = 0; i < MAX_NET_IFACES; i++) {
        if (config->macs[i]) {
            pr_loc_dbg("Free MAC%d @ %p", i, config->macs[i]);
            kfree(config->macs[i]);
        }
    }
}
