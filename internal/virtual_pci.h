#ifndef REDPILL_VIRTUAL_PCI_H
#define REDPILL_VIRTUAL_PCI_H

#include <linux/types.h>

/*
 * The following macros are useful for converting PCI_CLASS_* constants to individual values in structs
 *  31 .................. 0
 *  [class][sub][prog][rev]  (each is 8 bit number, often times class-sub-prog is represented as on 24-bit int)
 *
 *  For more information see header comment in the corresponding .c file.
 */
#define U24_CLASS_TO_U8_CLASS(x) (((x) >> 16) & 0xFF)
#define U24_CLASS_TO_U8_SUBCLASS(x) (((x) >> 8) & 0xFF)
#define U24_CLASS_TO_U8_PROGIF(x) ((x) & 0xFF)
#define U16_CLASS_TO_U8_CLASS(x) (((x) >> 8) & 0xFF)
#define U16_CLASS_TO_U8_SUBCLASS(x) ((x) & 0xFF)

//Some helpful constants on top of what's in linux/pci_ids.h & linux/pci_regs.h
#define PCI_DSC_NO_INT_LINE 0xFF
#define PCI_DSC_NO_INT_PIN 0x00
#define PCI_DSC_PROGIF_NONE 0x00
#define PCI_DSC_REV_NONE 0x00
#define PCI_DSC_NULL_BAR 0x00000000
#define PCI_DSC_NULL_CAP 0x00
#define PCI_DSC_RSV8 0x00
#define PCI_DSC_RSV16 0x0000
#define PCI_DSC_INF_LATENCY 0xFF //i.e. accepts any latency in access
#define PCI_DSC_ZERO_BURST 0xFF //i.e. doesn't need any length of burst
#define PCI_DSC_BIST_NONE 0x00

//See https://en.wikipedia.org/wiki/PCI_configuration_space#/media/File:Pci-config-space.svg
//This struct MUST be packed to allow for easy reading, see https://kernelnewbies.org/DataAlignment
struct pci_dev_descriptor {
    u16 vid;               //Vendor ID
    u16 dev;               //Device ID

    u16 command;           //see PCI_COMMAND_*. Simply do "dev.command |= PCI_COMMAND_xxx" to set a flag.
    u16 status;            //see PCI_STATUS_*. Simply do "dev.status |= PCI_STATUS_xxx" to set a flag.

    u8 rev_id;
    u8 prog_if;            // ]
    u8 subclass;           // ]-> prof_if, subclass, and class are normally represented as 24-bit class code
    u8 class;              // ]

    u8 cache_line_size;
    u8 latency_timer;
    u8 header_type;        //see PCI_HEADER_TYPE_*
    u8 bist;               //see PCI_BIST_*

    u32 bar0;
    u32 bar1;
    u32 bar2;
    u32 bar3;
    u32 bar4;
    u32 bar5;

    u32 cardbus_cis;

    u16 subsys_vid;
    u16 subsys_id;

    u32 exp_rom_base_addr; //see PCI_ROM_* (esp PCI_ROM_ADDRESS_MASK)

    u8 cap_ptr;
    u8 reserved_34_8_15;   //should be 0x00
    u16 reserved_34_16_31; //should be 0x00

    u32 reserved_38h;

    u8 interrupt_line;
    u8 interrupt_pin;
    u8 min_gnt;
    u8 max_lat;
} __packed;
extern const struct pci_dev_descriptor pci_dev_conf_default_normal_dev; //See details in the .c file

//Support for bridges wasn't tested
struct pci_pci_bridge_descriptor {
    u16 vid;               //Vendor ID
    u16 dev;               //Device ID

    u16 command;           //see PCI_COMMAND_*. Simply do "dev.command |= PCI_COMMAND_xxx" to set a flag.
    u16 status;            //see PCI_STATUS_*. Simply do "dev.status |= PCI_STATUS_xxx" to set a flag.

    u8 rev_id;
    u8 prog_if;            // ]
    u8 subclass;           // ]-> prof_if, subclass, and class are normally represented as 24-bit class code
    u8 class;              // ]

    u8 cache_line_size;
    u8 latency_timer;
    u8 header_type;        //see PCI_HEADER_TYPE_*
    u8 bist;               //see PCI_BIST_*

    u32 bar0;
    u32 bar1;

    u8 pri_bus_no;
    u8 sec_bus_no;
    u8 subord_bus_no;
    u8 sec_lat_timer;

    u8 io_base;
    u8 io_limit;
    u16 sec_status;

    u16 mem_base;
    u16 mem_limit;

    u16 prefetch_mem_base;
    u16 prefetch_mem_limit;

    u32 prefetch_base_up32b;
    u32 prefetch_limit_up32b;

    u16 io_base_up16b;
    u16 io_limit_up16b;

    u8 cap_ptr;
    u8 reserved_34_8_15;   //should be 0x00
    u16 reserved_34_16_31; //should be 0x00

    u32 exp_rom_base_addr; //see PCI_ROM_* (esp PCI_ROM_ADDRESS_MASK)

    u8 interrupt_line;
    u8 interrupt_pin;
    u16 bridge_ctrl;
} __packed;

//This is currently not implemented
struct pci_dev_capability {
    u8 cap_id; //see PCI_CAP_ID_*, set to 0x00 to denote null-capability
    u8 cap_next; //offset where next capability exists, set to 0x00 to denote null-capability
    u8 cap_data[];
} __packed;

/**
 * Adds a single new device (along with the bus if needed)
 *
 * If you don't want to create the descriptor from scratch you can use "const struct pci_dev_conf_default_normal_dev"
 * while setting some missing params (see .c file header for details).
 * Note: you CAN reuse the same descriptor under multiple BDFs (bus_no/dev_no/fn_no)
 *
 * @param bus_no (0x00 - 0xFF)
 * @param dev_no (0x00 - 0x20)
 * @param descriptor Pointer to pci_dev_descriptor or pci_pci_bridge_descriptor
 * @return virtual_device ptr or error pointer (ERR_PTR(-E))
 */
const struct virtual_device *
vpci_add_single_device(unsigned char bus_no, unsigned char dev_no, struct pci_dev_descriptor *descriptor);

/**
 * See vpci_add_single_device() for details
 */
const struct virtual_device *
vpci_add_single_bridge(unsigned char bus_no, unsigned char dev_no, struct pci_pci_bridge_descriptor *descriptor);


/*
 * Adds a new multifunction device (along with the bus if needed)
 *
 * Warning about multifunctional devices
 *  - this function has a slight limitation due to how Linux scans devices. You HAVE TO add fn_no=0 entry as the LAST
 *    one when calling it multiple times. Kernel scans devices only once for changes and if it finds fn=0 and it's the
 *    only one (i.e. you added fn=0 first) adding more functions will not populate them (as kernel will never re-scan
 *    the device).
 *  - As per PCI spec Linux doesn't allow devices to have fn>0 if they don't have corresponding fn=0 entry
 *
 * @param bus_no (0x00 - 0xFF)
 * @param dev_no (0x00 - 0x20)
 * @param fn_no (0x00 - 0x07)
 * @param descriptor Pointer to pci_dev_descriptor or pci_pci_bridge_descriptor
 * @return virtual_device ptr or error pointer (ERR_PTR(-E))
 */
const struct virtual_device *
vpci_add_multifunction_device(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no,
                              struct pci_dev_descriptor *descriptor);

/**
 * See vpci_add_multifunction_device() for details
 */
const struct virtual_device *
vpci_add_multifunction_bridge(unsigned char bus_no, unsigned char dev_no, unsigned char fn_no,
                              struct pci_pci_bridge_descriptor *descriptor);

/**
 * Removes all previously added devices and buses
 *
 * Known bug: while you can remove things and they will be gone from the system you CANNOT re-add the same under the
 * same BFD coordinates. This will cause the kernel to complain about duplicated internal sysfs entries. It's most
 * likely an old kernel bug (we tried everything... it doesn't work).
 *
 * @param bus_no
 * @param dev_no
 * @param fn_no
 * @param descriptor
 * @return
 */
int vpci_remove_all_devices_and_buses(void);

#endif //REDPILL_VIRTUAL_PCI_H
