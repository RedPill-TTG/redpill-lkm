#include "pci_shim.h"
#include "../common.h"
#include "../config/runtime_config.h"
#include "../internal/virtual_pci.h"
#include <linux/pci_ids.h>

unsigned int free_dev_idx = 0;
static void *devices[MAX_VPCI_DEVS] = { NULL };

#define allocate_vpci_dev_dsc()                                                                   \
    if (free_dev_idx >= MAX_VPCI_DEVS) {                                                          \
        /*index has to be at max MAX_VPCI_DEVS-1*/                                                \
        pr_loc_bug("No more device indexes are available (max devs: %d)", MAX_VPCI_DEVS);         \
        return -ENOMEM;                                                                           \
    }                                                                                             \
    struct pci_dev_descriptor *dev_dsc = kmalloc(sizeof(struct pci_dev_descriptor), GFP_KERNEL);  \
    if (!dev_dsc) {                                                                               \
        pr_loc_crt("kmalloc failed");                                                             \
        return -EFAULT;                                                                           \
    }                                                                                             \
    memcpy(dev_dsc, &pci_dev_conf_default_normal_dev, sizeof(struct pci_dev_descriptor));         \
    devices[free_dev_idx++] = dev_dsc;

#define add_vdev_and_return() \
    const struct virtual_device *vpci_vdev = vpci_add_device(bus_no, 0x00, 0x00, dev_dsc);        \
    return IS_ERR(vpci_vdev) ? PTR_ERR(vpci_vdev) : 0;

/**
 * Adds a fake Marvell 88SE9235 controller
 *
 * These errors in kernlog are normal (as we don't emulate the behavior of the controller as it's not needed):
 *   pci 0001:0a:00.0: Can't map mv9235 registers
 *   ahci: probe of 0001:0a:00.0 failed with error -22
 *
 * @return 0 on success or -E
 */
static int vdev_add_88SE9235(unsigned char bus_no)
{
    allocate_vpci_dev_dsc();
    dev_dsc->vid = PCI_VENDOR_ID_MARVELL_EXT;
    dev_dsc->dev = 0x9235;
    dev_dsc->rev_id = 0x11;
    dev_dsc->class = U24_CLASS_TO_U8_CLASS(PCI_CLASS_STORAGE_SATA_AHCI);
    dev_dsc->subclass = U24_CLASS_TO_U8_SUBCLASS(PCI_CLASS_STORAGE_SATA_AHCI);
    dev_dsc->prog_if = U24_CLASS_TO_U8_SUBCLASS(PCI_CLASS_STORAGE_SATA_AHCI);
    add_vdev_and_return();
}

int register_pci_shim(void)
{
    //TODO: these are now hardcoded for 3615xs but this can be easily adapted by checking model passed to this function
    int error = 0;
    if (
            (error = vdev_add_88SE9235(0x07)) != 0
         || (error = vdev_add_88SE9235(0x08)) != 0
         || (error = vdev_add_88SE9235(0x09)) != 0
         || (error = vdev_add_88SE9235(0x0a)) != 0
       ) {
        pr_loc_err("PCI shim registration FAILED");

        return error;
    }

    pr_loc_inf("PCI shim registered");

    return 0;
}

int unregister_pci_shim(void)
{
    vpci_remove_all_devices_and_buses();

    for (int i = 0; i < free_dev_idx; i++) {
        pr_loc_dbg("Free PCI dev %d @ %p", i, devices[i]);
        kfree(devices[i]);
    }

    pr_loc_inf("PCI shim unregistered (but it's buggy!)");

    return -EIO; //vpci_remove_all_devices_and_buses has a bug - this is a canary to not forget
}

