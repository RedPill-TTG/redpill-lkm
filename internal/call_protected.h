#ifndef REDPILLLKM_CALL_PROTECTED_H
#define REDPILLLKM_CALL_PROTECTED_H

#include <linux/types.h> //bool

bool kernel_has_symbol(const char *name);

// ******************************************* //

#include <linux/seq_file.h>
int _cmdline_proc_show(struct seq_file *m, void *v);

#include <linux/notifier.h>
void _usb_register_notify(struct notifier_block *nb);
void _usb_unregister_notify(struct notifier_block *nb);
#endif //REDPILLLKM_CALL_PROTECTED_H
