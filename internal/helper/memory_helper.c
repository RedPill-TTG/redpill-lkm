/**
 * TODO: look into ovrride_symbol to check if there's any docs
 */
#include "memory_helper.h"
#include "../../common.h"
#include "../call_protected.h" //_flush_tlb_all()
#include <asm/cacheflush.h> //PAGE_ALIGN
#include <asm/page_types.h> //PAGE_SIZE
#include <asm/pgtable_types.h> //_PAGE_RW

#define PAGE_ALIGN_BOTTOM(addr) (PAGE_ALIGN(addr) - PAGE_SIZE) //aligns the memory address to bottom of the page boundary
#define NUM_PAGES_BETWEEN(low, high) (((PAGE_ALIGN_BOTTOM(high) - PAGE_ALIGN_BOTTOM(low)) / PAGE_SIZE) + 1)

void set_mem_addr_rw(const unsigned long vaddr, unsigned long len)
{
    unsigned long addr = PAGE_ALIGN_BOTTOM(vaddr);
    pr_loc_dbg("Disabling memory protection for page(s) at %p+%lu/%u (<<%p)", (void *) vaddr, len,
               (unsigned int) NUM_PAGES_BETWEEN(vaddr, vaddr + len), (void *) addr);

    //theoretically this should use set_pte_atomic() but we're touching pages that will not be modified by anything else
    unsigned int level;
    for(; addr <= vaddr; addr += PAGE_SIZE) {
        pte_t *pte = lookup_address(addr, &level);
        pte->pte |= _PAGE_RW;
    }

    _flush_tlb_all();
}

void set_mem_addr_ro(const unsigned long vaddr, unsigned long len)
{
    unsigned long addr = PAGE_ALIGN_BOTTOM(vaddr);
    pr_loc_dbg("Enabling memory protection for page(s) at %p+%lu/%u (<<%p)", (void *) vaddr, len,
               (unsigned int) NUM_PAGES_BETWEEN(vaddr, vaddr + len), (void *) addr);

    //theoretically this should use set_pte_atomic() but we're touching pages that will not be modified by anything else
    unsigned int level;
    for(; addr <= vaddr; addr += PAGE_SIZE) {
        pte_t *pte = lookup_address(addr, &level);
        pte->pte &= ~_PAGE_RW;
    }

    _flush_tlb_all();
}