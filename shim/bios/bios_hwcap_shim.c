/**
 * Overrides GetHwCapability to provide additional capabilities for older platforms (e.g. 3615xs)
 */
#include "bios_hwcap_shim.h"
#include "../../common.h"
#include "../shim_base.h"
#include "../../internal/override/override_symbol.h" //overriding GetHWCapability
#include "../../config/platform_types.h" //hw_config, platform_has_hwmon_*
#include <linux/synobios.h> //CAPABILITY_*, CAPABILITY

#define SHIM_NAME "mfgBIOS HW Capability"

static const struct hw_config *hw_config = NULL;
static override_symbol_inst *GetHwCapability_ovs = NULL;

static void dbg_compare_cap_value(SYNO_HW_CAPABILITY id, int computed_support)
{
#ifdef DBG_HWCAP
    int org_fout = -1;
    CAPABILITY org_cap = { '\0' };
    org_cap.id = id;
    int ovs_fout = call_overridden_symbol(org_fout, GetHwCapability_ovs, &org_cap);

    pr_loc_dbg("comparing GetHwCapability(id=%d)->support => computed=%d vs. real=%d [org_fout=%d, ovs_fout=%d]", id,
               computed_support, org_cap.support, org_fout, ovs_fout);
#endif
}

static int GetHwCapability_shim(CAPABILITY *cap)
{
    if (unlikely(!cap)) {
        pr_loc_err("Got NULL-ptr to %s", __FUNCTION__);
        return -EINVAL;
    }

    switch (cap->id) {
        case CAPABILITY_THERMAL:
            cap->support = platform_has_hwmon_thermal(hw_config) ? 1 : 0;
            dbg_compare_cap_value(cap->id, cap->support);
            return 0;

        case CAPABILITY_CPU_TEMP:
            cap->support = hw_config->has_cpu_temp;
            dbg_compare_cap_value(cap->id, cap->support);
            return 0;

        case CAPABILITY_FAN_RPM_RPT:
            cap->support = platform_has_hwmon_fan_rpm(hw_config) ? 1 : 0;
            dbg_compare_cap_value(cap->id, cap->support);
            return 0;

        case CAPABILITY_DISK_LED_CTRL:
        case CAPABILITY_AUTO_POWERON:
        case CAPABILITY_S_LED_BREATH:
        case CAPABILITY_MICROP_PWM:
        case CAPABILITY_CARDREADER:
        case CAPABILITY_LCM: {
            if (unlikely(!GetHwCapability_ovs)) {
                pr_loc_bug("%s() was called with proxy need when no OVS was available", __FUNCTION__);
                return -EIO;
            }

            int org_fout = -1;
            int ovs_fout = call_overridden_symbol(org_fout, GetHwCapability_ovs, cap);
            pr_loc_dbg("proxying GetHwCapability(id=%d)->support => real=%d [org_fout=%d, ovs_fout=%d]", cap->id,
                       cap->support, org_fout, ovs_fout);

            return org_fout;
        }

        default:
            pr_loc_err("unknown GetHwCapability(id=%d) => out=-EINVAL", cap->id);
            return -EINVAL;
    }
}

int register_bios_hwcap_shim(const struct hw_config *hw)
{
    shim_reg_in();

    if (unlikely(GetHwCapability_ovs))
        shim_reg_already();

    hw_config = hw;
    override_symbol_or_exit_int(GetHwCapability_ovs, "GetHwCapability", GetHwCapability_shim);

    shim_reg_ok();
    return 0;
}

int unregister_bios_hwcap_shim(void)
{
    shim_ureg_in();

    if (unlikely(!GetHwCapability_ovs))
        return 0; //this is deliberately a noop

    int out = restore_symbol(GetHwCapability_ovs);
    if (unlikely(out != 0)) {
        pr_loc_err("Failed to restore GetHwCapability - error=%d", out);
        return out;
    }
    GetHwCapability_ovs = NULL;

    shim_ureg_ok();
    return 0;
}

int reset_bios_hwcap_shim(void)
{
    shim_reset_in();
    put_overridden_symbol(GetHwCapability_ovs);
    GetHwCapability_ovs = NULL;

    shim_reset_ok();
    return 0;
}