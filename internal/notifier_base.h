#ifndef REDPILL_NOTIFIER_BASE_H
#define REDPILL_NOTIFIER_BASE_H

#define notifier_reg_in() pr_loc_dbg("Registering %s notifier", NOTIFIER_NAME);
#define notifier_reg_ok() pr_loc_inf("Successfully registered %s notifier", NOTIFIER_NAME);
#define notifier_ureg_in() pr_loc_dbg("Unregistering %s notifier", NOTIFIER_NAME);
#define notifier_ureg_ok() pr_loc_inf("Successfully unregistered %s notifier", NOTIFIER_NAME);
#define notifier_sub(nb) \
    pr_loc_dbg("%pF (priority=%d) subscribed to %s events", (nb)->notifier_call, (nb)->priority, NOTIFIER_NAME);
#define notifier_unsub(nb) \
    pr_loc_dbg("%pF (priority=%d) unsubscribed from %s events", (nb)->notifier_call, (nb)->priority, NOTIFIER_NAME);

#endif //REDPILL_NOTIFIER_BASE_H
