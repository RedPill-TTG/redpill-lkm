/*
 * This submodule removes blacklisted entries from /proc/cmdline to hide some options after the LKM is loaded
 *
 * The main reason to sanitize cmdline is to avoid leaking information like "vid=..." or "pid=..." to userspace. Doing
 * this may cause some other external modules parsing kernel cmdline to be confused why such options are present in the
 * boot params.
 *
 * This code is actually quite simple (in comparison to other parts). It first finds the dentry for /proc/cmdline, and
 * then without modifying the dentry itself replaces file operations parts of it. The new file operations point to our
 * implementation of /proc/cmdline which is a bare-standard proc entry implementation printing "filtrated_cmdline" to
 * userspace when requested.
 * The second part of the code deals with the actual filtration. List of blacklisted entries is passed during
 * registration (to allow flexibility). Usually it will be gathered form pre-generated config. Then a filtrated copy of
 * cmdline is created once (as this is a quite expensive string-ridden operation). This way trying to detect
 * modification of /proc/cmdline from userspace is virtually impossible as the timings are exactly like the original
 * implementation (found in fs/proc/cmdline.c).
 * The only sort-of way to find the original implementation is to access the kmesg buffer where the original cmdline is
 * baked into early on boot. Technically we can replace that too but this will get veeery messy and I doubt anyone will
 * try dig through kmesg messages with a regex for cmdline.
 */

#include "sanitize_cmdline.h"
#include "../../common.h"
#include "../../config/cmdline_delegate.h" //get_kernel_cmdline() & CMDLINE_MAX
#include <linux/proc_fs.h> //proc_create() & remove_proc_entry()
#include <linux/seq_file.h> //seq_*
#include <../fs/proc/internal.h> //proc_dir_entry internal structure

/**
 * Pre-generated filtered cmdline (by default it's an empty string in case it's somehow printed before filtration)
 * See filtrate_cmdline() for details
 */
static char *filtrated_cmdline = "";

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
    char *raw_cmdline = kmalloc_array(CMDLINE_MAX, sizeof(char), GFP_KERNEL);
    if (unlikely(!raw_cmdline)) {
        pr_loc_crt("kmalloc_array failed");
        return -EFAULT; //no free due to kmalloc failure
    }

    long cmdline_len = get_kernel_cmdline(raw_cmdline, CMDLINE_MAX);
    if(unlikely(cmdline_len < 0)) { //if <0 it's an error code
        pr_loc_dbg("get_kernel_cmdline failed with %ld", cmdline_len);
        return (int) cmdline_len;
    }

    filtrated_cmdline = kmalloc_array(cmdline_len + 1, sizeof(char), GFP_KERNEL);
    if (unlikely(!filtrated_cmdline)) {
        pr_loc_crt("kmalloc_array failed");
        return -EFAULT; //no free due to kmalloc failure
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

    pr_loc_dbg("Sanitized cmdline to: %s", filtrated_cmdline);

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
/**
 * This function has been copied verbatim from fs/proc/generic.c:proc_match()
 *
 * Copyright (C) 1991, 1992 Linus Torvalds.
 * Copyright (C) 1997 Theodore Ts'o
 */
static int proc_match(unsigned int len, const char *name, struct proc_dir_entry *de)
{
    if (len < de->namelen)
        return -1;
    if (len > de->namelen)
        return 1;

    return memcmp(name, de->name, len);
}

/**
 * Find a correct proc entry in the red-black tree. This is used since Linux v3.19 instead of a linked list:
 *  https://github.com/torvalds/linux/commit/710585d4922fd315f2cada8fbe550ae8ed23e994
 *
 * This is a modified version of fs/proc/generic.c:pde_subdir_find()
 *
 * Copyright (C) 1991, 1992 Linus Torvalds.
 * Copyright (C) 1997 Theodore Ts'o
 *
 * @param dir Pass the DIRECTORY to search in
 */
static struct proc_dir_entry *pde_find(struct proc_dir_entry *dir, const char *name)
{
    struct rb_node *node = dir->subdir.rb_node;
    u8 len = strlen(name);

    while (node) {
        struct proc_dir_entry *de = container_of(node, struct proc_dir_entry, subdir_node);
        int result = proc_match(len, name, de);

        if (result < 0)
            node = node->rb_left;
        else if (result > 0)
            node = node->rb_right;
        else
            return de;
    }

    return NULL;
}
#else
/**
 * Kernels till 3.19 use a simple linked list to store proc entries. This will find the correct one
 *
 * @param dir Pass the ENTRY IN A DIRECTORY to search in such directory
 *
 * @todo This can probably be improved to work like pde_find for >3.19 (i.e. take a directory and not entry)
 */
static inline struct proc_dir_entry *pde_find(struct proc_dir_entry *proc_entry, const char *name)
{
    u8 name_len = strlen(name);

    for (proc_entry = proc_entry->subdir; proc_entry; proc_entry = proc_entry->next) {
        if (unlikely(proc_entry->namelen == name_len) &&
            strncmp(proc_entry->name, "cmdline", sizeof_str_chunk("cmdline")) == 0)
            return proc_entry;
    }

    return NULL;
}
#endif

/**
 * Located directory entry structure for the original /proc/cmdline
 *
 * The kernel technically has fs/proc/generic.c:xlate_proc_name() but it's static, so it cannot be used from the
 * outside. However, we can create a dummy entry in /proc/__dummy__ to get parent (which will point to /proc) and then
 * find the one for /proc/cmdline
 *
 * This function calls version-dependant pde_find(). See the preprocessor "if" above
 *
 * @return pointer to original cmdline dentry or error pointer if it cannot be found (should never happen)
 */
static struct proc_dir_entry *locate_proc_cmdline(void)
{
    const struct file_operations dummy_proc_fops = { NULL };
    struct proc_dir_entry *proc_dummy_entry = proc_create("__dummy__", 0, NULL, &dummy_proc_fops);
    struct proc_dir_entry *proc_cmdline = pde_find(proc_dummy_entry->parent, "cmdline");
    remove_proc_entry("__dummy__", NULL);

    return proc_cmdline ? proc_cmdline : ERR_PTR(-ENOENT);
}

/**
 * Handles fs/proc/ semantics for reading. See include/linux/fs.h:file_operations.read for details.
 */
static int proc_cmdline_print_filtered(struct seq_file *m, void *v)
{
    seq_printf(m, "%s\n", filtrated_cmdline);
    return 0;
}

/**
 * Handles fs/proc/ semantics for openning. See include/linux/fs.h:file_operations.open for details.
 */
static int proc_cmdline_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_cmdline_print_filtered, NULL);
}

//This MUST be a global variable and not on the stack
static const struct file_operations filter_cmdline_fops = {
        .open		= proc_cmdline_open,
        .read		= seq_read,
        .llseek		= seq_lseek,
        .release	= single_release,
};


static struct proc_dir_entry *cmdline_dentry = NULL; //dentry for /proc/cmdline (it's located but not overwritten)
static const struct file_operations *original_fops = NULL; //original pointer to /proc/cmdline dentry->proc_fops
int register_stealth_sanitize_cmdline(cmdline_token *cmdline_blacklist[MAX_BLACKLISTED_CMDLINE_TOKENS])
{
    if (unlikely(cmdline_dentry)) {
        pr_loc_bug("Attempted to %s while already registered", __FUNCTION__);
        return 0; //Technically it succeeded
    }

    cmdline_dentry = locate_proc_cmdline();
    if (unlikely(IS_ERR(cmdline_dentry))) {
        pr_loc_bug("Failed to locate original cmdline");
        cmdline_dentry = NULL;
        return -ENOENT;
    }

    //This has to be done once (we're assuming cmdline doesn't change without reboot). In case this submodule is
    // re-registered the filtrated_cmdline is left as-is and reused
    if (filtrated_cmdline[0] == '\0' && filtrate_cmdline(cmdline_blacklist) != 0)
        return -EFAULT;

    //This allows us to replace it atomically - even if something has the cmdline currenty opened it will not fail
    // but will produce new (filtered) cmdline on re-open. Also, inotify() tricks will not reveal that we're replacing
    // cmdline ;)
    original_fops = cmdline_dentry->proc_fops;
    cmdline_dentry->proc_fops = &filter_cmdline_fops;

    pr_loc_inf("/proc/cmdline sanitized");

    return 0;
}

int unregister_stealth_sanitize_cmdline(void)
{
    if (unlikely(!cmdline_dentry)) {
        pr_loc_bug("Attempted to %s while it's not registered", __FUNCTION__);
        return 0; //Technically it succeeded
    }

    cmdline_dentry->proc_fops = original_fops;

    pr_loc_inf("Original /proc/cmdline restored");

    return 0;
}