#ifndef REDPILLLKM_CMDLINE_DELEGATE_H
#define REDPILLLKM_CMDLINE_DELEGATE_H

#include "runtime_config.h"
#include "cmdline_opts.h"

/**
 * Provides an easy access to kernel cmdline
 *
 * Internally in the kernel code it is available as "saved_command_line". However that variable is not accessible for
 * modules. This function populates a char buffer with the cmdline extracted using other methods.
 *
 * WARNING: if something (e.g. sanitize cmdline) overrides the cmdline this method will return the overridden one!
 *          However, this method caches the cmdline, so if you call it once it will cache the original one internally.
 *
 * @param cmdline_out A pointer to your buffer to save the cmdline
 * @param maxlen Your buffer space (in general you should use CMDLINE_MAX)
 * @return cmdline length on success or -E on error
 */
long get_kernel_cmdline(char *cmdline_out, unsigned long maxlen);

/**
 * Extracts & processes parameters from kernel cmdline
 *
 * Note: it's not guaranteed that the config will be valid. Check runtime_config.h.
 *
 * @param config pointer to save configuration
 */
int extract_config_from_cmdline(struct runtime_config *config);

#endif //REDPILLLKM_CMDLINE_DELEGATE_H
