#ifndef REDPILL_SANITIZE_CMDLINE_H
#define REDPILL_SANITIZE_CMDLINE_H

#include "../../config/cmdline_delegate.h" //MAX_BLACKLISTED_CMDLINE_TOKENS

/**
 * Register submodule sanitizing /proc/cmdline
 *
 * After registration /proc/cmdline will be non-destructively cleared from entries listed in cmdline_blacklist param.
 * It can be reversed using unregister_stealth_sanitize_cmdline()
 *
 * @return 0 on success, -E on error
 */
int register_stealth_sanitize_cmdline(cmdline_token *cmdline_blacklist[MAX_BLACKLISTED_CMDLINE_TOKENS]);

/**
 * Reverses what register_stealth_sanitize_cmdline() did
 *
 * @return 0 on success, -E on error
 */
int unregister_stealth_sanitize_cmdline(void);

#endif //REDPILL_SANITIZE_CMDLINE_H
