#ifndef REDPILLLKM_OVERRIDE_KFUNC_H
#define REDPILLLKM_OVERRIDE_KFUNC_H

#define OVERRIDE_JUMP_SIZE 1 + 1 + 8 + 1 + 1 //MOVQ + %rax + $vaddr + JMP + *%rax

/**
 * Overrides a kernel symbol with something else of your choice
 *
 * @param name Name of the kernel symbol (function) to override
 * @param new_sym_ptr An address/pointer to a new function
 * @param org_sym_ptr Pointer to some space to save address of the original function (warning: it's a pointer-pointer)
 * @param org_sym_code Pointer to some space to save original function code
 *
 * @return 0 on success, -E on error
 *
 * @example
 *     int null_printk() { return 0; }
 *     void *backup_addr; //A space for a POINTER
 *     unsigned char backup_code[OVERRIDE_JUMP_SIZE] = { '\0' }; //don't forget to actually reserve space
 *     override_symbol("printk",    null_printk,    &backup_addr,                        backup_code);
 *     //               ^           ^               ^                                    ^
 *     //               override    new function    pass a pointer to a pointer-space    save backup of printk()
 *     ...
 *     restore_symbol(backup_addr, backup_code); //restore backed-up copy of printk()
 *
 * @todo: This should be rewritten using INSN without inline ASM wizardy, but this is much more complex
 */
int override_symbol(const char *name, const void *new_sym_ptr, void * *org_sym_ptr, unsigned char *org_sym_code);

/**
 * Restores symbol overridden by override_symbol()
 *
 * For details see override_symbol() docblock
 *
 * @return 0 on success, -E on error
 */
int restore_symbol(void * org_sym_ptr, const unsigned char *org_sym_code);

/**
 * Non-destructively overrides a syscall
 *
 * This produces an effect similar to override_symbol(). However, it should be faster, safer, and most importantly
 * allows calling the original syscall in the override.
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

#endif //REDPILLLKM_OVERRIDE_KFUNC_H
