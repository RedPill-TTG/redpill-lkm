/*
 * DO NOT include this file anywhere besides runtime_config.c - its format is meant to be internal to the configuration
 * parsing.
 */
#ifndef REDPILLLKM_PLATFORMS_H
#define REDPILLLKM_PLATFORMS_H

#include "../shim/pci_shim.h"
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
    },
};

#endif //REDPILLLKM_PLATFORMS_H