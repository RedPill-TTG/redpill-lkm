/*
 * This submodule removes blacklisted entries from /proc/cmdline to hide some options after the LKM is loaded
 *
 * OVERVIEW
 * The main reason to sanitize cmdline is to avoid leaking information like "vid=..." or "pid=..." to userspace. Doing
 * this may cause some other external modules parsing kernel cmdline to be confused why such options are present in the
 * boot params.
 *
 * HOW IT WORKS?
 * The module overrides cmdline_proc_show() from fs/proc/cmdline.c with a jump to our implementation. The implementation
 * here serves a filtrated version of the cmdline.
 *
 * WHY OVERRIDE A STATIC METHOD?
 * This module has actually been rewritten to hard-override cmdline_proc_show() instead of "gently" finding the dentry
 * for /proc/cmdline, and then without modifying the dentry replacing the read operation in file_operations struct.
 * While this method is much cleaner and less invasive it has two problems:
 *  - Requires "struct proc_dir_entry" (which is internal and thus not available in toolkit builds)
 *  - Doesn't work if the module is loaded as ioscheduler (as funny enough this code will execute BEFORE /proc/cmdline
 *    is created)
 * This change has been made in commit "Rewrite cmdline sanitize to replace cmdline_proc_show".
 *
 * FILTRATION
 * The second part of the code deals with the actual filtration. List of blacklisted entries is passed during
 * registration (to allow flexibility). Usually it will be gathered form pre-generated config. Then a filtrated copy of
 * cmdline is created once (as this is a quite expensive string-ridden operation).
 * The only sort-of way to find the original implementation is to access the kmesg buffer where the original cmdline is
 * baked into early on boot. Technically we can replace that too but this will get veeery messy and I doubt anyone will
 * try dig through kmesg messages with a regex for cmdline (especially that with a small dmesg buffer it will roll over)
 */

#include "sanitize_cmdline.h"
#include "../../common.h"
#include "../../config/cmdline_delegate.h" //get_kernel_cmdline() & CMDLINE_MAX
#include "../override/override_symbol.h" //override_symbol() & restore_symbol()
#include <linux/seq_file.h> //seq_file, seq_printf()

/**
 * Pre-generated filtered cmdline (by default it's an empty string in case it's somehow printed before filtration)
 * See filtrate_cmdline() for details
 */
static char *filtrated_cmdline = NULL;

/**
 * Check if a given cmdline token is on the blacklist
 */
static bool
is_token_blacklisted(const char *param_pointer, cmdline_token *cmdline_blacklist[MAX_BLACKLISTED_CMDLINE_TOKENS]) {
    for (int i = 0; i < MAX_BLACKLISTED_CMDLINE_TOKENS; i++) {
        if (!cmdline_blacklist[i])
            return false;

        if (strncmp(param_pointer, (char *)cmdline_blacklist[i], strlen((char *)cmdline_blacklist[i])) == 0)
            return true;
    }

    return false;
}

/**
 * Filters-out all blacklisted entries from the cmdline string (fetched from /proc/cmdline)
 */
static int filtrate_cmdline(cmdline_token *cmdline_blacklist[MAX_BLACKLISTED_CMDLINE_TOKENS])
{
    char *raw_cmdline;
    kmalloc_or_exit_int(raw_cmdline, strlen_to_size(CMDLINE_MAX));

    long cmdline_len = get_kernel_cmdline(raw_cmdline, CMDLINE_MAX);
    if(unlikely(cmdline_len < 0)) { //if <0 it's an error code
        pr_loc_dbg("get_kernel_cmdline failed with %ld", cmdline_len);
        kfree(raw_cmdline);
        return (int) cmdline_len;
    }

    filtrated_cmdline = kmalloc(strlen_to_size(cmdline_len), GFP_KERNEL);
    if (unlikely(!filtrated_cmdline)) {
        kfree(raw_cmdline);
        kalloc_error_int(filtrated_cmdline, strlen_to_size(cmdline_len));
    }

    char *single_param_chunk; //Pointer to the beginning of the cmdline token
    char *filtrated_ptr = &filtrated_cmdline[0]; //Pointer to the current position in filtered

    size_t curr_param_len;
    while ((single_param_chunk = strsep(&raw_cmdline, CMDLINE_SEP)) != NULL) {
        if (single_param_chunk[0] == '\0') //Skip empty
            continue;

        if (is_token_blacklisted(single_param_chunk, cmdline_blacklist)) {
            pr_loc_dbg("Cmdline param \"%s\" blacklisted - skipping", single_param_chunk);
            continue;
        }

        curr_param_len = strlen(single_param_chunk);
        memcpy(filtrated_ptr, single_param_chunk, curr_param_len);
        filtrated_ptr += curr_param_len;
        *(filtrated_ptr++) = ' ';
    }

    *(filtrated_ptr-1) = '\0'; //Terminate whole param string (removing the trailing space)
    kfree(raw_cmdline);

    pr_loc_dbg("Sanitized cmdline to: %s", filtrated_cmdline);

    return 0;
}

/**
 * Handles fs/proc/ semantics for reading. See include/linux/fs.h:file_operations.read for details.
 */
static int cmdline_proc_show_filtered(struct seq_file *m, void *v)
{
    seq_printf(m, "%s\n", filtrated_cmdline);
    return 0;
}

static override_symbol_inst *ov_cmdline_proc_show = NULL;
int register_stealth_sanitize_cmdline(cmdline_token *cmdline_blacklist[MAX_BLACKLISTED_CMDLINE_TOKENS])
{
    if (unlikely(ov_cmdline_proc_show)) {
        pr_loc_bug("Attempted to %s while already registered", __FUNCTION__);
        return 0; //Technically it succeeded
    }

    int out;
    //This has to be done once (we're assuming cmdline doesn't change without reboot). In case this submodule is
    // re-registered the filtrated_cmdline is left as-is and reused
    if (!filtrated_cmdline && (out = filtrate_cmdline(cmdline_blacklist)) != 0)
        return out;

    ov_cmdline_proc_show = override_symbol("cmdline_proc_show", cmdline_proc_show_filtered);
    if (unlikely(IS_ERR(ov_cmdline_proc_show))) {
        out = PTR_ERR(ov_cmdline_proc_show);
        pr_loc_err("Failed to override cmdline_proc_show - error %d", out);
        ov_cmdline_proc_show = NULL;
        return out;
    }

    pr_loc_inf("/proc/cmdline sanitized");

    return 0;
}

int unregister_stealth_sanitize_cmdline(void)
{
    if (unlikely(!ov_cmdline_proc_show)) {
        pr_loc_bug("Attempted to %s while it's not registered", __FUNCTION__);
        return 0; //Technically it succeeded
    }

    int out = restore_symbol(ov_cmdline_proc_show);
    //We deliberately fall through here without checking as we have to free stuff at this point no matter what

    kfree(filtrated_cmdline);
    filtrated_cmdline = NULL;

    if (likely(out == 0))
        pr_loc_inf("Original /proc/cmdline restored");
    else
        pr_loc_err("Failed to restore original /proc/cmdline: org_cmdline_proc_show failed - error %d", out);

    return out;
}