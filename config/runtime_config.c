#include "runtime_config.h"
#include "platforms.h"
#include "../common.h"
#include "cmdline_delegate.h"
#include "uart_defs.h"

struct runtime_config current_config = {
    .hw = { '\0' },
    .sn = { '\0' },
    .boot_media = {
        .type = BOOT_MEDIA_USB,
        .mfg_mode = false,
        .vid = VID_PID_EMPTY,
        .pid = VID_PID_EMPTY,
        .dom_size_mib = 1024, //usually the image will be used with ESXi and thus it will be ~100MB anyway
    },
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

static __always_inline bool validate_boot_dev_usb(const struct boot_media *boot)
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

    pr_loc_dbg("Configured boot device type to USB");
    return true;
    //not checking for >VID_PID_MAX as vid type is already ushort
}

static __always_inline bool validate_boot_dev_sata_dom(const struct boot_media *boot)
{
#ifndef NATIVE_SATA_DOM_SUPPORTED
    pr_loc_err("Kernel you are running a kernel was built without SATA DoM support, you cannot use %s%c. "
               "You can try booting with %s%c to enable experimental fake-SATA DoM.",
               CMDLINE_KT_SATADOM, CMDLINE_KT_SATADOM_NATIVE,
               CMDLINE_KT_SATADOM, CMDLINE_KT_SATADOM_FAKE);
    return false;
#endif

    if (boot->vid != VID_PID_EMPTY || boot->pid != VID_PID_EMPTY)
        pr_loc_wrn("Using native SATA-DoM boot - %s and %s parameter values will be ignored",
                   CMDLINE_CT_VID, CMDLINE_CT_PID);

    //this config is impossible as there's no equivalent for force-reinstall boot on SATA, so it's better to detect
    //that rather than causing WTFs for someone who falsely assuming that it's possible
    //However, it does work with fake-SATA boot (as it emulates USB disk anyway)
    if (boot->mfg_mode) {
        pr_loc_err("You cannot combine %s%c with %s - the OS supports force-reinstall on USB and fake SATA disk only",
                   CMDLINE_KT_SATADOM, CMDLINE_KT_SATADOM_NATIVE, CMDLINE_CT_MFG);
        return false;
    }

    pr_loc_dbg("Configured boot device type to fake-SATA DOM");
    return true;
}

static __always_inline bool validate_boot_dev_sata_disk(const struct boot_media *boot)
{
#ifdef NATIVE_SATA_DOM_SUPPORTED
    pr_loc_wrn("The kernel you are running supports native SATA DoM (%s%c). You're currently using an experimental "
               "fake-SATA DoM (%s%c) - consider switching to native SATA DoM (%s%c) for more stable operation.",
               CMDLINE_KT_SATADOM, CMDLINE_KT_SATADOM_NATIVE,
               CMDLINE_KT_SATADOM, CMDLINE_KT_SATADOM_FAKE,
               CMDLINE_KT_SATADOM, CMDLINE_KT_SATADOM_NATIVE);
#endif

    if (boot->vid != VID_PID_EMPTY || boot->pid != VID_PID_EMPTY)
        pr_loc_wrn("Using fake SATA disk boot - %s and %s parameter values will be ignored",
                   CMDLINE_CT_VID, CMDLINE_CT_PID);

    pr_loc_dbg("Configured boot device type to fake-SATA DOM");
    return true;
}

static inline bool validate_boot_dev(const struct boot_media *boot)
{
    switch (boot->type) {
        case BOOT_MEDIA_USB:
            return validate_boot_dev_usb(boot);
        case BOOT_MEDIA_SATA_DOM:
            return validate_boot_dev_sata_dom(boot);
        case BOOT_MEDIA_SATA_DISK:
            return validate_boot_dev_sata_disk(boot);
        default:
            pr_loc_bug("Got unknown boot type - did you forget to update %s after changing cmdline parsing?",
                       __FUNCTION__);
            return false;

    }
}

static inline bool validate_nets(const unsigned short if_num, mac_address * const macs[MAX_NET_IFACES])
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

/**
 * This function validates consistency of the currently loaded platform config with the current environment
 *
 * Some options don't make sense unless the kernel was built with some specific configuration. This function aims to
 * detect common pitfalls in platforms configuration. This doesn't so much validate the platform definition per se
 * (but partially too) but the match between platform config chosen vs. kernel currently attempting to run that
 * platform.
 */
static inline bool validate_platform_config(const struct hw_config *hw)
{
#ifdef UART_BUG_SWAPPED
    const bool kernel_serial_swapped = true;
#else
    const bool kernel_serial_swapped = false;
#endif

    //This will not prevent the code from working, so it's not an error state by itself
    if (unlikely(hw->swap_serial && !kernel_serial_swapped))
        pr_loc_bug("Your kernel indicates COM1 & COM2 ARE NOT swapped but your platform specifies swapping");
    else if(unlikely(!hw->swap_serial && kernel_serial_swapped))
        pr_loc_bug("Your kernel indicates COM1 & COM2 ARE swapped but your platform specifies NO swapping");

    return true;
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
    valid &= validate_boot_dev(&config->boot_media);
    valid &= validate_nets(config->netif_num, config->macs);
    valid &= validate_platform_config(config->hw_config);

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
