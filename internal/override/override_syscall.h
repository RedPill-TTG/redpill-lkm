#ifndef REDPILL_OVERRIDE_SYSCALL_H
#define REDPILL_OVERRIDE_SYSCALL_H

#include "override_symbol.h"
#include <linux/syscalls.h>

//Modified syscall defines for shims based on native Linux syscalls (defined in linux/syscalls.h)
#define SYSCALL_SHIM_DEFINE1(name, ...) SYSCALL_SHIM_DEFINEx(1, _##name##_shim, __VA_ARGS__)
#define SYSCALL_SHIM_DEFINE2(name, ...) SYSCALL_SHIM_DEFINEx(2, _##name##_shim, __VA_ARGS__)
#define SYSCALL_SHIM_DEFINE3(name, ...) SYSCALL_SHIM_DEFINEx(3, _##name##_shim, __VA_ARGS__)
#define SYSCALL_SHIM_DEFINE4(name, ...) SYSCALL_SHIM_DEFINEx(4, _##name##_shim, __VA_ARGS__)
#define SYSCALL_SHIM_DEFINE5(name, ...) SYSCALL_SHIM_DEFINEx(5, _##name##_shim, __VA_ARGS__)
#define SYSCALL_SHIM_DEFINE6(name, ...) SYSCALL_SHIM_DEFINEx(6, _##name##_shim, __VA_ARGS__)
#define SYSCALL_SHIM_DEFINEx(x, name, ...)					         \
    static inline long SYSC##name(__MAP(x,__SC_DECL,__VA_ARGS__));	 \
	static asmlinkage long SyS##name(__MAP(x,__SC_LONG,__VA_ARGS__)) \
	{								                                 \
		long ret = SYSC##name(__MAP(x,__SC_CAST,__VA_ARGS__));	     \
		__MAP(x,__SC_TEST,__VA_ARGS__);				                 \
		__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));	         \
		return ret;						                             \
	}								                                 \
	static inline long SYSC##name(__MAP(x,__SC_DECL,__VA_ARGS__))

/**
 * Non-destructively overrides a syscall
 *
 * This produces an effect similar to override_symbol(). However, it should be faster, safer, and most importantly
 * allows calling the original syscall in the override.
 *
 * Warning: DO NOT use this method to override stubbed syscalls. These are syscall which aren't named "sys_foo" (e.g.
 * sys_execve or SyS_execve [alias]) but are handled by ASM stubs in arch/x86/kernel/entry_64.S (and visible as e.g.
 * stub_execve). If you do override such call with a normal function in the syscall table things will start breaking
 * unexpectedly as the registries will be modified in an unexpected way (stubs don't use cdecl)!
 * In such cases you need to override the actual sys_* (or even better: SyS_*) function with a jump using
 * override_symbol(). Either way you should use SYSCALL_SHIM_DEFINE#() to define the new target/shim.
 * Make sure to read https://lwn.net/Articles/604287/ and https://lwn.net/Articles/604406/
 *
 * @param syscall_num Number of the syscall to override (e.g. open)
 *                    You can find them as __NR_* defines in arch/x86/include/generated/uapi/asm/unistd_64.h
 * @param new_sysc_ptr An address/pointer to a new function
 * @param org_sysc_ptr Pointer to some space to save address of the original syscall (warning: it's a pointer-pointer);
 *                     You can pass a null-ptr if you don't care about the original syscall and the function will not
 *                     touch it
 *
 * @return 0 on success, -E on error
 */
int override_syscall(unsigned int syscall_num, const void *new_sysc_ptr, void * *org_sysc_ptr);

/**
 * Restores the syscall previously replaced by override_syscall()
 *
 * For details see override_syscall() docblock.
 *
 * @return 0 on success, -E on error
 */
int restore_syscall(unsigned int syscall_num);

#endif //REDPILL_OVERRIDE_SYSCALL_H
