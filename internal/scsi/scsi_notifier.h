#ifndef REDPILL_SCSI_NOTIFIER_H
#define REDPILL_SCSI_NOTIFIER_H

#include <linux/notifier.h> //All other parts including scsi_notifier.h cannot really not use linux/notifier.h

typedef enum {
    SCSI_EVT_DEV_PROBING, //device is being probed; it can be modified or outright ignored
    SCSI_EVT_DEV_PROBED_OK, //device is probed and ready
    SCSI_EVT_DEV_PROBED_ERR, //device was probed but it failed
} scsi_event;

/**
 * Callback signature: void (*f)(struct notifier_block *self, unsigned long state, void *data), where:
 *      unsigned long state => scsi_event event
 *      void *data => struct scsi_device *sdp
 *
 * Currently these methods are DELIBERATELY limited to SCSI TYPE_DISK scope. If you need other SCSI devices watching
 * add another set of methods (subscribe scsi_device_events() and such, do NOT extend the scope of these methods as
 * other parts of the code rely on pre-filtered events as in most cases listening for ALL devices is a lot of noise).
 *
 * @return
 */
int subscribe_scsi_disk_events(struct notifier_block *nb);
int unsubscribe_scsi_disk_events(struct notifier_block *nb);

int register_scsi_notifier(void);
int unregister_scsi_notifier(void);

#endif //REDPILL_SCSI_NOTIFIER_H
