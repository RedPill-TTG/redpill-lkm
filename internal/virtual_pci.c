/*
 * This file is a SIMPLE (yes, this IS simple) software emulation layer for PCI devices.
 *
 * Before you even start reading it you need to get familiar with references listed below. As the kernel people put it
 * mildly "The world of PCI is vast and full of (mostly unpleasant) surprises.". This module tries to abstract hardware
 * space emulation into highest level API possible.
 *
 *
 * QUICK INTRODUCTION
 * ------------------
 * To use it you need to supply a descriptor (e.g. struct pci_dev_descriptor) and give it domain-unique combination of
 * {bus#, device#, function#}. The domain for most (all?) physical devices is usually 0x0000. This module uses 0x0001 to
 * avoid conflicts.
 * Fast PCI facts (read this to add devices):
 *  - every device is in the system has a location of BDF (256 buses max, 32 devices/bus, 8 functions/device = 65536)
 *  - every device MUST contain a function 0 (and may contain 1-7)
 *  - function is kind-of a subdevice (e.g. a quad-port network card will usually have functions 0-3)
 *  - you should (but you don't HAVE to) set "master bus" (.command |= PCI_COMMAND_MASTER) for every function 0 device
 *    instance
 *  - every device MUST have a valid VID/DEV. None of the fields can be 0x0000 or 0xFFFF (they have special meanings)
 *  - this module does NOT have any support for capabilities (CAPs) as they're variable length and we don't want to
 *    force every device struct to take 256K of memory (todo if needed?)
 *  - there are three types of headers: PCI device, PCI-PCI bridge, PCI-CardBus bridge. Only the first one was tested.
 *    The second one allows for more levels of the tree and should work if configured properly (see struct
 *    pci_pci_bridge_descriptor) but it wasn't needed yet. The third one is practically a bitrot now.
 *  - EVERYTHING IN PCI IS LITTLE ENDIAN no matter what your CPU says. Triple check if you're setting values correctly.
 *    Then you realize you set them incorrectly.
 *  - "devfn" in Linux terminology does NOT mean "device function" but rather "device# and function#". It is described
 *    in drivers/pci/search.c as "encodes number of PCI slot in which the desired PCI device resides and the logical
 *    device number within that slot in case of multi-function devices".
 *    You can use macros DEVFN_COMBO_TO_DEV_NO() and DEVFN_COMBO_TO_DEV_FN() to get dev# and fn# from that field
 *  - Linux provides class & subclass constants (PCI_CLASS_* in include/linux/pci_ids.h). However they're defined as
 *    either:
 *     - 8 bit class
 *       - e.g. PCI_BASE_CLASS_SERIAL [0x0c]
 *       - can be put into pci_dev_descriptor.class directly
 *     - 16 bit class+subclass
 *       - e.g. PCI_CLASS_SERIAL_USB [0x0c03]
 *       - use U16_CLASS_TO_U8_CLASS(PCI_CLASS_SERIAL_USB) for pci_dev_descriptor.class [0x0c]
 *       - use U16_CLASS_TO_U8_SUBCLASS(PCI_CLASS_SERIAL_USB) for pci_dev_descriptor.subclass [0x03]
 *     - 24 bit (sic!) class+subclass+prog_if
 *       - e.g. PCI_CLASS_SERIAL_USB_EHCI (0x0c0320)
 *       - use U24_CLASS_TO_U8_CLASS(PCI_CLASS_SERIAL_USB) for pci_dev_descriptor.class [0x0c]
 *       - use U24_CLASS_TO_U8_SUBCLASS(PCI_CLASS_SERIAL_USB) for pci_dev_descriptor.subclass [0x03]
 *       - use U24_CLASS_TO_U8_PROGIF(PCI_CLASS_SERIAL_USB) for pci_dev_descriptor.prog_if [0x03]
 *  - "pci_dev_conf_default_normal_dev" provides a sane-default device where you need to only set: vid, dev, class,
 *    and subclass.
 *
 *
 * DEBUGGING DEVICES
 * -----------------
 * To see the tree you can use "lspci -tvnn". Here's a quick cheat-sheet from the output format:
 *  0001:0a:00.0 Class 0000: Device 1b4b:9235 (rev ff)
 *   ^   ^  ^  ^       ^            ^    ^         ^
 *   |   |  |  |       |            |    |         |_______  pci_dev_descriptor.class_revision (lower 24 bits)
 *   |   |  |  |       |            |    |_________________  pci_dev_descriptor.dev (device ID)
 *   |   |  |  |       |            |______________________  pci_dev_descriptor.vid (vendor ID)
 *   |   |  |  |       |___________________________________  pci_dev_descriptor.class_revision (higher 24 bits)
 *   |   |  |  |___________________________________________  PCI device function
 *   |   |  |______________________________________________  device num on the bus
 *   |   |_________________________________________________  PCI bus no
 *   |_____________________________________________________  PCIBUS_VIRTUAL_DOMAIN
 *
 *
 * To debug the Linux PCI subsytem side of things these will be useful:
 *  echo 'file probe.c +p' > /sys/kernel/debug/dynamic_debug/control
 *  echo 'file search.c +p' > /sys/kernel/debug/dynamic_debug/control
 *  echo 'file delete.c +p' > /sys/kernel/debug/dynamic_debug/control
 *
 *
 * INTERNAL STRUCTURE
 * ------------------
 * The module emulates PCI on the lowest possible level - it literally fakes the otherwise-physical memory of
 * configuration registries.
 *
 * The two header types are memory-mapped as follows: (PCI-CardBus isn't shown as nobody uses that)
 *          HEADER TYPE 0x00 (Normal Device)                        HEADER TYPE 0x01 (PCI-PCI Bridge)
 * 31                  16 15                    0 hh       31                 16 15                     0 hh
 * ╠══════════╩══════════╬══════════╩═══════════╬════      ╠═════════╩══════════╬═══════════╩═══════════╬════
 * ║      Device ID      ║       Vendor ID      ║ 00       ║      Device ID     ║       Vendor ID       ║ 00
 * ╠═════════════════════╬══════════════════════╬════      ╠════════════════════╬═══════════════════════╬════
 * ║        Status       ║        Command       ║ 04       ║       Status       ║        Command        ║ 04
 * ╠══════════╦══════════╬══════════╦═══════════╬════      ╠═════════╦══════════╬═══════════╦═══════════╬════
 * ║   Class  ║ Subclass ║  ProgIF  ║  Rev. ID  ║ 08       ║  Class  ║ Subclass ║   ProgIF  ║  Rev. ID  ║ 08
 * ╠══════════╬══════════╬══════════╬═══════════╬════      ╠═════════╬══════════╬═══════════╬═══════════╬════
 * ║   BIST   ║  HeaderT ║ Lat.Tmr. ║  Cache LS ║ 0c       ║   BIST  ║  HeaderT ║  Lat.Tmr. ║  Cache LS ║ 0c
 * ╠══════════╩══════════╩══════════╩═══════════╬════      ╠═════════╩══════════╩═══════════╩═══════════╬════
 * ║                    BAR0                    ║ 10       ║                    BAR0                    ║ 10
 * ╠════════════════════════════════════════════╬════      ╠════════════════════════════════════════════╬════
 * ║                    BAR1                    ║ 14       ║                    BAR1                    ║ 14
 * ╠════════════════════════════════════════════╬════      ╠═════════╦══════════╦═══════════╦═══════════╬════
 * ║                    BAR2                    ║ 18       ║ SecLatT ║ SubordB# ║  SecBus#  ║  PriBus#  ║ 18
 * ╠════════════════════════════════════════════╬════      ╠═════════╩══════════╬═══════════╬═══════════╬════
 * ║                    BAR3                    ║ 1c       ║  Secondary Status  ║ I/O Limit ║  I/O Base ║ 1c
 * ╠════════════════════════════════════════════╬════      ╠════════════════════╬═══════════╩═══════════╬════
 * ║                    BAR4                    ║ 20       ║    Memory limit    ║      Memory base      ║ 20
 * ╠════════════════════════════════════════════╬════      ╠════════════════════╬═══════════════════════╬════
 * ║                    BAR5                    ║ 24       ║  Prefetch. Mem. L. ║   Prefetch. Mem. B.   ║ 24
 * ╠════════════════════════════════════════════╬════      ╠════════════════════╩═══════════════════════╬════
 * ║               Cardbus CIS ptr              ║ 28       ║       Prefetchable Base Upper 32 bit       ║ 28
 * ╠═════════════════════╦══════════════════════╬════      ╠════════════════════════════════════════════╬════
 * ║      Subsys ID      ║      Subsys VID      ║ 2c       ║       Prefetchable Limit Upper 32 bit      ║ 2c
 * ╠═════════════════════╩══════════════════════╬════      ╠════════════════════╦═══════════════════════╬════
 * ║             Exp. ROM Base Addr.            ║ 30       ║  I/O Lim. Up. 16b  ║    I/O Base Up. 16b   ║ 30
 * ╠════════════════════════════════╦═══════════╬════      ╠════════════════════╩═══════════╦═══════════╬════
 * ║              *RSV*             ║  Cap. ptr ║ 34       ║              *RSV*             ║  Cap. ptr ║ 34
 * ╠════════════════════════════════╩═══════════╬════      ╠════════════════════════════════╩═══════════╬════
 * ║                    *RSV*                   ║ 38       ║             Exp. ROM Base Addr.            ║ 38
 * ╠══════════╦══════════╦══════════╦═══════════╬════      ╠════════════════════╦═══════════╦═══════════╬════
 * ║ Max Lat. ║ Min Gnt. ║ Int. pin ║ Int. lin. ║ 3c       ║   Bridge Control   ║  Int. pin ║ Int. lin. ║ 3c
 * ╠══════════╩══════════╩══════════╩═══════════╬═══════   ╠════════════════════╩═══════════╩═══════════╬═══════
 * ║    Optional Dev.-Dep. Config (192 bytes)   ║ 40-100   ║    Optional Dev.-Dep. Config (192 bytes)   ║ 40-100
 * ╚════════════════════════════════════════════╩═══════   ╚════════════════════════════════════════════╩═══════
 *
 *
 * LINUX PCI SUBSYSTEM SCANNING ROUTINE
 * ------------------------------------
 * The kernel has a surprisingly readable code for the PCI scanning. We recommend starting from drivers/pci/probe.c and
 * "struct pci_bus *pci_scan_bus()" function.
 * In a big simplification it goes something like this:
 *  probe.c
 *  pci_scan_bus()
 *     => pci_scan_child_bus
 *         => loop pci_scan_slot(bus, devfn) with devfn=<0,0x100> every 8 bytes
 *         => pci_scan_single_device
 *             => pci_get_slot to check if device already exists
 *             => pci_scan_device to probe the device
 *                 => pci_bus_read_dev_vendor_id
 *                     => .... [and others]
 *             => pci_device_add if device probe succeeded
 *
 * THE ACPI SAGA
 * -------------
 * If you were thinking PCI is hard you haven't heard about ACPI. Kernels starting from v3.13 require ACPI companion
 * for PCI devices when the system was configured to run on an ACPI-complain x86 platform. This isn't an unusual
 * assumption. Before v3.13 the struct x86_sysdata contained a simple ACPI handle, which could be NULL. Now it should
 * contain a structure. However it still PROBABLY can be NULL.
 * See https://github.com/torvalds/linux/commit/7b1998116bbb2f3e5dd6cb9a8ee6db479b0b50a9 for details of that change.
 *
 * When the structure (=ACPI data) is NULL the error "ACPI: \: failed to evaluate _DSM (0x1001)" will be logged upon
 * scanning. However it seems to be harmless. There are two ways to get rid of this error: 1) Implement a proper ACPI
 * _DSM [no, just NO], or 2) user override_symbol() for acpi_evaluate_dsm() with a function doing the following (for the
 * time of scanning ONLY):
 *   union acpi_object *obj = kmalloc(sizeof(union acpi_object), GFP_KERNEL);
 *   obj->type = ACPI_TYPE_INTEGER;
 *   obj->integer.value = 1;
 *   return obj;
 *
 * x86 BUS SCANNING BUG (>=v4.1)
 * -----------------------------
 * Since v4.1 adding a new bus under a different domain will cause devices on the bus to not be fully populated. See the
 * comment in "vpci_add_device()" here for details & a simple fix.
 *
 * KNOWN BUGS
 * ----------
 * Under Linux v3.10 once bus is added it cannot be fully removed (or we didn't find the correct way). When you do the
 * initial add and scan everything works correctly. You can later even remove that bus BUT the kernel leaves some sysfs
 * stuff behind in /sys/devices (while /sys/bus/pci/devices are cleaned up). This means that if you try to re-register
 * the same bus it explode with sysfs duplication errors.
 * As of now we have no idea how to go around that.
 *
 *
 * References:
 *  - https://stackoverflow.com/a/31465293 (how PCI subsystem works)
 *  - https://docs.oracle.com/cd/E19120-01/open.solaris/819-3196/hwovr-25/index.html (PCI working theory)
 *  - https://elixir.bootlin.com/linux/v3.10.108/source/include/uapi/linux/pci_regs.h (Linux PCI registers)
 *  - https://elixir.bootlin.com/linux/v3.10.108/source/drivers/pci/probe.c (PCI scanning code; very readable)
 *  - https://blog.csdn.net/moon146/article/details/18988849 (scanning process)
 *  - https://wiki.osdev.org/PCI (details regarding flags & commands)
 */
#include "virtual_pci.h"
#include "../common.h"
#include "../config/runtime_config.h" //MAX_VPCI_BUSES
#include <linux/pci.h>
#include <linux/pci_regs.h> //PCI device header constants
#include <linux/pci_ids.h> //Constants for vendors, classes, and other
#include <linux/list.h> //list_for_each
#include <linux/device.h> //device_del

#define PCIBUS_VIRTUAL_DOMAIN 0x0001 //normal PC buses are (always?) on domain 0, this is just a next one
#define PCI_DEVICE_NOT_FOUND_VID_DID 0xFFFFFFFF //A special case to detect non-existing devices (per PCI spec)

//Model of a default config for a device
const struct pci_dev_descriptor pci_dev_conf_default_normal_dev = {
    .vid = 0xDEAD, //set me!
    .dev = 0xBEEF, //set me!

    .command = 0x0000,
    .status  = 0x0000,

    .rev_id = PCI_DSC_REV_NONE,
    .prog_if = PCI_DSC_PROGIF_NONE,
    .subclass = U16_CLASS_TO_U8_CLASS(PCI_CLASS_NOT_DEFINED), //set me!
    .class = U16_CLASS_TO_U8_CLASS(PCI_CLASS_NOT_DEFINED), //set me!

    .cache_line_size = 0x00,
    .latency_timer = 0x00,
    .header_type = PCI_HEADER_TYPE_NORMAL,
    .bist = PCI_DSC_BIST_NONE, //Built-In Self Test

    .bar0 = PCI_DSC_NULL_BAR,
    .bar1 = PCI_DSC_NULL_BAR,
    .bar2 = PCI_DSC_NULL_BAR,
    .bar3 = PCI_DSC_NULL_BAR,
    .bar4 = PCI_DSC_NULL_BAR,
    .bar5 = PCI_DSC_NULL_BAR,

    .cardbus_cis = 0x00000000,

    .subsys_vid = 0x0000, //you probably want to set this
    .subsys_id = 0x0000, //you probably want to set this

    .exp_rom_base_addr = 0x00000000,

    .cap_ptr = PCI_DSC_NULL_CAP,
    .reserved_34_8_15 = PCI_DSC_RSV8,
    .reserved_34_16_31 = PCI_DSC_RSV16,

    .reserved_38h = 0x00000000,

    .interrupt_line = PCI_DSC_NO_INT_LINE,
    .interrupt_pin = PCI_DSC_NO_INT_PIN,
    .min_gnt = PCI_DSC_ZERO_BURST,
    .max_lat = PCI_DSC_INF_LATENCY,
};

struct virtual_device {
    unsigned char *bus_no; //same as bus->number, used when bus is not initialized yet (e.g. during scanning)
    unsigned char dev_no;
    unsigned char fn_no;
    struct pci_bus* bus;
    void *descriptor;
};
static unsigned int free_bus_idx = 0; //Used to find next free bus and for indexing other arrays
static struct pci_bus *buses[MAX_VPCI_BUSES] = { NULL }; //All virtual buses

static unsigned int free_dev_idx = 0; //Used to find next free bus and for indexing other arrays
static struct virtual_device *devices[MAX_VPCI_DEVS] = { NULL }; //All virtual devices

//Macros to easily iterate over lists above
#define for_each_bus_idx() for (int i = 0, last_bus_idx = free_bus_idx-1; i <= last_bus_idx; i++)
#define for_each_dev_idx() for (int i = 0, last_dev_idx = free_dev_idx-1; i <= last_dev_idx; i++)

/**
 * Prints pci_dev_descriptor or pci_pci_bridge_descriptor
 */
void print_pci_descriptor(void *test_dev)
{
    pr_loc_dbg("Printing PCI descriptor @ %p", test_dev);
    printk("\n31***********0***ADDR*******************\n");
    u8 *ptr = (u8 *)test_dev;
    for (int row = 3; row < 64; row += 4) {
        for (int byte = 0; byte > -4; byte--) {
            printk("%02x ", *(ptr + row + byte));
            if (byte == -1) printk("  ");
        }

        printk(" | 0x%02X\n", row - 3);
    }
    //The following format will be useful when/if CAPs are implemented
//    printk("\n--------------Device Private--------------\n");
//    printk("00000000 00000000 00000000 00000000  | xxx\n");
//    printk("******************************************\n");
}

/**
 * @param bus The bus (may be under first scan so only its number may be present in virtual_device)
 * @param devfn Device AND its function; it's a 0-256 number allowing for 32 devices with 8 functions each
 * @param where Offset in the device structure to read
 * @param size How many BYTES (not bits) to read
 * @param val Pointer to save read bytes
 * @return PCIBIOS_*
 */
static int pci_read_cfg(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
    //devfn is a combination of device number on bus and function number (Bus/Device/Function addressing)
    //Each device which exists MUST implement function 0. So every 8th value of devfn we have a new device.
    unsigned char vdev_no = DEVFN_COMBO_TO_DEV_NO(devfn);
    unsigned char vdev_fn = DEVFN_COMBO_TO_DEV_FN(devfn);

    void *pci_descriptor = NULL;

    //Very noisy!
    //pr_loc_dbg("Read SYN wh=0x%d sz=%d B / %d for vDEV @ bus=%02x dev=%02x fn=%02x", where, size, size * 8,
    //           bus->number, vdev_no, vdev_fn);
    for_each_dev_idx() {
        //Very noisy!
        //pr_loc_dbg("Checking vDEV @ bus=%02x dev=%02x fn=%02x", *devices[i]->bus_no, devices[i]->dev_no,
        //           devices[i]->fn_no);

        //We cannot use devices[i]->bus->number during scan as the bus may just being created and no ->bus is available
        if(*devices[i]->bus_no == bus->number && devices[i]->dev_no == vdev_no && devices[i]->fn_no == vdev_fn) {
            //Very noisy!
            //pr_loc_dbg("Found matching vDEV @ bus=%02x dev=%02x fn=%02x => vidx=%d", bus->number, vdev_no, vdev_fn, i);
            pci_descriptor = devices[i]->descriptor;
            break;
        }
    };

    if (!pci_descriptor) {
        if (where == PCI_VENDOR_ID || where == PCI_DEVICE_ID)
            *val = PCI_DEVICE_NOT_FOUND_VID_DID;

        return PCIBIOS_DEVICE_NOT_FOUND;
    }

    //Very noisy!
    //pr_loc_dbg("Read ACK wh=0x%d sz=%d B / %d for vDEV @ bus=%02x dev=%02x fn=%02x", where, size, size * 8, bus->number,
    //           vdev_no, vdev_fn);
    memcpy(val, (u8 *)pci_descriptor + where, size);

    return PCIBIOS_SUCCESSFUL;
}

static int pci_write_cfg(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
    return PCIBIOS_SET_FAILED;
}

//Definition of callbacks the PCI subsystem uses to query the root bus
static struct pci_ops pci_shim_ops = {
    .read = pci_read_cfg,
    .write = pci_write_cfg
};

//x86-specific sysdata which is expected to be present while running on x86 (if it's not you will get a KP)
static struct pci_sysdata x86_sysdata = {
    .domain = PCIBUS_VIRTUAL_DOMAIN,
#ifdef CONFIG_ACPI
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
    .companion = NULL, //See https://github.com/torvalds/linux/commit/7b1998116bbb2f3e5dd6cb9a8ee6db479b0b50a9
#else
    .acpi = NULL,
#endif //LINUX_VERSION_CODE
#endif //CONFIG_ACPI
    .iommu = NULL
};

//_NO  => number according to the PCI spec
//_IDX => index in arrays (internal to this emulation layer only)
#define BUS_NO_VALID(x) ((x) >= 0 && (x) <= 0xFF) //Check if a given bus# is valid according to the PCI spec
#define DEV_NO_VALID(x) ((x) >= 0 && (x) <= 32) //Check if a given dev# is valid according to the PCI spec
#define FN_NO_VALID(x) ((x) >= 0 && (x) <= 7) //Check if a given function# is valid according to the PCI spec
#define VBUS_IDX_VALID(x) ((x) >= 0 && (x) < MAX_VPCI_BUSES-1) //Check if virtual bus INDEX is valid for this emulator
#define VBUS_IDX_USED(x) ((x) >= 0 && (x) < free_bus_idx) //Check if a given bus index is used now in the emulator
#define VDEV_IDX_VALID(x) ((x) >= 0 && (x) < MAX_VPCI_DEVS-1) //Check if virtual device INDEX is valid for this emulator
#define VDEV_IDX_USED(x) ((x) >= 0 && (x) < free_dev_idx) //Check if a given bus index is used now in the emulator

static inline int validate_bdf(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no)
{
    if (unlikely(!BUS_NO_VALID(bus_no))) {
        pr_loc_err("%02x is not a valid PCI bus number", bus_no);
        return -EINVAL;
    }

    if (unlikely(!DEV_NO_VALID(dev_no))) {
        pr_loc_err("%02x is not a valid PCI device number", dev_no);
        return -EINVAL;
    }

    if (unlikely(!FN_NO_VALID(fn_no))) {
        pr_loc_err("%02x is not a valid PCI device function number", fn_no);
        return -EINVAL;
    }

    //if the free device index is not valid it means we're out of free IDs for devices
    if (unlikely(!VDEV_IDX_VALID(free_dev_idx))) {
        pr_loc_bug("No more device indexes are available (max devs: %d)", MAX_VPCI_DEVS);
        return -ENOMEM;
    }

    //If the device has the same B/D/F address it is a duplicate
    for_each_dev_idx() {
        if (
                likely(*devices[i]->bus_no == bus_no) &&
                unlikely(devices[i]->dev_no == dev_no && devices[i]->fn_no)
                ) {
            pr_loc_err("Device bus=%02x dev=%02x fn=%02x already exists in vidx=%d", bus_no, dev_no, fn_no, i);
            return -EEXIST;
        }
    };

    return 0;
}

static inline struct pci_bus *get_vbus_by_number(unsigned char bus_no)
{
    for_each_bus_idx() { //Determine whether we need to rescan existing bus after adding a device OR scan a new root bus
        if (buses[i]->number == bus_no) {
            pr_loc_dbg("Found existing bus_no=%d @ bidx=%d", bus_no, i);
            return buses[i];
            break;
        }
    };

    return NULL;
}

const struct virtual_device *
vpci_add_device(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no, void *descriptor)
{
    pr_loc_dbg("Attempting to add vPCI device [printed below] @ bus=%02x dev=%02x fn=%02x", bus_no, dev_no, fn_no);
    print_pci_descriptor(descriptor);

    int error = validate_bdf(bus_no, dev_no, fn_no);
    if (error != 0)
        return ERR_PTR(error);

    struct pci_bus *bus = get_vbus_by_number(bus_no);

    //At this point we know the device can be added either to a new or existing bus so we have to populate their struct
    struct virtual_device *device = kmalloc(sizeof(struct virtual_device), GFP_KERNEL);
    device->dev_no = dev_no;
    device->fn_no = fn_no;
    device->descriptor = descriptor;

    if (bus) { //We have an existing bus to use
        device->bus_no = &bus->number;
        devices[free_dev_idx++] = device;

        pci_rescan_bus(bus); //this cannot fail - it simply return max device num

        pr_loc_err("Added device with existing bus @ bus=%02x dev=%02x fn=%02x", *device->bus_no, device->dev_no,
                   device->fn_no);
        return device;
    }

    //No existing bus - check if we can add a new one
    //if the free bus index is not valid it means we're out of free IDs for buses
    if (unlikely(!VBUS_IDX_VALID(free_bus_idx))) {
        pr_loc_bug("No more bus indexes are available (max buses: %d)", MAX_VPCI_BUSES);
        return ERR_PTR(-ENOMEM);
    }

    //Since we don't have a bus so we need to add the device with a mock dev_no and trigger scanning (which actually
    // creates the bus). While it sounds counter-intuitive it is how the PCI subsystem works.
    unsigned char tmp_bus_no = bus_no; //It will be valid for the time of initial scan
    device->bus_no = &tmp_bus_no;
    devices[free_dev_idx++] = device;

    bus = pci_scan_bus(*device->bus_no, &pci_shim_ops, &x86_sysdata);
    if (!bus) {
        pr_loc_err("pci_scan_bus failed - cannot add new bus");
        devices[free_dev_idx--] = NULL; //Reverse adding & ensure idx is still free
        kfree(device); //Free memory for the device itself
        return ERR_PTR(-EIO);
    }

    device->bus_no = &bus->number; //Replace temp bus number pointer with the actual bus struct pointer
    device->bus = bus;
    buses[free_bus_idx++] = bus;

    /*
     * There was a commit in v4.1 which made "subtle" change aimed to "cleanup control flow" by moving
     * pci_bus_add_devices(bus) from drivers/pci/probe.c:pci_scan_bus() to a higher order
     * arch/x86/pci/common.c:pcibios_scan_root().
     * However this means that adding a bus with a domain different than 0 as used on x86 with BIOS/ACPI causes some
     * resources to not be created (e.g. /sys/bus/pci/devices/..../config) which in turn breaks a ton of tools (lspci
     * included). This is because pci_bus_add_devices() calls pci_create_sysfs_dev_files().
     * It's important to mention that this is broken only for new buses - pci_rescan_bus() calls pci_bus_add_devices().
     *
     * Don't even fucking ask how long we looked for that...
     *
     * See https://github.com/torvalds/linux/commit/8e795840e4d89df3d594e736989212ee8a4a1fca#
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
    pr_loc_dbg("Linux >=v4.1 quirk: calling pci_bus_add_devices(bus) manually");
    pci_bus_add_devices(bus);
#endif

    pr_loc_err("Added device with new bus @ bus=%02x dev=%02x fn=%02x", *device->bus_no, device->dev_no, device->fn_no);
    return device;
}

int vpci_remove_all_devices_and_buses(void)
{
    //The order here is crucial - kernel WILL NOT remove references to devices on bus removal (and cause a KP)
    //Doing this in any other order will cause an instant KP when PCI subsys tries to access its structures (e.g. lspci)
    //However, this is still leaving dangling things in /sys/devices which cannot be removed (kernel bug?)

    struct pci_dev *pci_dev, *pci_dev_n;
    for_each_bus_idx() {
        list_for_each_entry_safe(pci_dev, pci_dev_n, &buses[i]->devices, bus_list) {
            pr_loc_dbg("Detaching vDEV dev=%02x fn=%02x from bus=%02x [add=%d]", DEVFN_COMBO_TO_DEV_NO(pci_dev->devfn),
                       DEVFN_COMBO_TO_DEV_FN(pci_dev->devfn), buses[i]->number, pci_dev->is_added);
            pci_stop_and_remove_bus_device(pci_dev);
        }
    }

    for_each_dev_idx() {
        pr_loc_dbg("Removing PCI vDEV @ didx %d", i);
        kfree(devices[i]);
        devices[i] = NULL;
    };
    free_dev_idx = 0;

    for_each_bus_idx() {
        pr_loc_dbg("Removing child PCI vBUS @ bidx %d", i);
        pci_rescan_bus(buses[i]);
        pci_remove_bus(buses[i]);
        buses[i] = NULL;
    }
    free_bus_idx = 0;

    pr_loc_inf("All vPCI devices and buses removed");

    return -EIO; //This is hardcoded to return an error as there's a known bug (see "KNOWN BUGS" in the file header)
}