#include "override_syscall.h"
#include "../../common.h"
#include "../helper/memory_helper.h" //set_mem_addr_ro(), set_mem_addr_rw()
#include <asm/asm-offsets.h> //__NR_syscall_max & NR_syscalls
#include <asm/unistd.h> //syscalls numbers (e.g. __NR_read)

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

static unsigned long *overridden_syscall[NR_syscalls] = { NULL }; //@todo this should be alloced dynamically
int override_syscall(unsigned int syscall_num, const void *new_sysc_ptr, void * *org_sysc_ptr)
{
    pr_loc_dbg("Overriding syscall #%d with %pf()<%p>", syscall_num, new_sysc_ptr, new_sysc_ptr);

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

    set_mem_addr_rw((long)&syscall_table_ptr[syscall_num], sizeof(unsigned long));
    pr_loc_dbg("syscall #%d originally %ps<%p> will now be %ps<%p> @ %d", syscall_num,
               (void *) overridden_syscall[syscall_num], (void *) overridden_syscall[syscall_num], new_sysc_ptr,
               new_sysc_ptr, smp_processor_id());
    syscall_table_ptr[syscall_num] = (unsigned long) new_sysc_ptr;
    set_mem_addr_ro((long)&syscall_table_ptr[syscall_num], sizeof(unsigned long));

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

    set_mem_addr_rw((long)&syscall_table_ptr[syscall_num], sizeof(unsigned long));
    pr_loc_dbg("Restoring syscall #%d from %ps<%p> to original %ps<%p>", syscall_num,
               (void *) syscall_table_ptr[syscall_num], (void *) syscall_table_ptr[syscall_num],
               (void *) overridden_syscall[syscall_num], (void *) overridden_syscall[syscall_num]);
    syscall_table_ptr[syscall_num] = (unsigned long)overridden_syscall[syscall_num];
    set_mem_addr_rw((long)&syscall_table_ptr[syscall_num], sizeof(unsigned long));

    print_syscall_table(syscall_num-5, syscall_num+5);

    return 0;
}