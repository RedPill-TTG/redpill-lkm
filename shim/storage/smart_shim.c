/**
 * Emulates SMART capabilities on drives without SMART support
 *
 * WHY?!
 * When running under a hypervisor some emulated drives will lack SMART support, which is required for some tools to
 * operate properly. For example QEmu (e.g. in Proxmox) emulate some basic SMART support (see qemu/hw/ide/core.c), which
 * is enough. However, surprisingly popular VMWare products lack any SMART support whatsoever. This is the main reason
 * for this shim to exist.
 *
 *
 * HOW IT WORKS?
 * The SMART subsystem is rather simple yet contains a large amount of legacy. To understand what's going on here you
 * should first read the micron.com docs and then fill-in the details from the official ATA/ATAPI-6 spec (see
 * references). Overall the SMART has a few main parts:
 *   - live/snapshot readings (that's the table with values & thresholds you see in smartctl)
 *   - capabilities (static values which specify what a given drive can do, e.g. whether SMART is supported)
 *   - stored logs
 *   - API to execute tests
 *
 * All SMART actions are executed by the userspace, and thus delivered via ioctl call to a device. When the ioctl is
 * executed a shared buffer is passed which contains a header + space for saving response data. There are two
 * different ioctl commands used to execute SMART requests with several subcommands:
 *   - HDIO_DRIVE_CMD (ioctl, see handle_hdio_drive_cmd_ioctl() & Documentation/ioctl/hdio.txt)
 *     - ATA_CMD_ID_ATA (read capabilities of the drive, see handle_ata_cmd_identify())
 *       # we only hook it to indicate that SMART is supported & enabled even if the ATA disk don't REALLY support it
 *       # if the drive supports SMART this hook is a noop
 *       # for non ATA-complaint disks (e.g. VirtIO SCSI) we generate a full fake IDENTIFY data (populate_ata_id())
 *     - ATA_CMD_SMART (read data from SMART subsystem, see handle_ata_cmd_smart())
 *       # we hook it to emulate SMART data
 *       # if the drive supports SMART this hook is a noop
 *       - ATA_SMART_READ_VALUES (read all live values from the drive, see populate_ata_smart_values())
 *       - ATA_SMART_READ_THRESHOLDS (read thresholds from live data of the drive, see populate_ata_smart_thresholds())
 *       - ATA_SMART_ENABLE (ask drive to enable SMART, it shouldn't really be delivered but we respond with "OK")
 *       - WIN_FT_SMART_READ_LOG_SECTOR (use WIN_SMART interface to read stored logs, see populate_win_smart_log())
 *       - WIN_FT_SMART_IMMEDIATE_OFFLINE (use WIN_SMART iface to run SMART test, see populate_win_smart_exec_test())
 *
 *  - HDIO_DRIVE_TASK (ioctl, see handle_hdio_drive_task_ioctl() & Documentation/ioctl/hdio.txt)
 *    - WIN_CMD_SMART (use WIN_SMART to read data from SMART subsystem, see handle_ata_task_smart())
 *      # this command is used by smartctl (and probably others) to read a general OK/FAIL status of a drive
 *      # we respond with "OK" to all commands below
 *      - WIN_FT_SMART_STATUS
 *      - WIN_FT_SMART_AUTOSAVE
 *      - WIN_FT_SMART_AUTO_OFFLINE
 *
 * Note: Most of the commands are using the standard ATA/ATAPI interface, few are using (legacy?) WIN_SMART interface.
 *       While WIN_SMART can theoretically be used to read values etc no tool from this century will do that (they will
 *       use the ATA/ATAPI interface). This shim emulates WIN_SMART only when needed.
 *
 *
 * LIMITATIONS
 *   - Values are always static and the same for all drives
 *   - Power-on hours & other counters (e.g. start-stop count) are static
 *     - Ideally values should be calculated as hours from some date to ensure they increase
 *     - Start-stop counter (and others) can be derived from power-on hours using linear regression
 *
 *
 * SEQUENCE OF ACTIONS FOR IOCTL REPLACEMENT
 * This submodule has a rather unintuitive initialization sequence (it's multistage). It works in the following order:
 *   1. Checks if "sd" driver is loaded
 *      - if not loaded it verifies if it exists in the kernel and overrides sd_ioctl() [see 2.]
 *      - it SHOULD wait for the driver instead but due to current notifier limitations we can't do that
 *   2. Temporarily installs trampoline in sd_ioctl() [drivers/scsi/sd.c] to sd_ioctl_canary()
 *   3. Awaits any IOCTL from userspace
 *      - it simply waits until sd_ioctl_canary() is called
 *      - sd_ioctl_canary's only role is to install a fast and permanent shim of sd_ioctl() by replacing the ioctl
 *        routing in SCSI driver (see details in the comment for sd_ioctl_canary()) to point to sd_ioctl_smart_shim()
 *      - sd_ioctl() trampoline is removed
 *      - after installation it triggers sd_ioctl_smart_shim() to handle that IOCTL which canary captured
 *   4. sd_ioctl_smart_shim() is triggered for every ioctl to a /dev/sdX device coming from the userspace
 *      - it filters commands which are SMART-related (or at least what smartmontools uses as nobody uses anything else)
 *      - all non-SMART commands are forwarded as-is
 *      - SMART commands are forwarded to the drive if the drive supports SMART, if not a sensible values are faked
 *
 * References
 *  - https://www.micron.com/-/media/client/global/documents/products/technical-note/solid-state-storage/tnfd10_p400e_smart_firmware_0142.pdf
 *  - https://hddguru.com/documentation/2006.01.27-ATA-ATAPI-6/ (the official ATA/ATAPI-6 specs)
 *  - https://www.kernel.org/doc/Documentation/ioctl/hdio.txt (HDIO_* ioctls summary from Linux)
 *  - https://github.com/qemu/qemu/blob/266469947161aa10b1d36843580d369d5aa38589/hw/ide/core.c#L1826 (qemu SMART)
 */
#include "smart_shim.h"
#include "../shim_base.h"
#include "../../common.h"
#include "../../internal/intercept_driver_register.h" //waiting for "sd" driver to load
#include "../../internal/helper/memory_helper.h" //set_mem_addr_ro(), set_mem_addr_rw()
#include "../../internal/helper/symbol_helper.h" //kernel_has_symbol()
#include "../../internal/scsi/hdparam.h" //a ton of ATA constants
#include "../../internal/scsi/scsi_toolbox.h" //checking for "sd" driver load state
#include "../../internal/override_symbol.h" //installing sd_ioctl_canary()
#include <linux/fs.h> //struct block_device
#include <linux/genhd.h> //struct gendisk
#include <linux/blkdev.h> //struct block_device_operations
#include <linux/spinlock.h> //spinlock_t, spin_*
#include <linux/ata.h> //ATA_*

#define SHIM_NAME "SMART emulator"

//address of original and unmodified sd_ioctl(); populated by the canary and after the canary trampoline is removed
static int (*sd_ioctl_org) (struct block_device *, fmode_t, unsigned, unsigned long) = NULL;
struct block_device_operations *sd_fops = NULL; //ptr to drivers/scsi/sd.c:sd_fops [to restore sd_ioctl on removal]
static struct override_symbol_inst* sd_ioctl_canary_ovs = NULL; //sd_ioctl() override for canary
static spinlock_t sd_ioctl_canary_lock;

/********************************************* Fake SMART data definition *********************************************/
//see "Table 4: SMART Attribute Summary" in micron.com document for a nice summary
//These values below were taken from a random WD drive and slightly modified. While there isn't a definitive list of
// "what must be in SMART" (as ATAPI spec says that everything is vendor-specific) there are some "usually available"
// params the FLAG value is split into low (LSB) and high (MSB). Really only the first 6 bits (so in LSB) are defined:
//  Bit 0 - Warranty purposes
//  Bit 1 - Offline collected
//  Bit 2 - Performance degradation indicator
//  Bit 3 - Error rate dependent
//  Bit 4 - Event counter (should probably NOT be used with bit 3)
//  Bit 5 - Self-preservation
//Values here are a cross-section of HDD & SSD capable ones. While reading these we don't always have easy access to
// the data used to determine whether the drive is an SSD or an HDD so we "play it cool" by only giving values which are
// valid for both.
static const int fake_smart[][ATA_SMART_RECORD_LEN] = {
    /*  #, lFLAG, hFLAG, VAL,  WRST,  RAW_DATA,                RAW_ATTR_SPC, THRESH,   NAME */
    {   1, 0x2d,  0x00,  0xc8, 0xc8,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x06 }, /* Raw_Read_Error_Rate */
    {   2, 0x04,  0x00,  0x80, 0x75,  0x80, 0x00, 0x00, 0x00,  0x00, 0x00,   0x40 }, /* Throughput_Performance */
    {   3, 0x27,  0x00,  0xb1, 0xB0,  0x4f, 0x12, 0x00, 0x00,  0x00, 0x00,   0x06 }, /* Spin_Up_Time */
    {   4, 0x32,  0x00,  0x64, 0x64,  0x45, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Start_Stop_Count */
    {   5, 0x33,  0x00,  0xc8, 0xC8,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x8c }, /* Reallocated_Sector_Ct */
    //Seek_Error_Rate(7) is invalid for SSDs
    //deliberately not providing Seek_Time_Performance (8) to prevent fs from "recalibrating" to this
    {   9, 0x32,  0x00,  0x06, 0x00,  0xad, 0x32, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Power_On_Hours */
    //Spin_Retry_Count(10) and Calibration_Retry_Count(11) are invalid for SSDs
    {  12, 0x32,  0x00,  0x64, 0x64,  0x2a, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Start_Stop_Count */
    {  13, 0x2e,  0x00,  0xc8, 0xc8,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Read_Soft_Error_Rate */
    //14-174: very vendor-specific / esoteric
    //175-182: SSD only
    { 183, 0x33,  0x00,  0xc8, 0xc8,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x8c }, /* Runtime_Bad_Block */
    { 184, 0x33,  0x00,  0xC8, 0xc8,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x8c }, /* End-to-End_Error */
    //185-186: very vendor-specific / esoteric
    { 187, 0x3a,  0x00,  0x3e, 0x3e,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x05 }, /* Reported_Uncorrect */
    { 188, 0x32,  0x00,  0x64, 0x64,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Command_Timeout */
    //High_Fly_Writes(189) is invalid for SSDs
    { 190, 0x22,  0x00,  0x3e, 0x3e,  0x1B, 0x00, 0x1E, 0x1B,  0x00, 0x00,   0x00 }, /* Airflow_Temperature_Cel */
    //Sense_Error_Rate(191) is invalid for SSDs
    { 192, 0x32,  0x00,  0x64, 0x64,  0x28, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Power-Off_Retract_Count */
    //Unknown_SSD_Attribute(193) is invalid for SSDs
    { 194, 0x22,  0x00,  0x76, 0x62,  0x1d, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Temperature_Celsius */
    { 195, 0x32,  0x00,  0x80, 0x80,  0x39, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Hardware_ECC_Recovered */
    { 196, 0x32,  0x00,  0x80, 0x80,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Reallocated_Event_Count */
    { 197, 0x32,  0x00,  0x80, 0x80,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Current_Pending_Sector */
    { 198, 0x30,  0x00,  0x64, 0xfe,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* Offline_Uncorrectable */
    { 199, 0x32,  0x00,  0xC8, 0xC8,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,   0x00 }, /* UDMA_CRC_Error_Count */
    //rest of the attributes are esoteric or invalid for SSDs
};

//SMART components versions (some of them CANNOT be changed)
#define SMART_SNAP_VERSION 0x01 //version for the live data snapshot; vendor-specific
#define WIN_SMART_SUM_LOG_VERSION 0x01 //WIN_SMART summary log version; ALWAYS 1 as per ATAPI/6 sec. 8.55.6.8.2.1
#define WIN_SMART_COMP_LOG_VERSION 0x01 //WIN_SMART comprehensive log version; ALWAYS 1 as per ATAPI/6 sec. 8.55.6.8.3.1
#define WIN_SMART_TEST_LOG_VERSION 0x01 //WIN_SMART self-test log version; ALWAYS 1 as per ATAPI/6 sec. 8.55.6.8.4.1


/********************************************* ATA/IOCTL helper functions *********************************************/
/**
 * Calculates a standard per-sector ATA checksum
 * 
 * ATA/ATAPI-6 standard contains the same checksum references in many places. It's always saved in the last byte of a
 * sector (index 511). It is described e.g. in "Table 5: SMART Attribute Entry Format". It's defined as "Two's 
 * complement checksum of preceding 511B[ytes]". Wikipedia has a great article about that as well.
 * 
 * @param buff A single-sector sized buffer to compute & save checksum to
 */
static void ata_calc_sector_checksum(u8 *buff)
{
    for (int i = 0; i < (ATA_SECT_SIZE-1); i++) {
        buff[(ATA_SECT_SIZE-1)] += buff[i];
    }
    
    buff[(ATA_SECT_SIZE-1)] = 256 - buff[(ATA_SECT_SIZE-1)];
}

/**
 * Calculates a standard per-worded structure ATA checksum
 *
 * In principal it's almost the same thing as ata_calc_sector_checksum() but with some constant added to be 16 bits.
 * See "8.16.64 Word 255: Integrity word". Checksum is always saved in word 255.
 *
 * @param word_buff A 255-word (each 16 bits) sized buffer to compute & save checksum to
 */
static void ata_calc_integrity_word(u16 *word_buff)
{
    u8 *byte_buff = (u8 *)word_buff;

    for (int i = 0; i < (ATA_SECT_SIZE-2); i++) {
        byte_buff[(ATA_SECT_SIZE-2)] += byte_buff[i];
    }

    byte_buff[(ATA_SECT_SIZE-2)] = 256 - byte_buff[(ATA_SECT_SIZE-2)];
    byte_buff[(ATA_SECT_SIZE-1)] = 0xa5;
}

/**
 * ATA/ATAPI uses "strings" which are LE arranged 8 bit characters into 16 bit words padded with spaces to full length
 *
 * Example of 10 character ATA field:
 *  =normal=> "TEST12"
 *  =ATA====> "ETTS21    "
 *
 * @param dst buffer to copy the string to
 * @param src standard NULL-byte terminated text
 * @param length ATA field length; must be even
 */
static void set_ata_string(u8 *dst, const char *src, u8 length)
{
    if (unlikely(length % 2 != 0)) {
        pr_loc_bug("Length must be even but got %d", length);
        --length;
    }

    memset(dst, 0x20, length); //fields in ATA/ATAPI are space-padded and not terminated by \0
    for (u8 i = 0; i < length; i += 2)
    {
        if (src[i] == '\0')
            break;

        dst[i + 1] = src[i];
        dst[i] = src[i + 1];
    }
}

/**
 * Duplicates a user-supplied ioctl() buffer into kernel space to safely read data from it
 *
 * This pointer returned here is... peculiar. First 4 bytes are a header (as defined by UAPI "struct hd_drive_cmd_hdr"
 * in hdreg.h). Remaining 512 bytes are 16 bits words.
 *
 * @param sectors How many sectors to copy
 * @param src User buffer to copy from
 *
 * @return Pointer to a new kernel-space buffer or ERR_PTR; you need to free/put-it-back using put_ioctl_buffer
 */
static unsigned char* get_ioctl_buffer_kcopy(u8 sectors, const __user void *src)
{
    unsigned char *kbuf;

    kmalloc_or_exit_ptr(kbuf, ata_ioctl_buf_size(sectors));
    if(unlikely(copy_from_user(kbuf, src, ata_ioctl_buf_size(sectors)) != 0)) {
        pr_loc_err("Failed to copy ATA user buffer from ptr=%p to kspace=%p", src, kbuf);
        kfree(kbuf);
        return ERR_PTR(-EFAULT);
    }

    return kbuf;
}

/**
 * Releases buffer obtained from get_ioctl_buffer_*()
 */
static __always_inline void put_ioctl_buffer(unsigned char *buffer)
{
    kfree(buffer);
}

/*************************************** ATAPI/WIN command interface handling *****************************************/
static int populate_ata_id(const u8 *req_header, void __user *buff_ptr)
{
    pr_loc_dbg("Generating completely fake ATA IDENTITY");

    unsigned char *kbuf;
    kzalloc_or_exit_int(kbuf, HDIO_DRIVE_CMD_HDR_OFFSET + sizeof(struct rp_hd_driveid));
    struct rp_hd_driveid *did = (void *)(kbuf + HDIO_DRIVE_CMD_HDR_OFFSET); //did=drive ID

    //First write response header
    kbuf[HDIO_DRIVE_CMD_RET_STATUS] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_ERROR] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_SEC_CNT] = ATA_CMD_ID_ATA_SECTORS;

    did->config = 0x0000; //15th bit = ATA device, rest is reserved/obsolete
    set_ata_string(did->serial_no, "VH1132", 20);
    set_ata_string(did->fw_rev, "1.13.2", 8);
    set_ata_string(did->model, "Virtual HDD", 40);
    did->reserved50 = (1 << 14); //"shall be set to one"
    did->major_rev_num = 0xffff;
    did->minor_rev_num = 0xffff;
    did->command_set_1 = (1 << 3 | 1 << 0); //PM, SMART supported
    did->command_set_2 = (1 << 14); //"shall be set to one"
    did->cfsse = (1 << 14 | 1 << 1 | 1 << 0); //14: "shall be set to one" ; smart self-test supported ; smart error-log
    did->cfs_enable_1 = (1 << 3 | 1 << 0); //PM, SMART
    did->cfs_enable_2 = (1 << 14); //"shall be set to one"
    did->csf_default = (1 << 14 | 1 << 1 | 1 << 0); //"shall be one" ; SMART self-test, SMART error-test
    did->hw_config = (1 << 14 | 1 << 0); //both "shall be one"
    did->lba_capacity = 0xffffffff; //maybe we can get away with not reading capacity?

    ata_calc_integrity_word((void *)did);

    if (unlikely(copy_to_user(buff_ptr, kbuf, HDIO_DRIVE_CMD_HDR_OFFSET + sizeof(struct rp_hd_driveid)) != 0)) {
        pr_loc_err("Failed to copy fake ATA IDENTIFY packet to user ptr=%p", (void *)buff_ptr);
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return 0;
}

/**
 * Handles on-the-fly modification of data related to ATA IDENTIFY DEVICE command
 *
 * See "8.16 IDENTIFY DEVICE" section in the ATA/ATAPI-6 manual.
 *
 * @param org_ioctl_exec_result exit code of the original ioctl() call which reached the drive (done by
 *                              handle_hdio_drive_cmd_ioctl()). This command shouldn't normally fail for any drive.
 * @param req_header ioctl() header sent along the request, will be HDIO_DRIVE_CMD_HDR_OFFSET bytes long
 * @param buff_ptr userspace pointer to a buffer passed to the ioctl() call; it will be read and possibly altered
 *
 * @return definitive exit code for the ioctl(); in practice 0 when succedded [regardless of the modifications made] or
 *         the same error code as org_ioctl_exec_result passed
 */
static int handle_ata_cmd_identify(int org_ioctl_exec_result, const u8 *req_header, void __user *buff_ptr)
{
    //ATA IDENTIFY should not fail - it may mean a problem with a disk or the "disk" is a adapter (e.g. IDE>SATA) with
    // no disk connected, or if executed against a USB flash drive... or it's an VirtIO SCSI disk read as ATA
    if (unlikely(org_ioctl_exec_result != 0)) {
        pr_loc_dbg("sd_ioctl(HDIO_DRIVE_CMD ; ATA_CMD_ID_ATA) failed with error=%d, attempting to emulate something",
                   org_ioctl_exec_result);
        return populate_ata_id(req_header, buff_ptr);
    }

    //sanity check if requested ATA IDENTIFY sector count is really what we're planning to copy
    if (unlikely(req_header[HDIO_DRIVE_CMD_HDR_SEC_CNT]) != ATA_CMD_ID_ATA_SECTORS) {
        pr_loc_err("Expected %d bytes (%d sectors) DATA for ATA IDENTIFY DEVICE, got %d",
                   ATA_CMD_ID_ATA_SECTORS, ata_ioctl_buf_size(ATA_CMD_ID_ATA_SECTORS),
                   req_header[HDIO_DRIVE_CMD_HDR_SEC_CNT]);
        return -EIO;
    }

    //if the identity succeeded we need to check if SMART is supported & enabled. For that we need to clone
    // normally user buffer from ioctl (reading it directly is risky)
    unsigned char *kbuf = get_ioctl_buffer_kcopy(ATA_CMD_ID_ATA_SECTORS, buff_ptr);
    if (unlikely(IS_ERR(kbuf)))
        return PTR_ERR(kbuf); //it will already log error in kcopy

    u16 *ata_identity = (u16 *)(kbuf + HDIO_DRIVE_CMD_HDR_OFFSET);
    if (ata_is_smart_supported(ata_identity) && ata_is_smart_enabled(ata_identity)) {
        pr_loc_dbg("ATA_CMD_ID_ATA confirmed SMART support - noop");
        put_ioctl_buffer(kbuf); //we no longer need the buffer as we're not touching it, we've only read it
        return 0; //SMART supported, pass identity as-is
    }

    //if SMART is not supported we modify the response, retaining the original response header but changing
    // SMART flags & recalculating checksum
    pr_loc_dbg("ATA_CMD_ID_ATA confirmed *no* SMART support - pretending it's there");
    ata_set_smart_supported(ata_identity);
    ata_set_smart_enabled(ata_identity);
    ata_calc_integrity_word(ata_identity);

    if (unlikely(copy_to_user(buff_ptr, kbuf, ata_ioctl_buf_size(ATA_CMD_ID_ATA_SECTORS)) != 0)) {
        pr_loc_err("Failed to copy ATA IDENTIFY packet to user ptr=%p", (void *)buff_ptr);
        put_ioctl_buffer(kbuf);
        return -EFAULT;
    }

    put_ioctl_buffer(kbuf);
    return 0;
}

/**
 * Populates user ioctl() buffer with fake SMART snapshot values
 *
 * This function is responsible for the generation of data which you see in a usual tabular format as a result of
 * "smartctl -A" command. The data is formated from the "fake_smart" constant array present on the top of this file.
 *
 * @param req_header ioctl() header sent along the request, will be HDIO_DRIVE_CMD_HDR_OFFSET bytes long
 * @param buff_ptr userspace pointer to a buffer passed to the ioctl() call; it will be overwritten with data
 *
 * @return 0 on success, -EIO on unexpected call, -ENOMEM when memory reservation fails, or -EFAULT when data fails to
 *         copy to user buffer
 */
static int populate_ata_smart_values(const u8 *req_header, void __user *buff_ptr)
{
    pr_loc_dbg("Generating fake SMART values");

    //sanity check if requested SMART READ VALUES sector count is really what we're planning to copy
    if (unlikely(req_header[HDIO_DRIVE_CMD_HDR_SEC_CNT]) != ATA_SMART_READ_VALUES_SECTORS) {
        pr_loc_err("Expected %d bytes (%d sectors) DATA for ATA SMART READ VALUES, got %d",
                   ATA_SMART_READ_VALUES_SECTORS, ata_ioctl_buf_size(ATA_SMART_READ_VALUES_SECTORS),
                   req_header[HDIO_DRIVE_CMD_HDR_SEC_CNT]);
        return -EIO;
    }

    int i, j;
    unsigned char *kbuf;
    kzalloc_or_exit_int(kbuf, ata_ioctl_buf_size(ATA_SMART_READ_VALUES_SECTORS));
    u8 *smart_values = (u8 *)(kbuf + HDIO_DRIVE_CMD_HDR_OFFSET);

    //First write response header
    kbuf[HDIO_DRIVE_CMD_RET_STATUS] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_ERROR] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_SEC_CNT] = ATA_SMART_READ_VALUES_SECTORS;

    //See "Vendor-Specific Data Bytes 0–361" and "Table 5: SMART Attribute Entry Format" in micron.com
    // document for specification of these numbers and calculations
    //For full structure see "Table 59 − Device SMART data structure" in ATA/ATAPI-6 PDF
    smart_values[0] = SMART_SNAP_VERSION;

    //copy ALL attribute bytes as we were asked for everything (including thresholds)
    for (i = 0; i < ARRAY_SIZE(fake_smart); i++) {
        for (j = 0; j < 11; j++) {
            smart_values[2 + (ATA_SMART_RECORD_LEN * i) + j] = fake_smart[i][j];
        }
    }

    //specify that we never ran any SMART tests and we're not running any
    smart_values[362] = 0x82; //Sec. 8.55.5.8.1, Table 60 in ATA/ATAPI-6 PDF (self test ran on boot & succeeded)
    smart_values[363] = 0x00; //Sec. 8.55.5.8.2, Table 61 in ATA/ATAPI-6 PDF
    smart_values[364] = 0x45; //LSB of "Total time to complete Offline data collection" (seconds)
    smart_values[365] = 0x00; //MSB of "Total time to complete Offline data collection" (seconds)
    smart_values[367] = (1 << 3 | 1 << 4); //bitfield, see sec. 8.55.5.8.4 in ATA/ATAPI-6 PDF
    smart_values[368] = (1 << 0 | 1 << 1); //bitfield, see sec. 8.55.5.8.5 in ATA/ATAPI-6 PDF
    smart_values[369] = 0x01; //vendor-specific, rel. to sec. 8.55.5.8.5 in ATA/ATAPI-6 PDF
    smart_values[370] = 0x01; //bitfield, current only 1st bit used for error logging (Table 59)
    smart_values[372] = 0x05; //short self-test polling time (minutes), see Table 59
    smart_values[373] = 0x4B; //long self-test polling time (minutes), see Table 59

    ata_calc_sector_checksum(smart_values);

    if (copy_to_user(buff_ptr, kbuf, ata_ioctl_buf_size(ATA_SMART_READ_VALUES_SECTORS)) != 0) {
        pr_loc_err("Failed to copy SMART VALUES packet to user ptr=%p", buff_ptr);
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return 0;
}

/**
 * Populates user ioctl() buffer with a subset of fake SMART snapshot values, containing only thresholds
 *
 * @param req_header ioctl() header sent along the request, will be HDIO_DRIVE_CMD_HDR_OFFSET bytes long
 * @param buff_ptr userspace pointer to a buffer passed to the ioctl() call; it will be overwritten with data
 *
 * @return 0 on success, -EIO on unexpected call, -ENOMEM when memory reservation fails, or -EFAULT when data fails to
 *         copy to user buffer
 */
static int populate_ata_smart_thresholds(const u8 *req_header, void __user *buff_ptr)
{
    pr_loc_dbg("Generating fake SMART thresholds");

    //sanity check if requested SMART READ THRESHOLDS sector count is really what we're planning to copy
    if (unlikely(req_header[HDIO_DRIVE_CMD_HDR_SEC_CNT]) != ATA_SMART_READ_THRESHOLDS_SECTORS) {
        pr_loc_err("Expected %d bytes (%d sectors) DATA for ATA SMART READ THRESHOLDS, got %d",
                   ATA_SMART_READ_THRESHOLDS_SECTORS, ata_ioctl_buf_size(ATA_SMART_READ_THRESHOLDS_SECTORS),
                   req_header[HDIO_DRIVE_CMD_HDR_SEC_CNT]);
        return -EIO;
    }

    int i;
    unsigned char *kbuf;
    kzalloc_or_exit_int(kbuf, ata_ioctl_buf_size(ATA_SMART_READ_THRESHOLDS_SECTORS));
    u8 *smart_thresholds = (u8 *)(kbuf + HDIO_DRIVE_CMD_HDR_OFFSET);

    //First write response header
    kbuf[HDIO_DRIVE_CMD_RET_STATUS] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_ERROR] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_SEC_CNT] = ATA_SMART_READ_THRESHOLDS_SECTORS;

    //See "Vendor-Specific Data Bytes 0–361" and "Table 5: SMART Attribute Entry Format" in micron.com
    // document for specification of these numbers and calculations
    //For full structure see "Table 59 − Device SMART data structure" in ATA/ATAPI-6 PDF
    smart_thresholds[0] = SMART_SNAP_VERSION;

    //copy a subset of attribute bytes as we were asked for thresholds only
    for (i = 0; i < ARRAY_SIZE(fake_smart); i++) {
        smart_thresholds[2 + (ATA_SMART_RECORD_LEN * i) + 0] = fake_smart[i][0]; //entry id
        smart_thresholds[2 + (ATA_SMART_RECORD_LEN * i) + 1] = fake_smart[i][11]; //threshold value
    }

    ata_calc_sector_checksum(smart_thresholds);

    if (copy_to_user(buff_ptr, kbuf, ata_ioctl_buf_size(ATA_SMART_READ_THRESHOLDS_SECTORS)) != 0) {
        pr_loc_err("Failed to copy SMART THRESHOLDS packet to user ptr=%p", buff_ptr);
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return 0;
}

/**
 * Read stored SMART log using WIN_SMART interface
 *
 * This is a special command from the "WIN_SMART" subset to read the SMART offline log. To understand it see the
 * "8.55.6 SMART READ LOG" in ATA/ATAPI-6 specs. It describes it as"Command code B0h with the content of the Features
 * register equal to D5h" (B0h = 0xb0 = ATA_CMD_SMART; D5h = 0x05 = WIN_FT_SMART_READ_LOG_SECTOR).
 * There are multiple types of logs. This function implements all non-vendor ones.
 *
 * @param req_header ioctl() header sent along the request, will be HDIO_DRIVE_CMD_HDR_OFFSET bytes long
 * @param buff_ptr userspace pointer to a buffer passed to the ioctl() call; it will be overwritten with data
 *
 * @return 0 on success, -EIO on unexpected call, -ENOMEM when memory reservation fails, or -EFAULT when data fails to
 *         copy to user buffer
 */
static int populate_win_smart_log(const u8 *req_header, void __user *buff_ptr)
{
    pr_loc_dbg("Generating fake WIN_SMART log=%d entries", req_header[HDIO_DRIVE_CMD_HDR_SEC_NUM]);

    //sanity check if requested SMART READ LOG sector count is really what we're planning to copy
    if (unlikely(req_header[HDIO_DRIVE_CMD_HDR_SEC_CNT]) != ATA_WIN_SMART_READ_LOG_SECTORS) {
        pr_loc_err("Expected %d bytes (%d sectors) DATA for ATA WIN_SMART READ LOG, got %d",
                   ATA_WIN_SMART_READ_LOG_SECTORS, ata_ioctl_buf_size(ATA_WIN_SMART_READ_LOG_SECTORS),
                   req_header[HDIO_DRIVE_CMD_HDR_SEC_CNT]);
        return -EIO;
    }

    unsigned char *kbuf;
    kzalloc_or_exit_int(kbuf, ata_ioctl_buf_size(ATA_WIN_SMART_READ_LOG_SECTORS));
    u8 *smart_log = (u8 *)(kbuf + HDIO_DRIVE_CMD_HDR_OFFSET);

    //First write response header
    kbuf[HDIO_DRIVE_CMD_RET_STATUS] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_ERROR] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_SEC_CNT] = ATA_WIN_SMART_READ_LOG_SECTORS;

    //See "Table 62 − Log address definition" in ATAPI/6 docs
    switch (req_header[HDIO_DRIVE_CMD_HDR_SEC_NUM]) {
        case 0x01: //summary SMART error log (see sect. 8.55.6.8.2 Summary error log sector)
            smart_log[0] = WIN_SMART_SUM_LOG_VERSION;
            smart_log[1] = 0x00; //no error entries = index is 0
            smart_log[452] = 0x00; //no errors = count byte 1 is zero
            smart_log[453] = 0x00; //no errors = count byte 2 is zero
            break;

        case 0x02: //comprehensive SMART error log
            smart_log[0] = WIN_SMART_COMP_LOG_VERSION;
            smart_log[1] = 0x00; //no error entries = index is 0
            smart_log[452] = 0x00; //no errors = count byte 1 is zero
            smart_log[453] = 0x00; //no errors = count byte 2 is zero
            break;

        case 0x06: //SMART self-test log
            smart_log[0] = WIN_SMART_TEST_LOG_VERSION;
            smart_log[1] = 0x00; //revision (2nd byte, also defined by 8.55.6.8.4.1)
            smart_log[508] = 0x00; //no errors
            break;

        default: //other ones are reserved/vendor/etc
            pr_loc_err("Unexpected WIN_FT_SMART_READ_LOG_SECTOR with log_addr=%d",
                       req_header[HDIO_DRIVE_CMD_HDR_SEC_NUM]);
            kfree(kbuf);
            return -EIO;
    }

    ata_calc_sector_checksum(smart_log);

    if (copy_to_user(buff_ptr, kbuf, ata_ioctl_buf_size(ATA_WIN_SMART_READ_LOG_SECTORS)) != 0) {
        pr_loc_err("Failed to copy WIN_SMART LOG packet to user ptr=%p", buff_ptr);
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return 0;
}

/**
 * Dispatches an drive-internal SMART test using WIN_SMART interface
 *
 * @param req_header ioctl() header sent along the request, will be HDIO_DRIVE_CMD_HDR_OFFSET bytes long
 * @param buff_ptr userspace pointer to a buffer passed to the ioctl() call; it will be overwritten with data
 *
 * @return 0 on success, -EIO on unexpected call, -ENOMEM when memory reservation fails, or -EFAULT when data fails to
 *         copy to user buffer
 */
static int populate_win_smart_exec_test(const u8 *req_header, void __user *buff_ptr)
{
    pr_loc_dbg("Generating fake WIN_SMART offline test type=%d", req_header[HDIO_DRIVE_CMD_HDR_SEC_NUM]);

    unsigned char *kbuf;
    kzalloc_or_exit_int(kbuf, HDIO_DRIVE_CMD_HDR_OFFSET);

    //First write response header
    kbuf[HDIO_DRIVE_CMD_RET_STATUS] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_ERROR] = 0x00;
    kbuf[HDIO_DRIVE_CMD_RET_SEC_CNT] = ATA_WIN_SMART_EXEC_TEST;

    //See "Table 58 − SMART EXECUTE OFF-LINE IMMEDIATE LBA Low register values" in ATAPI/6 docs
    switch (req_header[HDIO_DRIVE_CMD_HDR_SEC_NUM]) {
        case 0x00: //off-line in off-line mode
        case 0x01: //short in off-line mode
        case 0x02: //long in off-line mode
        case 0x7f: //abort previous test
        case 0x81: //short in captive mode
        case 0x82: //long in captive mode
            break; //we only need to populate the response header

        default: //other ones are reserved/vendor/etc
            pr_loc_err("Unexpected WIN_FT_SMART_READ_LOG_SECTOR with log_addr=%d",
                       req_header[HDIO_DRIVE_CMD_HDR_SEC_NUM]);
            kfree(kbuf);
            return -EIO;
    }

    if (copy_to_user(buff_ptr, kbuf, HDIO_DRIVE_CMD_HDR_OFFSET) != 0) {
        pr_loc_err("Failed to copy WIN_SMART TEST header to user ptr=%p", buff_ptr);
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return 0;
}

/**
 * Emulates various SMART data requested via ATA_CMD_SMART method
 *
 * SMART responses here assume that original ioctl() failed (since otherwise it would be no point to emulate them). If
 * you call this function on a drive with functioning SMART it will be ignored and fake smart will be generated for it.
 *
 * @param req_header ioctl() header sent along the request, will be HDIO_DRIVE_CMD_HDR_OFFSET bytes long
 * @param buff_ptr userspace pointer to a buffer passed to the ioctl() call; it will be overwritten with data
 *
 * @return 0 on success, -EIO on unexpected call, -ENOMEM when memory reservation fails, or -EFAULT when data fails to
 *         copy to user buffer
 */
static int __always_inline handle_ata_cmd_smart(const u8 *req_header, void __user *buff_ptr)
{
    pr_loc_dbg("Got SMART *command* - looking for feature=0x%x", req_header[HDIO_DRIVE_CMD_HDR_FEATURE]);

    switch (req_header[HDIO_DRIVE_CMD_HDR_FEATURE]) {
        case ATA_SMART_READ_VALUES: //read all SMART values snapshot
            return populate_ata_smart_values(req_header, buff_ptr);

        case ATA_SMART_READ_THRESHOLDS: //read all SMART thresholds snapshot
            return populate_ata_smart_thresholds(req_header, buff_ptr);

        case ATA_SMART_ENABLE: //enable previously disabled SMART support
            pr_loc_wrn("Attempted ATA_SMART_ENABLE modification!");\
            return 0;

        case WIN_FT_SMART_READ_LOG_SECTOR: //reads offline-stored drive logs
            return populate_win_smart_log(req_header, buff_ptr);

        case WIN_FT_SMART_IMMEDIATE_OFFLINE: //execute a SMART test
            return populate_win_smart_exec_test(req_header, buff_ptr);

        default:
            pr_loc_dbg("Unknown SMART *command* read w/feature=0x%02x", req_header[HDIO_DRIVE_CMD_HDR_FEATURE]);
            return -EIO;
    }
}

/**
 * Shims various commands launched via HDIO_DRIVE_CMD interface, routing them to individual shims
 *
 * This function will shim calls which need to be modified or replaced. All ioctl()s routed here are first executed and
 * then routed to individual shims. If a given ioctl() is not a one which we're interested in altering the result of a
 * real ioctl() call to the driver will be returned unaltered.
 *
 * To fully understand this function make sure to read HDIO_DRIVE_CMD description provided by kernel developers at
 * https://www.kernel.org/doc/Documentation/ioctl/hdio.txt
 */
static int handle_hdio_drive_cmd_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, void __user *buff_ptr)
{
    //Before we execute ioctl we need to save the original header as ioctl will override it (they share buffer)
    u8 req_header[HDIO_DRIVE_CMD_HDR_OFFSET];
    if(unlikely(copy_from_user(req_header, buff_ptr, HDIO_DRIVE_CMD_HDR_OFFSET) != 0)) {
        pr_loc_err("Expected to copy HDIO_DRIVE_CMD header of %d bytes from %p - it failed", HDIO_DRIVE_CMD_HDR_OFFSET,
                   buff_ptr);
        return -EIO;
    }

    int ioctl_out = sd_ioctl_org(bdev, mode, cmd, (unsigned long)buff_ptr);
    switch (req_header[HDIO_DRIVE_CMD_HDR_CMD]) {
        //this command probes the disk for its overall capabilities; it may have nothing to do with SMART reading but
        // we need to modify it to indicate SMART support
        case ATA_CMD_ID_ATA:
            return handle_ata_cmd_identify(ioctl_out, req_header, buff_ptr);

        //this command asks directly for the SMART data of the drive and will fail on drives with no real SMART support
        case ATA_CMD_SMART:
            //if the drive supports SMART it will just return the data as-is, no need to proxy
            return (ioctl_out == 0) ? 0 : handle_ata_cmd_smart(req_header, buff_ptr);

        //We're only interested in a subset of commands - rest are simply redirected back
        default:
            pr_loc_dbg("sd_ioctl(HDIO_DRIVE_CMD ; cmd=0x%02x) => %d - not a hooked cmd, noop",
                       req_header[HDIO_DRIVE_CMD_HDR_CMD], ioctl_out);
            return ioctl_out;
    }
}


/***************************************** ATAPI/WIN task interface handling ******************************************/
/**
 * Emulates various SMART data requested via WIN_CMD_SMART method
 *
 * SMART responses here assume that original ioctl() failed (since otherwise it would be no point to emulate them). If
 * you call this function on a drive with functioning SMART it will be ignored and fake smart will be generated for it.
 *
 * @param req_header ioctl() header sent along the request, will be HDIO_DRIVE_TASK_HDR_OFFSET bytes long
 * @param buff_ptr userspace pointer to a buffer passed to the ioctl() call; it will be overwritten with data. So far
 *                 all known commands don't use any data buffer but only the response header (which, like req_header, is
 *                 HDIO_DRIVE_TASK_HDR_OFFSET bytes long)
 *
 * @return 0 on success, -EIO on unexpected call, -ENOMEM when memory reservation fails, or -EFAULT when data fails to
 *         copy to user buffer
 */
static int __always_inline handle_ata_task_smart(const u8 *req_header, void __user *buff_ptr)
{
    pr_loc_dbg("Got SMART *task* - looking for feature=0x%x", req_header[HDIO_DRIVE_TASK_HDR_FEATURE]);

    switch (req_header[HDIO_DRIVE_TASK_HDR_FEATURE]) {
        case WIN_FT_SMART_STATUS:
        case WIN_FT_SMART_AUTOSAVE:
        case WIN_FT_SMART_AUTO_OFFLINE: { //this must be in brackets, see https://stackoverflow.com/a/18496437
            //Status-checking commands in WIN_SMART officially work in such a way that if the buffer was unchanged it
            // means that SMART is OK. However, two static values of 0x4f/0xc2 are used for OK. Here we're taking the
            // easy (and probably safer) approach by returning the same values.
            //Smartmontools actually verifies if it's the same as req header as well:
            // https://github.com/mirror/smartmontools/blob/b63206bc12efb2ae543040b9008f42c037eb1f04/os_linux.cpp#L405
            u8 rsp_header[HDIO_DRIVE_TASK_HDR_OFFSET] = {
                [HDIO_DRIVE_TASK_RET_STATUS]  = 0x00,
                [HDIO_DRIVE_TASK_RET_ERROR]   = 0x00,
                [HDIO_DRIVE_TASK_RET_SEC_CNT] = req_header[HDIO_DRIVE_TASK_HDR_SEC_CNT],
                [HDIO_DRIVE_TASK_RET_SEC_NUM] = req_header[HDIO_DRIVE_TASK_HDR_SEC_NUM],
                [HDIO_DRIVE_TASK_RET_LCYL]    = req_header[HDIO_DRIVE_TASK_HDR_LCYL],
                [HDIO_DRIVE_TASK_RET_HCYL]    = req_header[HDIO_DRIVE_TASK_HDR_HCYL],
                [HDIO_DRIVE_TASK_RET_SEL]     = req_header[HDIO_DRIVE_TASK_HDR_SEL],
            };

            if (copy_to_user(buff_ptr, rsp_header, HDIO_DRIVE_TASK_HDR_OFFSET) != 0) {
                pr_loc_err("Failed to copy WIN_SMART feature=0x%02x task result packet to user ptr=%p",
                           req_header[HDIO_DRIVE_TASK_HDR_FEATURE], buff_ptr);
                return -EFAULT;
            }
            return 0;
        }
        default:
            pr_loc_dbg("Unknown SMART *task* read w/feature=0x%02x", req_header[HDIO_DRIVE_TASK_HDR_FEATURE]);
            return -EIO;
    }
}

/**
 * Shims various commands launched via HDIO_DRIVE_TASK interface, routing them to individual shims
 *
 * This function will shim calls which need to be modified or replaced. All ioctl()s routed here are first executed and
 * then routed to individual shims. If a given ioctl() is not a one which we're interested in altering the result of a
 * real ioctl() call to the driver will be returned unaltered.
 * This function is used to a very limited subset of commands in the normal circumstances, as see in the smartmontools
 * code: https://github.com/mirror/smartmontools/blob/b63206bc12efb2ae543040b9008f42c037eb1f04/os_linux.cpp#L379
 *
 * To fully understand this function make sure to read HDIO_DRIVE_TASK description provided by kernel developers at
 * https://www.kernel.org/doc/Documentation/ioctl/hdio.txt
 */
static int
handle_hdio_drive_task_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, void __user *buff_ptr)
{
    //Before we execute ioctl we need to save the original header as ioctl will override it (they share buffer)
    u8 req_header[HDIO_DRIVE_TASK_HDR_OFFSET];
    if (unlikely(copy_from_user(req_header, buff_ptr, HDIO_DRIVE_TASK_HDR_OFFSET) != 0)) {
        pr_loc_err("Expected to copy HDIO_DRIVE_TASK_HDR_OFFSET header of %d bytes from %p - it failed",
                   HDIO_DRIVE_TASK_HDR_OFFSET,
                   buff_ptr);
        return -EIO;
    }

    int ioctl_out = sd_ioctl_org(bdev, mode, cmd, (unsigned long)buff_ptr);
    switch (req_header[HDIO_DRIVE_TASK_HDR_CMD]) {
        //this command asks directly for the SMART data. From our understanding it's only used for a small subset of
        // commands. The normal SMART reads/logs/etc are going through HDIO_DRIVE_CMD instead. The only thing [so far]
        // is the SMART self-reported status which goes via HDIO_DRIVE_TASK route
        case WIN_CMD_SMART:
            //if the drive supports SMART it will just return the data as-is, no need to proxy
            return (ioctl_out == 0) ? 0 : handle_ata_task_smart(req_header, buff_ptr);

        //We're only interested in a subset of commands run via task IOCTL - rest are simply redirected back
        default:
            pr_loc_dbg("sd_ioctl(HDIO_DRIVE_TASK ; cmd=0x%02x) => %d - not a hooked cmd, noop",
                       req_header[HDIO_DRIVE_TASK_HDR_CMD], ioctl_out);
            return ioctl_out;
    }
}

/********************************** ioctl() handling re-routing from driver to shim ***********************************/
//These are called from each other so we need to predeclare them
int sd_ioctl_canary_install(void);
int sd_ioctl_canary_uninstall(void);

/**
 * Filters/proxies/emulates device IOCTLs as needed for emulating SMART
 *
 * This shim is installed just before the first IOCTL from the userspace.
 */
static int sd_ioctl_smart_shim(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
    pr_loc_dbg("Handling ioctl(0x%02x) for /dev/%s", cmd, bdev->bd_disk->disk_name);

    if (unlikely(!sd_ioctl_org)) {
        pr_loc_bug("Called %s but no original sd_ioctl() address is known", __FUNCTION__);
        return -EIO;
    }

    switch (cmd) {
        case HDIO_DRIVE_CMD: //"a special drive command" as per hdreg.h
            return handle_hdio_drive_cmd_ioctl(bdev, mode, cmd, (void *)arg);

        case HDIO_DRIVE_TASK: //"execute task and special drive command" as per Documentation/ioctl/hdio.txt
            return handle_hdio_drive_task_ioctl(bdev, mode, cmd, (void *)arg);

        default: //any other ioctls are proxied as-is
            pr_loc_dbg("sd_ioctl(0x%02x) - not a hooked ioctl, noop", cmd);
            return sd_ioctl_org(bdev, mode, cmd, arg);
    }
}

/**
 * Installs a permanent shim into the sd driver ops
 *
 * Keep in mind this function works only if the sd_fops address has been populated. It's normally done by the canary
 * (sd_ioctl_canary()) which is installed by sd_ioctl_canary_install().
 *
 * @return 0 on success; -E on error
 */
int sd_ioctl_smart_shim_install(void)
{
    if (unlikely(!sd_fops)) {
        pr_loc_bug("Gendisk has no ops (yet?)"); //this could happen if IOCTL somehow arrived before sd_probe_async()
        return -EIO;
    }

    //This shouldn't happen - it can only be the case if LKM is unloaded without cleanup (or cleanup is broken)
    if (unlikely(sd_fops->ioctl == sd_ioctl_smart_shim)) {
        pr_loc_bug("sd_ioctl() SMART shim was already installed");
        return 0;
    }

    pr_loc_dbg("Rerouting sd_fops->ioctl<%p>=%pF<%p> to %pF<%p>", &sd_fops->ioctl, sd_fops->ioctl, sd_fops->ioctl,
               sd_ioctl_smart_shim, sd_ioctl_smart_shim);
    sd_ioctl_org = sd_fops->ioctl;

    WITH_MEM_UNLOCKED(
        &sd_fops->ioctl, sizeof(void *),
        sd_fops->ioctl = sd_ioctl_smart_shim;
    );

    return 0;
}

/**
 * Removes the permanent shim present in sd driver ops (if installed)
 *
 * If called with shim not installed it will noop by design.
 *
 * @return 0 on success or noop; -E on error
 */
int sd_ioctl_smart_shim_uninstall(void)
{
    //sd_fops is not saved - nothing to restore
    if (unlikely(!sd_fops))
        return 0;

    if (unlikely(!sd_ioctl_org)) {
        pr_loc_bug("sd_fops->ioctl is not saved - cannot restore");
        return -EIO;
    }

    pr_loc_dbg("Restoring sd_fops->ioctl<%p>=%pF<%p> to %pF<%p>", &sd_fops->ioctl, sd_fops->ioctl, sd_fops->ioctl,
               sd_ioctl_org, sd_ioctl_org);

    WITH_MEM_UNLOCKED(
        &sd_fops->ioctl, sizeof(void *),
        sd_fops->ioctl = sd_ioctl_org;
    );

    sd_ioctl_org = NULL;
    sd_fops = NULL;

    return 0;
}

/**
 * Awaits any SCSI ioctl() to override the ioctl handler in sd_fops.ioctl (drivers/scsi/sd.c)
 *
 * What is essentially does it awaits the first call from the userspace to trigger chain landing in sd_ioctl(), which
 * will be any program attempting to do an ioctl() on /dev/sdX. Then it gets gendisk from block_device passed, which in
 * turn contains a pointer to the same-for-all sd_fops. Having this pointer lets us replace ->ioctl pointer to finally
 * shim the sd_ioctl() pathway. This is done to AVOID using override_symbol:call_overridden_symbol() which isn't really
 * that multi-thread safe and actually pretty slow. The last thing we want is slowing down IOCTLs on a storage system.
 * Using this strange routine above installs a fast IOCTL filter to e.g. respond to SMARTs.
 *
 * While this method isn't really clear other ones are even worse. To find sd_fops we need to get at least one
 * FULLY INITIALIZED gendisk, which is complicated. During sd_probe() the gendisk is only partially built (i.e. it lacks
 * sd_fops...). These other parts are populated in delayed/scheduled sd_probe_async(). So the number of permutations is
 * quite large, as we need to consider the following states of the system:
 *  - no driver loaded
 *  - driver loaded, no drives yet
 *  - driver loaded, 1 drive which is not initialized yet
 *  - driver loaded, >1 drive, one initialized and one not
 *  - driver loaded, 1 drive which is initialized
 *  - ...... you see the point?
 *
 *  This whole thing would be much easier if we could get scsi_disk structure, but it's a private one (and heavily
 *  modified with MY_ABC_HERE).
 */
static int sd_ioctl_canary(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
    int out;
    spin_lock(&sd_ioctl_canary_lock);

    pr_loc_dbg("%s triggered for first ioctl()", __FUNCTION__);
    //if the override of the symbol is no longer present it means that before we've got the lock another canary was
    // processing another IOCTL and did what the canary needed to do - we can safely redirect
    if (unlikely(!sd_ioctl_canary_ovs)) {
        if (unlikely(!sd_fops)) {
            pr_loc_bug("Canary is already processed after obtaining lock BUT fops aren't here - the canary is broken");
            return -EIO; //we don't really know the state
        }

        pr_loc_dbg("Canary is already processed after obtaining lock - proxying to sd_fops->ioctl directly");
        goto proxy_ioctl;
    }

    if (unlikely(!bdev)) {
        pr_loc_bug("NULL block_device passed to %s", __FUNCTION__);
        return -EIO;
    }

    struct gendisk *disk = bdev->bd_disk;
    if (unlikely(!disk)) {
        pr_loc_bug("block_device w/o gendisk found");
        return -EIO;
    }

    sd_fops = (void *)disk->fops; //forcefully remove "const" protection here
    out = sd_ioctl_smart_shim_install();
    if (out != 0) {
        pr_loc_err("Failed to install proper SMART shim");
        return -EIO;
    }

    out = sd_ioctl_canary_uninstall(); //it will log what's wrong
    spin_unlock(&sd_ioctl_canary_lock);
    if (out != 0)
        return -EIO; //we cannot continue as the sd_ioctl() wasn't restored and calling the shim can cause infinite loop

    proxy_ioctl:
    pr_loc_dbg("Canary finished - routing to sd_fops->ioctl = %pF<%p>", sd_fops->ioctl, sd_fops->ioctl);
    return sd_fops->ioctl(bdev, mode, cmd, arg);
}

/**
 * Installs a temporary sd_ioctl() replacement to capture sd_fops used to install permanent shim
 *
 * This function is used to replace sd_ioctl() with sd_ioctl_canary(). The purpose of this is to capture sd_fops on a
 * first ioctl() from userspace. Then this temporary canary is replaced with a real one (the sd_ioctl_smart_shim()).
 * That two-step process is here because we don't have a sane way of obtaining sd_fops (see the function comment for
 * sd_ioctl_canary() to learn about the details) and using override_symbol method makes calls to the original sd_ioctl()
 * slow and potentially prone to multithreading problems.
 *
 * @return 0 on success; -E on error
 */
int sd_ioctl_canary_install(void)
{
    if (unlikely(sd_ioctl_canary_ovs)) {
        pr_loc_bug("Canary already installed");
        return 0; //technically not a full-blown failure
    }

    spin_lock_init(&sd_ioctl_canary_lock);
    sd_ioctl_canary_ovs = override_symbol("sd_ioctl", sd_ioctl_canary);
    if (IS_ERR(sd_ioctl_canary_ovs)) {
        pr_loc_err("Failed to install sd_ioctl() canary");
        int out = PTR_ERR(sd_ioctl_canary_ovs);
        sd_ioctl_canary_ovs = NULL;
        return out;
    }

    return 0;
}

/**
 * Removes the temporary shim installed via sd_ioctl_canary_install() (if installed)
 *
 * If called with shim not installed it will noop by design. It will noop if the shim was never installed or if it was
 * already replaced with the permanent shim.
 *
 * @return 0 on success or noop; -E on error
 */
int sd_ioctl_canary_uninstall(void)
{
    //this isn't an error failure - it will most likely happen when unregistering this shim after first ioctl
    if (unlikely(!sd_ioctl_canary_ovs))
        return 0;

    int out = restore_symbol(sd_ioctl_canary_ovs);
    if (out != 0) {
        pr_loc_err("Failed to uninstall sd_ioctl() canary");
        return out;
    }

    sd_ioctl_canary_ovs = NULL;
    return 0;
}

/****************************************** Standard public API of the shim *******************************************/
int register_disk_smart_shim(void)
{
    shim_reg_in();

    int out;

    out = is_scsi_driver_loaded();
    if (IS_SCSI_DRIVER_ERROR(out)) {
        pr_loc_err("Failed to determine SCSI driver status - error=%d", out);
        return out;
    } else if(out == SCSI_DRV_LOADED || kernel_has_symbol("sd_ioctl")) {
        //driver is loaded, OR it's not loaded, but it's compiled-in
        pr_loc_dbg("SCSI driver exists - installing canary");
        if ((out = sd_ioctl_canary_install()) != 0)
            return out;
    } else { //driver not loaded and doesn't exist (=not compiled in)
        //normally this should call watch_scsi_driver_register() but the current implementation of driver watcher allows
        // for just a single watcher per driver (as it doesn't use standard kernel notifiers, sic!). This is however
        // unlikely case to ever occur
        pr_loc_bug("Cannot register SMART shim - the SCSI driver \"%s\" is not loaded and it doesn't exist",
                   SCSI_DRV_NAME);
        return -ENXIO;
    }

    shim_reg_ok();
    return 0;
}

int unregister_disk_smart_shim(void)
{
    shim_ureg_in();

    int out;
    bool is_error = false;

    out = sd_ioctl_canary_uninstall();
    if (out != 0) {
        pr_loc_err("sd_ioctl_canary_uninstall failed - error=%d", out);
        is_error = true;
    }

    out = sd_ioctl_smart_shim_uninstall();
    if (out != 0) {
        pr_loc_err("sd_ioctl_smart_shim_uninstall failed - error=%d", out);
        is_error = true;
    }

    if (is_error)
        return -EIO;

    shim_ureg_ok();
    return 0;
}