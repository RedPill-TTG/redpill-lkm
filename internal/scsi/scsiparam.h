/**
 * This file contains a list of cherry-picked constants useful while dealing with SCSI subsystem
 */
#ifndef REDPILL_SCSIPARAM_H
#define REDPILL_SCSIPARAM_H

#include <linux/version.h> //KERNEL_VERSION_CODE, KERNEL_VERSION()
#include <scsi/scsi.h> //SERVICE_ACTION_IN or SERVICE_ACTION_IN_16

#define SCSI_RC16_LEN 32 //originally defined in drivers/scsi/sd.c as RC16_LEN
#define SCSI_CMD_TIMEOUT (30 * HZ) //originally defined in drivers/scsi/sd.h as SD_TIMEOUT
#define SCSI_CMD_MAX_RETRIES 5 //normal drives shouldn't fail the command even once
#define SCSI_CAP_MAX_RETRIES 3
#define SCSI_BUF_SIZE 512 //originally defined in drivers/scsi/sd.h as SD_BUF_SIZE

//Old kernels used ambiguous constant: https://github.com/torvalds/linux/commit/eb846d9f147455e4e5e1863bfb5e31974bb69b7c
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
#define SCSI_SERVICE_ACTION_IN_16 SERVICE_ACTION_IN
#else
#define SCSI_SERVICE_ACTION_IN_16 SERVICE_ACTION_IN_16
#endif


#endif //REDPILL_SCSIPARAM_H
