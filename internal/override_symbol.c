/**
 * This little (and dangerous) utility allows for replacement of any arbitrary kernel symbols with your own
 *
 * Since we're in the kernel we can do anything we want. This also included manipulating the actual code of functions
 * executed by the kernel. So, if someone calls printk() it normally goes to the correct place. However this place can
 * be override with a snippet of ASM which jumps to another place - a place we specify. It doesn't require a genius to
 * understand the power and implication of this ;)
 *
 * See header file for example of usage. In short this code does the following:
 * 0. Kernel protects .text pages (r/o by default, so you don't override the code by accident): they need to be unlocked
 * 1. Find where our symbol-to-be-replaced is located
 * 2. Disable memory pages write protection (WP bit in CR0 registry on x86) taking care of preempt
 *    (as symbols are in .text which is )
 * 4. Make memory page containing symbol-to-be-replaced r/w
 * 5. Generate jump code ASM containing the address of new symbol specified by the caller
 * 6. Mark the memory page from [4] r/o again and enable CR0 WP [2] back
 * 7. [optional] Process is fully reversible
 *
 *
 * There's also a variant made for syscalls specifically. It differs by the fact that override_symbol() makes
 * the original code unusable (as the first bytes are replaced with a jump) yet allows you to replace ANY function. The
 * overridden_syscall() in the other hand changes a pointer in the syscalls table. That way you CAN use the original
 * pointer to call back the original Linux syscall code you replaced. It works roughly like so:
 * 0. Kernel keeps syscalls table in .data section which is marked as r/o: it needs to be unlocked
 *
 * References:
 *  - https://www.cs.uaf.edu/2016/fall/cs301/lecture/09_28_machinecode.html
 *  - http://www.watson.org/%7Erobert/2007woot/2007usenixwoot-exploitingconcurrency.pdf
 */

#include "override_symbol.h"
#include "../common.h"
#include "call_protected.h" //set_memory_*()
#include <linux/preempt.h> //preempt_* macros
#include <asm/processor-flags.h> //X86_* flags
#include <asm/special_insns.h> //*_cr0()
#include <asm/cacheflush.h> //PAGE_ALIGN
#include <asm/asm-offsets.h> //__NR_syscall_max & NR_syscalls
#include <generated/uapi/asm/unistd_64.h> //syscalls numbers (e.g. __NR_read)
#include <linux/kallsyms.h> //kallsyms_lookup_name()
#include <linux/string.h> //memcpy()

#define JUMP_ADDR_POS 2 //JUMP starts at [2] in the jump template below
static const unsigned char jump_tpl[OVERRIDE_JUMP_SIZE] =
    "\x48\xb8" "\x00\x00\x00\x00\x00\x00\x00\x00" /* MOVQ 64-bit-vaddr, %rax */
    "\xff\xe0" /* JMP *%rax */
;

//While writing jump code it may land in just by the end of one page and cross to the next (unlikely). This macro
// calculates how many pages we need for the jumpcode:                last_page_beginning   -   first_page_beginning
#define NUM_PAGES_WITH_JUMP(vaddr) (((vaddr + OVERRIDE_JUMP_SIZE - 1) | ~PAGE_MASK) + 1)    -   PAGE_ALIGN(vaddr)

/**
 * Disables write-protection for the memory where symbol resides
 *
 * This in essence needs to first disable CR0 protection register (which normally blocks changing pages to r/w), then
 * switch the page to r/w. For details see CR0 documentation in Section 2.5 of Intel Developer's Manual:
 * https://software.intel.com/content/dam/develop/public/us/en/documents/325384-sdm-vol-3abcd.pdf
 * All this code needs to run with preempting disabled to prevent ctx switching and landing in on a different core.
 *
 * FYI: This will stop working in Linux 5.3:
 *      https://unix.stackexchange.com/questions/575122/does-linux-kernel-since-version-5-0-have-a-cr0-protection
 *      But of course there's a workaround already:
 *      https://hadfiabdelmoumene.medium.com/change-value-of-wp-bit-in-cr0-when-cr0-is-panned-45a12c7e8411
 */
static int disable_symbol_wp(const unsigned long vaddr)
{
    pr_loc_dbg("Disabling memory protection for page at %p (<<%p)", (void *)vaddr, (void *)PAGE_ALIGN(vaddr));

    preempt_disable();
    write_cr0(read_cr0() & (~X86_CR0_WP));

    int out = _set_memory_rw(PAGE_ALIGN(vaddr), NUM_PAGES_WITH_JUMP(vaddr));
    if (out != 0) {
        pr_loc_err("set_memory_rw() failed: %d", out);
        write_cr0(read_cr0() | X86_CR0_WP);
        preempt_enable();
    }

    return out;
}

/**
 * Reverses disable_symbol_wp()
 */
static int enable_symbol_wp(const unsigned long vaddr)
{
    pr_loc_dbg("Enabling memory protection for page at %p (<<%p)", (void *)vaddr, (void *)PAGE_ALIGN(vaddr));

    int out = _set_memory_ro(PAGE_ALIGN(vaddr), NUM_PAGES_WITH_JUMP(vaddr));
    write_cr0(read_cr0() | X86_CR0_WP);
    preempt_enable();

    if (out != 0)
        pr_loc_err("set_memory_ro() failed: %d", out);

    return out;
}

int override_symbol(const char *name, const void *new_sym_ptr, void * *org_sym_ptr, unsigned char *org_sym_code)
{
    pr_loc_dbg("Overriding %s() with f()<%p>", name, new_sym_ptr);

    *org_sym_ptr = (void *)kallsyms_lookup_name(name);
    if (*org_sym_ptr == 0) {
        pr_loc_err("Failed to locate vaddr for %s()", name);
        return -EFAULT;
    }
    pr_loc_dbg("Found %s() @ <%p>", name, *org_sym_ptr);

    //First generate jump new_sym_ptr
    unsigned char jump[OVERRIDE_JUMP_SIZE];
    memcpy(jump, jump_tpl, OVERRIDE_JUMP_SIZE);
    *(long *)&jump[JUMP_ADDR_POS] = (long)new_sym_ptr;
    pr_loc_dbg("Generated jump to f()<%p> for %s()<%p>: "
               "%02x%02x %02x%02x%02x%02x%02x%02x%02x%02x %02x%02x",
               new_sym_ptr, name, *org_sym_ptr,
               jump[0],jump[1], jump[2],jump[3],jump[4],jump[5],jump[6],jump[7],jump[8],jump[9], jump[10],jump[11]);

    memcpy(org_sym_code, *org_sym_ptr, OVERRIDE_JUMP_SIZE); //Backup old code

    int out = disable_symbol_wp((long)*org_sym_ptr);
    if (out != 0) //disable_symbol_wp() already logs what happened
        return out;

    pr_loc_dbg("Writing jump code to <%p>", *org_sym_ptr);
    memcpy(*org_sym_ptr, jump, OVERRIDE_JUMP_SIZE);
    out = enable_symbol_wp((long)*org_sym_ptr); //already logs what happened if it failed

    pr_loc_dbg("Override for %s set up with %p", name, new_sym_ptr);

    return out;
}

int restore_symbol(void * org_sym_ptr, const unsigned char *org_sym_code)
{
    pr_loc_dbg("Restoring symbol @ %p", org_sym_ptr);

    int out = disable_symbol_wp((long)org_sym_ptr);
    if (out != 0) //disable_symbol_wp() already logs what happened
        return out;

    pr_loc_dbg("Writing original code to <%p>", org_sym_ptr);
    memcpy(org_sym_ptr, org_sym_code, OVERRIDE_JUMP_SIZE);

    out = enable_symbol_wp((long)org_sym_ptr); //already logs what happened if it failed
    pr_loc_dbg("Symbol restored @ %p", org_sym_ptr);

    return 0;
}

static unsigned long *syscall_table_ptr = NULL;
static void print_syscall_table(unsigned int from, unsigned to)
{
    if (unlikely(!syscall_table_ptr)) {
        pr_loc_dbg("Cannot print - no syscall_table_ptr address");
        return;
    }

    if (unlikely(from < 0 || to > __NR_syscall_max || from > to)) {
        pr_loc_bug("%s called with from=%d to=%d which are invalid", __FUNCTION__, from, to);
        return;
    }

    pr_loc_dbg("Printing syscall table %d-%d @ %p containing %d elements", from, to, (void *)syscall_table_ptr, NR_syscalls);
    for (unsigned int i = from; i < to; i++) {
        pr_loc_dbg("#%03d\t%pS", i, (void *)syscall_table_ptr[i]);
    }
}

static int find_sys_call_table(void)
{
    syscall_table_ptr = (unsigned long *)kallsyms_lookup_name("sys_call_table");
    if (syscall_table_ptr != 0) {
        pr_loc_dbg("Found sys_call_table @ <%p> using kallsyms", syscall_table_ptr);
        return 0;
    }

    //See https://kernelnewbies.kernelnewbies.narkive.com/L1uH0n8P/
    //In essence some systems will have it and some will not - finding it using kallsyms is the easiest and fastest
    pr_loc_dbg("Failed to locate vaddr for sys_call_table using kallsyms - falling back to memory search");

    /*
     There's also the bruteforce way - scan through the memory until you find it :D
     We know numbers for syscalls (e.g. __NR_close, __NR_write, __NR_read, etc.) which are essentially fixed positions
     in the sys_call_table. We also know addresses of functions handling these calls (sys_close/sys_write/sys_read
     etc.). This lets us scan the memory for one syscall address reference and when found confirm if this is really
     a place of sys_call_table by verifying other 2-3 places to make sure other syscalls are where they should be
     The huge downside of this method is it is slow as potentially the amount of memory to search may be large.
    */
    unsigned long sys_close_ptr = kallsyms_lookup_name("sys_close");
    unsigned long sys_open_ptr = kallsyms_lookup_name("sys_open");
    unsigned long sys_read_ptr = kallsyms_lookup_name("sys_read");
    unsigned long sys_write_ptr = kallsyms_lookup_name("sys_write");
    if (sys_close_ptr == 0 || sys_open_ptr == 0 || sys_read_ptr == 0 || sys_write_ptr == 0) {
        pr_loc_bug(
                "One or more syscall handler addresses cannot be located: "
                "sys_close<%p>, sys_open<%p>, sys_read<%p>, sys_write<%p>",
                (void *)sys_close_ptr, (void *)sys_open_ptr, (void *)sys_read_ptr, (void *)sys_write_ptr);
         return -EFAULT;
    }

    /*
     To speed up things a bit we search from a known syscall which was loaded early into the memory. To be safe we pick
     the earliest address and go from there. It can be nicely visualized on a system which DO export sys_call_table
     by running grep -E ' (__x64_)?sys_(close|open|read|write|call_table)$' /proc/kallsyms | sort
     You will get something like that:
      ffffffff860c18b0 T __x64_sys_close
      ffffffff860c37a0 T __x64_sys_open
      ffffffff860c7a80 T __x64_sys_read
      ffffffff860c7ba0 T __x64_sys_write
      ffffffff86e013a0 R sys_call_table    <= it's way below any of the syscalls but not too far (~13,892,336 bytes)
    */
    unsigned long i = sys_close_ptr;
    if (sys_open_ptr < i) i = sys_open_ptr;
    if (sys_read_ptr < i) i = sys_read_ptr;
    if (sys_write_ptr < i) i = sys_write_ptr;

    //If everything goes well it should take ~1-2ms tops (which is slow in the kernel sense but it's not bad)
    pr_loc_dbg("Scanning memory for sys_call_table starting at %p", (void *)i);
    for (; i < ULONG_MAX; i += sizeof(void *)) {
        syscall_table_ptr = (unsigned long *)i;

        if (unlikely(
                syscall_table_ptr[__NR_close] == sys_close_ptr &&
                syscall_table_ptr[__NR_open] == sys_open_ptr &&
                syscall_table_ptr[__NR_read] == sys_read_ptr &&
                syscall_table_ptr[__NR_write] == sys_write_ptr
        )) {
            pr_loc_dbg("Found sys_call_table @ %p", (void *)syscall_table_ptr);
            return 0;
        }
    }

    pr_loc_bug("Failed to find sys call table");
    syscall_table_ptr = NULL;
    return -EFAULT;
}

static unsigned long *overridden_syscall[NR_syscalls] = { NULL };
int override_syscall(unsigned int syscall_num, const void *new_sysc_ptr, void * *org_sysc_ptr)
{
    pr_loc_dbg("Overriding syscall #%d with f()<%p>", syscall_num, new_sysc_ptr);

    int out = 0;
    if (unlikely(!syscall_table_ptr)) {
        out = find_sys_call_table();
        if (unlikely(out != 0))
            return out;
    }

    if (unlikely(syscall_num > __NR_syscall_max)) {
        pr_loc_bug("Invalid syscall number: %d > %d", syscall_num, __NR_syscall_max);
        return -EINVAL;
    }

    print_syscall_table(syscall_num-5, syscall_num+5);

    if (unlikely(overridden_syscall[syscall_num])) {
        pr_loc_bug("Syscall %d is already overridden - will be replaced (bug?)", syscall_num);
    } else {
        //Only save original-original entry (not the override one)
        overridden_syscall[syscall_num] = (unsigned long *)syscall_table_ptr[syscall_num];
    }

    if (org_sysc_ptr != 0)
        *org_sysc_ptr = overridden_syscall[syscall_num];

    out = disable_symbol_wp((long)&syscall_table_ptr[syscall_num]);
    if (out != 0) //disable_symbol_wp() already logs what happened
        return out;

    pr_loc_dbg("syscall #%d originally %ps<%p> will now be %ps<%p>", syscall_num,
               (void *) overridden_syscall[syscall_num], (void *) overridden_syscall[syscall_num], new_sysc_ptr,
               new_sysc_ptr);
    syscall_table_ptr[syscall_num] = (unsigned long) new_sysc_ptr;
    out = enable_symbol_wp((long)&syscall_table_ptr[syscall_num]); //already logs what happened if it failed

    print_syscall_table(syscall_num-5, syscall_num+5);

    return out;
}

int restore_syscall(unsigned int syscall_num)
{
    pr_loc_dbg("Restoring syscall #%d", syscall_num);

    if (unlikely(!syscall_table_ptr)) {
        pr_loc_bug("Syscall table not found in %s ?!", __FUNCTION__);
        return -EFAULT;
    }

    if (unlikely(syscall_num > __NR_syscall_max)) {
        pr_loc_bug("Invalid syscall number: %d > %d", syscall_num, __NR_syscall_max);
        return -EINVAL;
    }

    if (unlikely(overridden_syscall[syscall_num] == 0)) {
        pr_loc_bug("Syscall #%d cannot be restored - it was never overridden", syscall_num);
        return -EINVAL;
    }

    print_syscall_table(syscall_num-5, syscall_num+5);

    int out = 0;
    out = disable_symbol_wp((long)&syscall_table_ptr[syscall_num]);
    if (out != 0) //disable_symbol_wp() already logs what happened
        return out;

    pr_loc_dbg("Restoring syscall #%d from %ps<%p> to original %ps<%p>", syscall_num,
               (void *) syscall_table_ptr[syscall_num], (void *) syscall_table_ptr[syscall_num],
               (void *) overridden_syscall[syscall_num], (void *) overridden_syscall[syscall_num]);
    syscall_table_ptr[syscall_num] = (unsigned long)overridden_syscall[syscall_num];
    out = enable_symbol_wp((long)&syscall_table_ptr[syscall_num]); //already logs what happened if it failed

    print_syscall_table(syscall_num-5, syscall_num+5);

    return 0;
}
