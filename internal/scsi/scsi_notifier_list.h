/**
 * This file exists solely as a workaround for GCC bug #275674 - static structures are misdirected as dynamic
 *
 * Linux contains many clever idioms. One of them is a complex initialization of heads for notifier chains
 * (include/linux/notifier.h). They do contain an embedded cast to a struct. GCC <5 detects that as a dynamic allocation
 * and refuses to initialize it statically. This breaks all the macros for notifier (e.g. BLOCKING_NOTIFIER_INIT). Old
 * kernels (i.e. <3.18) cannot be compiled with GCC >4.9 so... we cannot use a newer GCC but we cannot use older due to
 * a bug. One of the solutions would be to convert the whole code of this module to GNU89 but this is painful to use.
 *
 * Such structures are working in GNU89 mode as well as when defined as a heap variable in a function. However, GCC is
 * smart enough to release the memory from within a function (so we cannot just wrap it in a function and return a ptr).
 * Due to the complex nature of the struct we didn't want to hardcode it here as they change between kernel version.
 * As a workaround we created a separate compilation unit containing just the struct and compile it in GNU89 mode, while
 * rest of the project stays at GNU99.
 *
 * Resources
 *  - https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63567 (bug report)
 *  - https://unix.stackexchange.com/a/275674 (kernel v3.18 restriction)
 *  - https://stackoverflow.com/a/49119902 (linking files compiled with different language standard in GCC)
 *  - https://www.kernel.org/doc/Documentation/kbuild/makefiles.txt (compilation option per file in Kbuild; sect. 3.7)
 */
#ifndef REDPILL_SCSI_NOTIFIER_LIST_H
#define REDPILL_SCSI_NOTIFIER_LIST_H

extern struct blocking_notifier_head rp_scsi_notify_list;

#endif //REDPILL_SCSI_NOTIFIER_LIST_H
