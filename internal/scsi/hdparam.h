/**
 * This file serves a similar role to include/uapi/linux/hdreg.h, combining constants & macros for dealing with ATA
 */
#include <linux/hdreg.h> //HDIO_*
#include <linux/ata.h> //ATA_ID_*, ATA_CMD_*, ATA_SECT_SIZE

#ifndef REDPILL_HDPARAM_H
#define REDPILL_HDPARAM_H

/********************************** Parameters related to HDIO_DRIVE_CMD ioctl call ***********************************/
//the following constants contain size/offsets/indexes of "struct hd_drive_cmd_hdr" fields from user API
// also see HDIO_DRIVE_CMD in https://www.kernel.org/doc/Documentation/ioctl/hdio.txt
#define HDIO_DRIVE_CMD_HDR_OFFSET 4 //same as HDIO_DRIVE_CMD_HDR_SIZE but in sectors
#define HDIO_DRIVE_CMD_HDR_CMD 0 //command
#define HDIO_DRIVE_CMD_HDR_SEC_NUM 1 //sector number
#define HDIO_DRIVE_CMD_HDR_FEATURE 2 //feature
#define HDIO_DRIVE_CMD_HDR_SEC_CNT 3 //sector count

#define HDIO_DRIVE_CMD_RET_STATUS 0
#define HDIO_DRIVE_CMD_RET_ERROR 1
#define HDIO_DRIVE_CMD_RET_SEC_CNT 2 //sector count

//convert sector size of data section of an ATA (sub)command to the full buffer size with header
#define ata_ioctl_buf_size(data_sectors) (u16)(HDIO_DRIVE_CMD_HDR_SIZE+((data_sectors)*(ATA_SECT_SIZE*sizeof(u8))))

//Expected sizes of various commands/subcommands data sections
#define ATA_CMD_ID_ATA_SECTORS 1
#define ATA_SMART_READ_VALUES_SECTORS 1 //subcommand of ATA_CMD_SMART
#define ATA_SMART_READ_THRESHOLDS_SECTORS 1 //subcommand of ATA_CMD_SMART
#define ATA_WIN_SMART_READ_LOG_SECTORS 1 //subcommand of ATA_CMD_SMART
#define ATA_WIN_SMART_EXEC_TEST 1 //subcommand of ATA_CMD_SMART

/********************************** Parameters related to HDIO_DRIVE_TASK ioctl call **********************************/
//another method of calling some ioctls
// see https://github.com/mirror/smartmontools/blob/b63206bc12efb2ae543040b9008f42c037eb1f04/os_linux.cpp#L379
// also see HDIO_DRIVE_TASK in https://www.kernel.org/doc/Documentation/ioctl/hdio.txt
#define HDIO_DRIVE_TASK_HDR_OFFSET 7
#define HDIO_DRIVE_TASK_HDR_CMD 0 //command code
#define HDIO_DRIVE_TASK_HDR_FEATURE 1 //feature
#define HDIO_DRIVE_TASK_HDR_SEC_CNT 2 //sector count
#define HDIO_DRIVE_TASK_HDR_SEC_NUM 3 //sector number
#define HDIO_DRIVE_TASK_HDR_LCYL 4 //CYL LO
#define HDIO_DRIVE_TASK_HDR_HCYL 5 //CYL HI
#define HDIO_DRIVE_TASK_HDR_SEL 6 //device head

#define HDIO_DRIVE_TASK_RET_STATUS 0
#define HDIO_DRIVE_TASK_RET_ERROR 1
#define HDIO_DRIVE_TASK_RET_SEC_CNT 2 //sector count
#define HDIO_DRIVE_TASK_RET_SEC_NUM 3 //sector number
#define HDIO_DRIVE_TASK_RET_LCYL 4 //CYL LO
#define HDIO_DRIVE_TASK_RET_HCYL 5 //CYL HI
#define HDIO_DRIVE_TASK_RET_SEL 6 //device head

//all WIN_FT_* entries are defined under "WIN_SMART sub-commands" in hdreg.h
#define WIN_CMD_SMART 0xb0 //defined in full linux headers as WIN_SMART in hdreg.h
#define WIN_FT_SMART_IMMEDIATE_OFFLINE 0xd4
#define WIN_FT_SMART_READ_LOG_SECTOR 0xd5
#define WIN_FT_SMART_STATUS 0xda
#define WIN_FT_SMART_AUTOSAVE 0xd2 //this is not a typo (AUTOSAVE and AUTO_OFFLINE are spelled differently in ATA spec)
#define WIN_FT_SMART_AUTO_OFFLINE 0xdb

/*************************************** Params related to ATA IDENTIFY command ***************************************/
//Word numbers for the ATA IDENTIFY command response fields & bits in them (described in "struct hd_driveid")
#define ATA_ID_COMMAND_SET_1_SMART 0x01 //first bit of command set #1 contains SMART supported flag
#define ATA_ID_COMMAND_SET_2_VALID 0x4000 //14th bit with should always be 1 when disk supports cmd set 2
#define ATA_ID_CFS_ENABLE_1_SMART 0x01 //first bit of command set #1 contains SMART enable flag
#define ATA_ID_CSF_DEFAULT_VALID 0x4000 //14th bit with should always be 1 when disk supports that

/************************************************* ATA IDENTIFY macros ************************************************/
//These can be used with ATA IDENTIFY data returned by HDIO_GET_IDENTITY or HDIO_DRIVE_CMD=>ATA_CMD_IDENTIFY_DEV with
// when buffer is corrected by HDIO_DRIVE_CMD_HDR_OFFSET (to move over the header data)
#define ata_is_smart_supported(id_data) (((id_data)[ATA_ID_COMMAND_SET_2] & ATA_ID_COMMAND_SET_2_VALID) && \
                                  ((id_data)[ATA_ID_COMMAND_SET_1] & ATA_ID_COMMAND_SET_1_SMART))

#define ata_is_smart_enabled(id_data) (((id_data)[ATA_ID_CSF_DEFAULT] & ATA_ID_CSF_DEFAULT_VALID) && \
                                ((id_data)[ATA_ID_CFS_ENABLE_1] & ATA_ID_CFS_ENABLE_1_SMART))

//set_/unset_ are deliberately asymmetrical here - we don't want to invalidate whole word when disabling SMART
#define ata_set_smart_supported(id_data) \
    do { \
        (id_data)[ATA_ID_COMMAND_SET_2] |= ATA_ID_COMMAND_SET_2_VALID; \
        (id_data)[ATA_ID_COMMAND_SET_1] |= ATA_ID_COMMAND_SET_1_SMART; \
    } while(0)
#define ata_reset_smart_supported(id_data) \
    do { \
        (id_data)[ATA_ID_COMMAND_SET_1] &= ~ATA_ID_COMMAND_SET_1_SMART; \
    } while(0)

#define ata_set_smart_enabled(id_data) \
    do { \
        (id_data)[ATA_ID_CSF_DEFAULT] |= ATA_ID_CSF_DEFAULT_VALID; \
        (id_data)[ATA_ID_CFS_ENABLE_1] |= ATA_ID_CFS_ENABLE_1_SMART; \
    } while(0)
#define ata_reset_smart_enabled(id_data) \
    do { \
        (id_data)[ATA_ID_CFS_ENABLE_1] &= ~ATA_ID_CFS_ENABLE_1_SMART; \
    } while(0)

/*********************************************** Miscellaneous constants **********************************************/
#define ATA_SMART_RECORD_LEN 12 //length of the SMART snapshot data row in bytes, defined

//Modified for kernel use - it's the "hd_driveid" struct from Linux include/uapi/linux/hdreg.h which represents a
// response to HDIO_GET_IDENTITY. See "Table 26 − IDENTIFY DEVICE information" in ATA/ATAPI-6 spec for details.
struct rp_hd_driveid {
    u16 config; /* lots of obsolete bit flags */
    u16 cyls; /* Obsolete, "physical" cyls */
	u16 reserved2;	/* reserved (word 2) */
	u16 heads; /* Obsolete, "physical" heads */
	u16 track_bytes;	/* unformatted bytes per track */
	u16 sector_bytes;	/* unformatted bytes per sector */
	u16 sectors;	/* Obsolete, "physical" sectors per track */
	u16 vendor0;	/* vendor unique */
	u16 vendor1;	/* vendor unique */
	u16 vendor2;	/* Retired vendor unique */
	u8 serial_no[20];	/* 0 = not_specified */
	u16 buf_type;	/* Retired */
	u16 buf_size;	/* Retired, 512 byte increments
					 * 0 = not_specified
					 */
	u16 ecc_bytes;	/* for r/w long cmds; 0 = not_specified */
	u8	fw_rev[8];	/* 0 = not_specified */
	u8	model[40];	/* 0 = not_specified */
	u8	max_multsect;	/* 0=not_implemented */
	u8	vendor3;	/* vendor unique */
	u16 dword_io;	/* 0=not_implemented; 1=implemented */
	u8	vendor4;	/* vendor unique */
	u8	capability;	/* (upper byte of word 49)
					 *  3:	IORDYsup
					 *  2:	IORDYsw
					 *  1:	LBA
					 *  0:	DMA
					 */
	u16 reserved50;	/* reserved (word 50) */
	u8 vendor5;	/* Obsolete, vendor unique */
	u8 tPIO; /* Obsolete, 0=slow, 1=medium, 2=fast */
	u8 vendor6;	/* Obsolete, vendor unique */
	u8 tDMA; /* Obsolete, 0=slow, 1=medium, 2=fast */
	u16 field_valid;	/* (word 53)
					 *  2:	ultra_ok	word  88
					 *  1:	eide_ok		words 64-70
					 *  0:	cur_ok		words 54-58
					 */
	u16 cur_cyls;	/* Obsolete, logical cylinders */
	u16 cur_heads;	/* Obsolete, l heads */
	u16 cur_sectors;	/* Obsolete, l sectors per track */
	u16 cur_capacity0;	/* Obsolete, l total sectors on drive */
	u16 cur_capacity1;	/* Obsolete, (2 words, misaligned int)     */
	u8	multsect;	/* current multiple sector count */
	u8	multsect_valid;	/* when (bit0==1) multsect is ok */
	unsigned int	lba_capacity;	/* Obsolete, total number of sectors */
	u16 dma_1word;	/* Obsolete, single-word dma info */
	u16 dma_mword;	/* multiple-word dma info */
	u16  eide_pio_modes; /* bits 0:mode3 1:mode4 */
	u16  eide_dma_min;	/* min mword dma cycle time (ns) */
	u16  eide_dma_time;	/* recommended mword dma cycle time (ns) */
	u16  eide_pio;       /* min cycle time (ns), no IORDY  */
	u16  eide_pio_iordy; /* min cycle time (ns), with IORDY */
	u16 words69_70[2];	/* reserved words 69-70
					 * future command overlap and queuing
					 */
	u16 words71_74[4];	/* reserved words 71-74
					 * for IDENTIFY PACKET DEVICE command
					 */
	u16  queue_depth;	/* (word 75)
					 * 15:5	reserved
					 *  4:0	Maximum queue depth -1
					 */
	u16  words76_79[4];	/* reserved words 76-79 */
	u16  major_rev_num;	/* (word 80) */
	u16  minor_rev_num;	/* (word 81) */
	u16  command_set_1;	/* (word 82) supported
					 * 15:	Obsolete
					 * 14:	NOP command
					 * 13:	READ_BUFFER
					 * 12:	WRITE_BUFFER
					 * 11:	Obsolete
					 * 10:	Host Protected Area
					 *  9:	DEVICE Reset
					 *  8:	SERVICE Interrupt
					 *  7:	Release Interrupt
					 *  6:	look-ahead
					 *  5:	write cache
					 *  4:	PACKET Command
					 *  3:	Power Management Feature Set
					 *  2:	Removable Feature Set
					 *  1:	Security Feature Set
					 *  0:	SMART Feature Set
					 */
	u16  command_set_2;	/* (word 83)
					 * 15:	Shall be ZERO
					 * 14:	Shall be ONE
					 * 13:	FLUSH CACHE EXT
					 * 12:	FLUSH CACHE
					 * 11:	Device Configuration Overlay
					 * 10:	48-bit Address Feature Set
					 *  9:	Automatic Acoustic Management
					 *  8:	SET MAX security
					 *  7:	reserved 1407DT PARTIES
					 *  6:	SetF sub-command Power-Up
					 *  5:	Power-Up in Standby Feature Set
					 *  4:	Removable Media Notification
					 *  3:	APM Feature Set
					 *  2:	CFA Feature Set
					 *  1:	READ/WRITE DMA QUEUED
					 *  0:	Download MicroCode
					 */
	u16  cfsse; /* (word 84)
					 * cmd set-feature supported extensions
					 * 15:	Shall be ZERO
					 * 14:	Shall be ONE
					 * 13:6	reserved
					 *  5:	General Purpose Logging
					 *  4:	Streaming Feature Set
					 *  3:	Media Card Pass Through
					 *  2:	Media Serial Number Valid
					 *  1:	SMART selt-test supported
					 *  0:	SMART error logging
					 */
	u16  cfs_enable_1;	/* (word 85)
					 * command set-feature enabled
					 * 15:	Obsolete
					 * 14:	NOP command
					 * 13:	READ_BUFFER
					 * 12:	WRITE_BUFFER
					 * 11:	Obsolete
					 * 10:	Host Protected Area
					 *  9:	DEVICE Reset
					 *  8:	SERVICE Interrupt
					 *  7:	Release Interrupt
					 *  6:	look-ahead
					 *  5:	write cache
					 *  4:	PACKET Command
					 *  3:	Power Management Feature Set
					 *  2:	Removable Feature Set
					 *  1:	Security Feature Set
					 *  0:	SMART Feature Set
					 */
	u16  cfs_enable_2;	/* (word 86)
					 * command set-feature enabled
					 * 15:	Shall be ZERO
					 * 14:	Shall be ONE
					 * 13:	FLUSH CACHE EXT
					 * 12:	FLUSH CACHE
					 * 11:	Device Configuration Overlay
					 * 10:	48-bit Address Feature Set
					 *  9:	Automatic Acoustic Management
					 *  8:	SET MAX security
					 *  7:	reserved 1407DT PARTIES
					 *  6:	SetF sub-command Power-Up
					 *  5:	Power-Up in Standby Feature Set
					 *  4:	Removable Media Notification
					 *  3:	APM Feature Set
					 *  2:	CFA Feature Set
					 *  1:	READ/WRITE DMA QUEUED
					 *  0:	Download MicroCode
					 */
	u16  csf_default;	/* (word 87)
					 * command set-feature default
					 * 15:	Shall be ZERO
					 * 14:	Shall be ONE
					 * 13:6	reserved
					 *  5:	General Purpose Logging enabled
					 *  4:	Valid CONFIGURE STREAM executed
					 *  3:	Media Card Pass Through enabled
					 *  2:	Media Serial Number Valid
					 *  1:	SMART selt-test supported
					 *  0:	SMART error logging
					 */
	u16  dma_ultra;	/* (word 88) */
	u16 trseuc; /* time required for security erase */
	u16 trsEuc; /* time required for enhanced erase */
	u16 CurAPMvalues;	/* current APM values */
	u16 mprc; /* master password revision code */
	u16 hw_config;	/* hardware config (word 93)
					 * 15:	Shall be ZERO
					 * 14:	Shall be ONE
					 * 13:
					 * 12:
					 * 11:
					 * 10:
					 *  9:
					 *  8:
					 *  7:
					 *  6:
					 *  5:
					 *  4:
					 *  3:
					 *  2:
					 *  1:
					 *  0:	Shall be ONE
					 */
	u16 acoustic;	/* (word 94)
					 * 15:8	Vendor's recommended value
					 *  7:0	current value
					 */
	u16 msrqs; /* min stream request size */
	u16 sxfert; /* stream transfer time */
	u16 sal; /* stream access latency */
	unsigned int	spg; /* stream performance granularity */
	unsigned long long lba_capacity_2;/* 48-bit total number of sectors */
	u16 words104_125[22];/* reserved words 104-125 */
	u16 last_lun;	/* (word 126) */
	u16 word127;	/* (word 127) Feature Set
					 * Removable Media Notification
					 * 15:2	reserved
					 *  1:0	00 = not supported
					 *	01 = supported
					 *	10 = reserved
					 *	11 = reserved
					 */
	u16 dlf; /* (word 128)
					 * device lock function
					 * 15:9	reserved
					 *  8	security level 1:max 0:high
					 *  7:6	reserved
					 *  5	enhanced erase
					 *  4	expire
					 *  3	frozen
					 *  2	locked
					 *  1	en/disabled
					 *  0	capability
					 */
	u16  csfo; /*  (word 129)
					 * current set features options
					 * 15:4	reserved
					 *  3:	auto reassign
					 *  2:	reverting
					 *  1:	read-look-ahead
					 *  0:	write cache
					 */
	u16 words130_155[26];/* reserved vendor words 130-155 */
	u16 word156;	/* reserved vendor word 156 */
	u16 words157_159[3];/* reserved vendor words 157-159 */
	u16 cfa_power;	/* (word 160) CFA Power Mode
					 * 15 word 160 supported
					 * 14 reserved
					 * 13
					 * 12
					 * 11:0
					 */
	u16 words161_175[15];/* Reserved for CFA */
	u16 words176_205[30];/* Current Media Serial Number */
	u16 words206_254[49];/* reserved words 206-254 */
	u16 integrity_word;	/* (word 255)
					 * 15:8 Checksum
					 *  7:0 Signature
					 */
} __packed;

#endif //REDPILL_HDPARAM_H
