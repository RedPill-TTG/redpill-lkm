#ifndef REDPILL_RTC_PROXY_H
#define REDPILL_RTC_PROXY_H

#include "mfgbios_types.h"

/**
 * Gets current RTC time (shims VTK_RTC_GET_TIME)
 */
int rtc_proxy_get_time(struct MfgCompatTime *mfgTime);

/**
 * Sets current RTC time (shims VTK_RTC_SET_TIME)
 */
int rtc_proxy_set_time(struct MfgCompatTime *mfgTime);

/**
 * Enables auto-power on functionality (shims VTK_RTC_INT_APWR).
 *
 * This is not REALLY implemented and only shimmed. Many motherboards don't handle it well or only support it from
 * certain ACPI PSTATEs. It is even more unsupported by hypervisors. If you REALLY need it create a bug report or a PR.
 */
int rtc_proxy_init_auto_power_on(void);

/**
 * Gets time for auto-power on (shims VTK_RTC_GET_APWR). **See note for rtc_proxy_init_auto_power_on()**
 */
int rtc_proxy_get_auto_power_on(struct MfgCompatAutoPwrOn *mfgPwrOn);

/**
 * Sets time for auto-power on (shims VTK_RTC_SET_APWR). **See note for rtc_proxy_init_auto_power_on()**
 */
int rtc_proxy_set_auto_power_on(struct MfgCompatAutoPwrOn *mfgPwrOn);

/**
 * Disables auto-power on functionality (shims VTK_RTC_UINT_APWR). **See note for rtc_proxy_init_auto_power_on()**
 */
int rtc_proxy_uinit_auto_power_on(void);

int unregister_rtc_proxy_shim(void);
int register_rtc_proxy_shim(void);

#endif //REDPILL_RTC_PROXY_H
