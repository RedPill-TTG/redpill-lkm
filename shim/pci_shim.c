#define SHIM_NAME "PCI devices"

#include "pci_shim.h"
#include "shim_base.h"
#include "../common.h"
#include "../config/vpci_types.h" //MAX_VPCI_DEVS, pci_shim_device_type
#include "../config/platform_types.h" //hw_config
#include "../internal/virtual_pci.h"
#include <linux/pci_ids.h>

unsigned int free_dev_idx = 0;
static void *devices[MAX_VPCI_DEVS] = { NULL };

static struct pci_dev_descriptor *allocate_vpci_dev_dsc(void) {
    if (free_dev_idx >= MAX_VPCI_DEVS) {
        /*index has to be at max MAX_VPCI_DEVS-1*/
        pr_loc_bug("No more device indexes are available (max devs: %d)", MAX_VPCI_DEVS);
        return ERR_PTR(-ENOMEM);
    }

    struct pci_dev_descriptor *dev_dsc;
    kmalloc_or_exit_ptr(dev_dsc, sizeof(struct pci_dev_descriptor));
    memcpy(dev_dsc, &pci_dev_conf_default_normal_dev, sizeof(struct pci_dev_descriptor));
    devices[free_dev_idx++] = dev_dsc;

    return dev_dsc;
}
#define allocate_vpci_dev_dsc_var() \
    struct pci_dev_descriptor *dev_dsc = allocate_vpci_dev_dsc(); \
    if (IS_ERR(dev_dsc)) return PTR_ERR(dev_dsc);
    
static int
add_vdev(struct pci_dev_descriptor *dev_dsc, unsigned char bus_no, unsigned char dev_no, unsigned char fn_no,
                    bool is_mf)
{
    const struct virtual_device *vpci_vdev;

    if (is_mf) {
        vpci_vdev = vpci_add_multifunction_device(bus_no, dev_no, fn_no, dev_dsc);
    } else if(unlikely(fn_no != 0x00)) {
        //Making such config will either cause the device to not show up at all or only fn_no=0 one will show u
        pr_loc_bug("%s called with non-MF device but non-zero fn_no", __FUNCTION__);
        return -EINVAL;
    } else {
        vpci_vdev = vpci_add_single_device(bus_no, dev_no, dev_dsc);
    }

    return IS_ERR(vpci_vdev) ? PTR_ERR(vpci_vdev) : 0;
}

/**
 * Adds a fake Marvell controller
 *
 * These errors in kernlog are normal (as we don't emulate the behavior of the controller as it's not needed):
 *   pci 0001:0a:00.0: Can't map mv9235 registers
 *   ahci: probe of 0001:0a:00.0 failed with error -22
 *
 * @return 0 on success or -E
 */
static inline int
vdev_add_generic_marvell_ahci(u16 dev, unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    allocate_vpci_dev_dsc_var();
    dev_dsc->vid = PCI_VENDOR_ID_MARVELL_EXT;
    dev_dsc->dev = dev;
    dev_dsc->rev_id = 0x11; //All Marvells so far use revision 11
    dev_dsc->class = U24_CLASS_TO_U8_CLASS(PCI_CLASS_STORAGE_SATA_AHCI);
    dev_dsc->subclass = U24_CLASS_TO_U8_SUBCLASS(PCI_CLASS_STORAGE_SATA_AHCI);
    dev_dsc->prog_if = U24_CLASS_TO_U8_PROGIF(PCI_CLASS_STORAGE_SATA_AHCI);
    return add_vdev(dev_dsc, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_MARVELL_88SE9235(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    return vdev_add_generic_marvell_ahci(0x9235, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_MARVELL_88SE9215(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    return vdev_add_generic_marvell_ahci(0x9215, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_I211(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    allocate_vpci_dev_dsc_var();
    dev_dsc->vid = PCI_VENDOR_ID_INTEL;
    dev_dsc->dev = 0x1539;
    dev_dsc->rev_id = 0x03; //Not confirmed
    dev_dsc->class = U16_CLASS_TO_U8_CLASS(PCI_CLASS_NETWORK_ETHERNET);
    dev_dsc->subclass = U16_CLASS_TO_U8_SUBCLASS(PCI_CLASS_NETWORK_ETHERNET);
    return add_vdev(dev_dsc, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_CPU_AHCI_CTRL(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    allocate_vpci_dev_dsc_var();
    dev_dsc->vid = PCI_VENDOR_ID_INTEL;
    dev_dsc->dev = 0x5ae3;
    dev_dsc->class = U24_CLASS_TO_U8_CLASS(PCI_CLASS_STORAGE_SATA_AHCI);
    dev_dsc->subclass = U24_CLASS_TO_U8_SUBCLASS(PCI_CLASS_STORAGE_SATA_AHCI);
    dev_dsc->prog_if = U24_CLASS_TO_U8_PROGIF(PCI_CLASS_STORAGE_SATA_AHCI);
    return add_vdev(dev_dsc, bus_no, dev_no, fn_no, is_mf);
}

//This technically should be a bridge but we don't have the info to recreate full tree
static inline int
vdev_add_generic_intel_pcie(u16 dev, unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf) {
    allocate_vpci_dev_dsc_var();
    dev_dsc->vid = PCI_VENDOR_ID_INTEL;
    dev_dsc->dev = dev;
    dev_dsc->class = U16_CLASS_TO_U8_CLASS(PCI_CLASS_BRIDGE_PCI);
    dev_dsc->subclass = U16_CLASS_TO_U8_SUBCLASS(PCI_CLASS_BRIDGE_PCI);
    return add_vdev(dev_dsc, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_CPU_PCIE_PA(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    return vdev_add_generic_intel_pcie(0x5ad8, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_CPU_PCIE_PB(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    return vdev_add_generic_intel_pcie(0x5ad6, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_CPU_USB_XHCI(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    allocate_vpci_dev_dsc_var();
    dev_dsc->vid = PCI_VENDOR_ID_INTEL;
    dev_dsc->dev = 0x5aa8;
    dev_dsc->class = U24_CLASS_TO_U8_CLASS(PCI_CLASS_SERIAL_USB_XHCI);
    dev_dsc->subclass = U24_CLASS_TO_U8_SUBCLASS(PCI_CLASS_SERIAL_USB_XHCI);
    dev_dsc->prog_if = U24_CLASS_TO_U8_PROGIF(PCI_CLASS_SERIAL_USB_XHCI);
    return add_vdev(dev_dsc, bus_no, dev_no, fn_no, is_mf);
}

static inline int
vdev_add_generic_intel_io(u16 dev, unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    allocate_vpci_dev_dsc_var();
    dev_dsc->vid = PCI_VENDOR_ID_INTEL;
    dev_dsc->dev = dev;
    dev_dsc->class = U16_CLASS_TO_U8_CLASS(PCI_CLASS_SP_OTHER);
    dev_dsc->subclass = U16_CLASS_TO_U8_SUBCLASS(PCI_CLASS_SP_OTHER);
    return add_vdev(dev_dsc, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_CPU_I2C(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    return vdev_add_generic_intel_io(0x5aac, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_CPU_HSUART(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    return vdev_add_generic_intel_io(0x5abc, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_CPU_SPI(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    return vdev_add_generic_intel_io(0x5ac6, bus_no, dev_no, fn_no, is_mf);
}

static int vdev_add_INTEL_CPU_SMBUS(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf)
{
    allocate_vpci_dev_dsc_var();
    dev_dsc->vid = PCI_VENDOR_ID_INTEL;
    dev_dsc->dev = 0x5ad4;
    dev_dsc->class = U16_CLASS_TO_U8_CLASS(PCI_CLASS_SERIAL_SMBUS);
    dev_dsc->subclass = U16_CLASS_TO_U8_SUBCLASS(PCI_CLASS_SERIAL_SMBUS);

    return add_vdev(dev_dsc, bus_no, dev_no, fn_no, is_mf);
}

static int (*dev_type_handler_map[])(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, bool is_mf) = {
        [VPD_MARVELL_88SE9235] = vdev_add_MARVELL_88SE9235,
        [VPD_MARVELL_88SE9215] = vdev_add_MARVELL_88SE9215,
        [VPD_INTEL_I211] = vdev_add_INTEL_I211,
        [VPD_INTEL_CPU_AHCI_CTRL] = vdev_add_INTEL_CPU_AHCI_CTRL,
        [VPD_INTEL_CPU_PCIE_PA] = vdev_add_INTEL_CPU_PCIE_PA,
        [VPD_INTEL_CPU_PCIE_PB] = vdev_add_INTEL_CPU_PCIE_PB,
        [VPD_INTEL_CPU_USB_XHCI] = vdev_add_INTEL_CPU_USB_XHCI,
        [VPD_INTEL_CPU_I2C] = vdev_add_INTEL_CPU_I2C,
        [VPD_INTEL_CPU_HSUART] = vdev_add_INTEL_CPU_HSUART,
        [VPD_INTEL_CPU_SPI] = vdev_add_INTEL_CPU_SPI,
        [VPD_INTEL_CPU_SMBUS] = vdev_add_INTEL_CPU_SMBUS,
};

int register_pci_shim(const struct hw_config *hw)
{
    shim_reg_in();

    pr_loc_dbg("Creating vPCI devices for %s", hw->name);
    int out;
    for (int i = 0; i < MAX_VPCI_DEVS; i++) {
        if (hw->pci_stubs[i].type == __VPD_TERMINATOR__)
            break;

        pr_loc_dbg("Calling %ps with B:D:F=%02x:%02x:%02x mf=%d", dev_type_handler_map[hw->pci_stubs[i].type],
                   hw->pci_stubs[i].bus, hw->pci_stubs[i].dev, hw->pci_stubs[i].fn,
                   hw->pci_stubs[i].multifunction ? 1 : 0);

        out = dev_type_handler_map[hw->pci_stubs[i].type](hw->pci_stubs[i].bus, hw->pci_stubs[i].dev,
                                                          hw->pci_stubs[i].fn, hw->pci_stubs[i].multifunction);

        if (out != 0) {
            pr_loc_err("Failed to create vPCI device B:D:F=%02x:%02x:%02x - error=%d", hw->pci_stubs[i].bus,
                       hw->pci_stubs[i].dev, hw->pci_stubs[i].fn, out);
            return out;
        }

        pr_loc_dbg("vPCI device %d created successfully", i+1);
    }

    shim_reg_ok();
    return 0;
}

int unregister_pci_shim(void)
{
    shim_ureg_in();
    vpci_remove_all_devices_and_buses();

    for (int i = 0; i < free_dev_idx; i++) {
        pr_loc_dbg("Free PCI dev %d @ %p", i, devices[i]);
        kfree(devices[i]);
    }

    shim_ureg_ok();
    return -EIO; //vpci_remove_all_devices_and_buses has a bug - this is a canary to not forget
}
