#include "scsi_notifier_list.h"
#include <linux/notifier.h>

BLOCKING_NOTIFIER_HEAD(rp_scsi_notify_list);
