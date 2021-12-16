/*
 * DO NOT include this file anywhere besides runtime_config.c - its format is meant to be internal to the configuration
 * parsing.
 */
#ifndef REDPILLLKM_PLATFORMS_H
#define REDPILLLKM_PLATFORMS_H

#include "../shim/pci_shim.h"
#include "platform_types.h"
const struct hw_config supported_platforms[] = {
    {
        .name = "DS3615xs",
        .pci_stubs = {
                { .type = VPD_MARVELL_88SE9235, .bus = 0x07, .dev = 0x00, .fn = 0x00, .multifunction = false },
                { .type = VPD_MARVELL_88SE9235, .bus = 0x08, .dev = 0x00, .fn = 0x00, .multifunction = false },
                { .type = VPD_MARVELL_88SE9235, .bus = 0x09, .dev = 0x00, .fn = 0x00, .multifunction = false },
                { .type = VPD_MARVELL_88SE9235, .bus = 0x0a, .dev = 0x00, .fn = 0x00, .multifunction = false },
                { .type = __VPD_TERMINATOR__ }
        },
        .emulate_rtc = false,
        .swap_serial = true,
        .reinit_ttyS0 = false,
        .fix_disk_led_ctrl = false,
        .has_cpu_temp = true,
        .hwmon = {
            .sys_thermal = { HWMON_SYS_TZONE_REMOTE1_ID, HWMON_SYS_TZONE_LOCAL_ID, HWMON_SYS_TZONE_REMOTE2_ID },
            .sys_voltage = { HWMON_SYS_VSENS_VCC_ID, HWMON_SYS_VSENS_VPP_ID, HWMON_SYS_VSENS_V33_ID,
                            HWMON_SYS_VSENS_V5_ID, HWMON_SYS_VSENS_V12_ID },
            .sys_fan_speed_rpm = {HWMON_SYS_FAN1_ID, HWMON_SYS_FAN2_ID },
            .hdd_backplane = { HWMON_SYS_HDD_BP_NULL_ID },
            .psu_status = { HWMON_PSU_NULL_ID },
            .sys_current = { HWMON_SYS_CURR_NULL_ID },
        }
    },
    {
	.name = "DS3617xs",
        .pci_stubs = {
                { .type = VPD_MARVELL_88SE9215, .bus = 0x01, .dev = 0x00, .fn = 0x00, .multifunction = false },
                { .type = VPD_MARVELL_88SE9215, .bus = 0x02, .dev = 0x00, .fn = 0x00, .multifunction = false },
                { .type = VPD_MARVELL_88SE9235, .bus = 0x08, .dev = 0x00, .fn = 0x00, .multifunction = false },
                { .type = __VPD_TERMINATOR__ }
        },
        .emulate_rtc = false,
        .swap_serial = false,
        .reinit_ttyS0 = true,
        .fix_disk_led_ctrl = false,
        .has_cpu_temp = true,
        .hwmon = {
            .sys_thermal = { HWMON_SYS_TZONE_REMOTE1_ID, HWMON_SYS_TZONE_LOCAL_ID, HWMON_SYS_TZONE_REMOTE2_ID },
            .sys_voltage = { HWMON_SYS_VSENS_VCC_ID, HWMON_SYS_VSENS_VPP_ID, HWMON_SYS_VSENS_V33_ID,
                            HWMON_SYS_VSENS_V5_ID, HWMON_SYS_VSENS_V12_ID },
            .sys_fan_speed_rpm = {HWMON_SYS_FAN1_ID, HWMON_SYS_FAN2_ID },
            .hdd_backplane = { HWMON_SYS_HDD_BP_NULL_ID },
            .psu_status = { HWMON_PSU_NULL_ID },
            .sys_current = { HWMON_SYS_CURR_NULL_ID },
        }
    },
    {
            .name = "DS918+",
            .pci_stubs = {
                    { .type = VPD_MARVELL_88SE9215,    .bus = 0x01, .dev = 0x00, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_I211,          .bus = 0x02, .dev = 0x00, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_I211,          .bus = 0x03, .dev = 0x00, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_CPU_AHCI_CTRL, .bus = 0x00, .dev = 0x12, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_CPU_PCIE_PA,   .bus = 0x00, .dev = 0x13, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_CPU_PCIE_PB,   .bus = 0x00, .dev = 0x14, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_CPU_USB_XHCI,  .bus = 0x00, .dev = 0x15, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_CPU_I2C,       .bus = 0x00, .dev = 0x16, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_CPU_HSUART,    .bus = 0x00, .dev = 0x18, .fn = 0x00, .multifunction = false },
                    { .type = VPD_INTEL_CPU_SPI,       .bus = 0x00, .dev = 0x19, .fn = 0x02, .multifunction = true },
                    { .type = VPD_INTEL_CPU_SPI,       .bus = 0x00, .dev = 0x19, .fn = 0x00, .multifunction = true },
                    { .type = VPD_INTEL_CPU_SMBUS,     .bus = 0x00, .dev = 0x1f, .fn = 0x01, .multifunction = true },
                    { .type = VPD_INTEL_CPU_SMBUS,     .bus = 0x00, .dev = 0x1f, .fn = 0x00, .multifunction = true },

                    { .type = __VPD_TERMINATOR__ }
            },
            .emulate_rtc = true,
            .swap_serial = false,
            .reinit_ttyS0 = true,
            .fix_disk_led_ctrl = true,
            .has_cpu_temp = true,
            .hwmon = {
                .sys_thermal = { HWMON_SYS_TZONE_NULL_ID },
                .sys_voltage = { HWMON_SYS_VSENS_NULL_ID },
                .sys_fan_speed_rpm = { HWMON_SYS_FAN_NULL_ID },
                .hdd_backplane = { HWMON_SYS_HDD_BP_DETECT_ID, HWMON_SYS_HDD_BP_ENABLE_ID },
                .psu_status = { HWMON_PSU_NULL_ID },
                .sys_current = { HWMON_SYS_CURR_NULL_ID },
            }
    },
};

#endif //REDPILLLKM_PLATFORMS_H
