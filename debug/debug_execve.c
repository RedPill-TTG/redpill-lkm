#include "debug_execve.h"
#include "../common.h"
#include <linux/sched.h> //task_struct
#include <asm/uaccess.h> //get_user
#include <linux/compat.h> //compat_uptr_t
#include <linux/binfmts.h> //MAX_ARG_STRINGS

/*
 * Struct copied 1:1 from:
 *
 *  linux/fs/exec.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
struct user_arg_ptr {
#ifdef CONFIG_COMPAT
    bool is_compat;
#endif
    union {
        const char __user *const __user *native;
#ifdef CONFIG_COMPAT
        const compat_uptr_t __user *compat;
#endif
    } ptr;
};

/*
 * Function copied 1:1 from:
 *
 *  linux/fs/exec.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
static const char __user *get_user_arg_ptr(struct user_arg_ptr argv, int nr)
{
    const char __user *native;

#ifdef CONFIG_COMPAT
    if (unlikely(argv.is_compat)) {
        compat_uptr_t compat;

        if (get_user(compat, argv.ptr.compat + nr))
            return ERR_PTR(-EFAULT);

        return compat_ptr(compat);
    }
#endif

    if (get_user(native, argv.ptr.native + nr))
        return ERR_PTR(-EFAULT);

    return native;
}

/*
 * Modified for simplicity from count() in:
 *
 *  linux/fs/exec.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
static int count_args(struct user_arg_ptr argv)
{
    if (argv.ptr.native == NULL)
        return 0;

    int i = 0;

    for (;;) {
        const char __user *p = get_user_arg_ptr(argv, i);

        if (!p)
            break;

        if (IS_ERR(p) || i >= MAX_ARG_STRINGS)
            return -EFAULT;

        ++i;

        if (fatal_signal_pending(current))
            return -ERESTARTNOHAND;
        cond_resched();
    }

    return i;
}

void RPDBG_print_execve_call(const char *filename, const char __user *const __user *argv)
{
    struct task_struct *caller = get_cpu_var(current_task);

    struct user_arg_ptr argv_up = {.ptr.native = argv};
    int argc = count_args(argv_up);

    char *arg_str = kzalloc(MAX_ARG_STRLEN, GFP_KERNEL);
    if (unlikely(!arg_str)) {
        pr_loc_crt("kzalloc failed");
        return;
    }

    char *arg_ptr = &arg_str[0];
    for (int i = 0; i < argc; i++) {
        strncpy(arg_ptr, argv[i], (&arg_str[MAX_ARG_STRLEN - 2] - arg_ptr));
        arg_ptr += strlen(arg_ptr);
        *arg_ptr = (i + 1 == argc) ? '\0' : ' ';
        ++arg_ptr;
    }

    pr_loc_dbg("execve@cpu%d: %s[%d]=>%s<%i> {%s}", caller->on_cpu, caller->comm, caller->pid, filename, argc,
               arg_str);

    kfree(arg_str);
}