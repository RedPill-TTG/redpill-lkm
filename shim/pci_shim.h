#ifndef REDPILL_PCI_SHIM_H
#define REDPILL_PCI_SHIM_H

enum pci_shim_device_type {
    __VPD_TERMINATOR__,
    VPD_MARVELL_88SE9235, //1b4b:9235
    VPD_MARVELL_88SE9215, //1b4b:9215
    VPD_INTEL_I211, //8086:1539
    VPD_INTEL_CPU_AHCI_CTRL, //8086:5ae3
    VPD_INTEL_CPU_PCIE_PA, //8086:5ad8
    VPD_INTEL_CPU_PCIE_PB, //8086:5ad6
    VPD_INTEL_CPU_USB_XHCI, //8086:5aa8
    VPD_INTEL_CPU_I2C, //8086:5aac
    VPD_INTEL_CPU_HSUART, //8086:5abc
    VPD_INTEL_CPU_SPI, //8086:5ac6
    VPD_INTEL_CPU_SMBUS, //8086:5ad4
};

typedef struct hw_config hw_config_;
int register_pci_shim(const struct hw_config *hw);
int unregister_pci_shim(void);

#endif //REDPILL_PCI_SHIM_H
