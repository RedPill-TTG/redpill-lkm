#ifndef REDPILLLKM_OVERRIDE_KFUNC_H
#define REDPILLLKM_OVERRIDE_KFUNC_H

#include <linux/types.h>

#define OVERRIDE_JUMP_SIZE 1 + 1 + 8 + 1 + 1 //MOVQ + %rax + $vaddr + JMP + *%rax
#define DEFINE_OVSYMBOL_PTRS(fname) \
    static unsigned char *fname##_code = NULL; \
    static void *fname##_addr = NULL;
#define ALLOC_OVSYMBOL_PTRS(fname) \
    fname##_code = kmalloc(OVERRIDE_JUMP_SIZE, GFP_KERNEL); \
    if (!unlikely(fname##_code)) { \
        pr_loc_crt("kmalloc failed"); \
        fname##_code = NULL; \
        return -EFAULT; \
    }
//it IS SAFE to call this even on potentially unallocated variables, it's an intentional design choice to make code
//using this more smooth in error handling
#define FREE_OVSYMBOL_PTRS(fname) \
    if (likely(fname##_code)) { kfree(fname##_code); } \
    fname##_code = NULL; \
    fname##_addr = NULL; \

typedef struct override_symbol_inst override_symbol_inst;

/************************************************* Current interface **************************************************/
/**
 * Calls the original symbol, returning nothing, that was previously overridden
 *
 * @param sym pointer to a override_symbol_inst
 * @param ... any arguments to the original function
 *
 * @return 0 if the execution succeeded, -E if it didn't
 */
#define call_overridden_symbol_void(sym, ...) ({              \
    int __ret;                                                \
    bool __was_installed = symbol_is_overridden(sym);         \
    _Pragma("GCC diagnostic push")                            \
    _Pragma("GCC diagnostic ignored \"-Wstrict-prototypes\"") \
    void (*__ptr)() = __get_org_ptr(sym);                     \
    _Pragma("GCC diagnostic pop")                             \
    __ret = __disable_symbol_override(sym);                   \
    if (likely(__ret == 0)) {                                 \
        __ptr(__VA_ARGS__);                                   \
        if (likely(__was_installed)) {                        \
        __ret = __enable_symbol_override(sym);                \
        }                                                     \
    }                                                         \
    __ret;                                                    \
});

/**
 * Calls the original symbol, returning a value, that was previously overridden
 *
 * @param out_var name of the variable where original function return value should be placed
 * @param sym pointer to a override_symbol_inst
 * @param ... any arguments to the original function
 *
 * @return 0 if the execution succeeded, -E if it didn't
 */
#define call_overridden_symbol(out_var, sym, ...) ({          \
    int __ret;                                                \
    bool __was_installed = symbol_is_overridden(sym);         \
    _Pragma("GCC diagnostic push")                            \
    _Pragma("GCC diagnostic ignored \"-Wstrict-prototypes\"") \
    typeof (out_var) (*__ptr)() = __get_org_ptr(sym);         \
    _Pragma("GCC diagnostic pop")                             \
    __ret = __disable_symbol_override(sym);                   \
    if (likely(__ret == 0)) {                                 \
        out_var = __ptr(__VA_ARGS__);                         \
        if (likely(__was_installed)) {                        \
            __ret = __enable_symbol_override(sym);            \
        }                                                     \
    }                                                         \
    __ret;                                                    \
});

/**
 * Overrides a kernel symbol with something else of your choice
 *
 * @param name Name of the kernel symbol (function) to override
 * @param new_sym_ptr An address/pointer to a new function
 *
 * @return Instance of override_symbol_inst struct pointer on success, ERR_PTR(-E) on error
 *
 * @example
 *     struct override_symbol_inst *ovi;
 *     int null_printk() {
 *         int print_res;
 *         call_overridden_symbol(print_res, ovi, "No print for you!");
 *         return print_res;
 *     }
 *     ovi = override_symbol("printk", null_printk);
 *     if (IS_ERR(ovi)) { ... handle error ... }
 *     ...
 *     restore_symbol(backup_addr, backup_code); //restore backed-up copy of printk()
 *
 * @todo: This should be rewritten using INSN without inline ASM wizardy, but this is much more complex
 */
struct override_symbol_inst* __must_check override_symbol(const char *name, const void *new_sym_ptr);

/**
 * Restores symbol overridden by override_symbol()
 *
 * For details see override_symbol() docblock
 *
 * @return 0 on success, -E on error
 */
int restore_symbol(struct override_symbol_inst *sym);

/**
 * Check if the given symbol override is currently active
 */
bool symbol_is_overridden(struct override_symbol_inst *sym);

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

/****************** Private helpers (should not be used directly by any code outside of this unit!) *******************/
#include <linux/types.h>
int __enable_symbol_override(override_symbol_inst *sym);
int __disable_symbol_override(override_symbol_inst *sym);
void * __get_org_ptr(struct override_symbol_inst *sym);

#endif //REDPILLLKM_OVERRIDE_KFUNC_H
