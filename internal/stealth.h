#ifndef REDPILLLKM_STEALTH_H
#define REDPILLLKM_STEALTH_H

#ifdef STEALTH_MODE

/**
 * Prevents the module from being detected by overriding some things
 *
 * @see https://github.com/xcellerator/linux_kernel_hacking/blob/master/3_RootkitTechniques/3.0_hiding_lkm/rootkit.c
 */
void goStealth(void);

#endif //STEALTH_MODE


#endif //REDPILLLKM_STEALTH_H
