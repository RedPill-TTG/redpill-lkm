#ifndef REDPILL_PLATFORM_TYPES_H
#define REDPILL_PLATFORM_TYPES_H

#include "vpci_types.h" //vpci_device_stub, MAX_VPCI_DEVS

#ifndef RP_MODULE_TARGET_VER
#error "The RP_MODULE_TARGET_VER is not defined - it is required to properly set VTKs"
#endif

//All HWMON_SYS enums defined here are for internal RP use only. Normally these have long names but duplicating names
// across multiple platforms is wasteful (and causes platforms.h compilation unit to grow)
//While adding new constants here MAKE SURE TO NOT CONFLICT with existing ones defining names in synobios.h (here we
// postfixed everything with _ID)
enum hwmon_sys_thermal_zone_id {
    //i.e. "non-existent zone" so that we don't need another flag/number to indicated # of supported zones
    HWMON_SYS_TZONE_NULL_ID = 0,
    HWMON_SYS_TZONE_REMOTE1_ID,
    HWMON_SYS_TZONE_REMOTE2_ID,
    HWMON_SYS_TZONE_LOCAL_ID,
    HWMON_SYS_TZONE_SYSTEM_ID,
    HWMON_SYS_TZONE_ADT1_LOC_ID,
    HWMON_SYS_TZONE_ADT2_LOC_ID,
};
#define HWMON_SYS_THERMAL_ZONE_IDS 5 //number of thermal zones minus the fake NULL_ID

enum hwmon_sys_voltage_sensor_id {
    //i.e. "non-existent sensor type" so that we don't need another flag/number to indicated # of supported ones
    HWMON_SYS_VSENS_NULL_ID = 0,
    HWMON_SYS_VSENS_VCC_ID,
    HWMON_SYS_VSENS_VPP_ID,
    HWMON_SYS_VSENS_V33_ID,
    HWMON_SYS_VSENS_V5_ID,
    HWMON_SYS_VSENS_V12_ID,
    HWMON_SYS_VSENS_ADT1_V33_ID,
    HWMON_SYS_VSENS_ADT2_V33_ID,
};
#define HWMON_SYS_VOLTAGE_SENSOR_IDS 7 //number of voltage sensors minus the fake NULL_ID

enum hwmon_sys_fan_rpm_id {
    //i.e. "non-existent fan" so that we don't need another flag/number to indicated # of supported fans
    HWMON_SYS_FAN_NULL_ID = 0,
    HWMON_SYS_FAN1_ID,
    HWMON_SYS_FAN2_ID,
    HWMON_SYS_FAN3_ID,
    HWMON_SYS_FAN4_ID,
};
#define HWMON_SYS_FAN_RPM_IDS 4

enum hwmon_sys_hdd_bp_id {
    //i.e. "non-existent backplane sensor" so that we don't need another flag/number to indicated # of supported ones
    HWMON_SYS_HDD_BP_NULL_ID = 0,
    HWMON_SYS_HDD_BP_DETECT_ID,
    HWMON_SYS_HDD_BP_ENABLE_ID,
};
#define HWMON_SYS_HDD_BP_IDS 2 //number of HDD backplane sensors minus the fake NULL_ID

enum hw_psu_sensor_id {
    //i.e. "non-existent PSU sensor" so that we don't need another flag/number to indicated # of supported ones
    HWMON_PSU_NULL_ID = 0,
    HWMON_PSU_PWR_IN_ID,
    HWMON_PSU_PWR_OUT_ID,
#if RP_MODULE_TARGET_VER == 6
    HWMON_PSU_TEMP_ID,
#elif RP_MODULE_TARGET_VER == 7
    HWMON_PSU_TEMP1_ID,
    HWMON_PSU_TEMP2_ID,
    HWMON_PSU_TEMP3_ID,
    HWMON_PSU_FAN_VOLT,
#endif
    HWMON_PSU_FAN_RPM_ID,
    HWMON_PSU_STATUS_ID,
};
#if RP_MODULE_TARGET_VER == 6
#define HWMON_PSU_SENSOR_IDS 2 //number of power supply sensors minus the fake NULL_ID
#elif RP_MODULE_TARGET_VER == 7
#define HWMON_PSU_SENSOR_IDS 8 //number of power supply sensors minus the fake NULL_ID
#else
#error "Unknown RP_MODULE_TARGET_VER version specified"
#endif

enum hwmon_sys_current_id {
    //i.e. "non-existent current sensor" so that we don't need another flag/number to indicated # of supported ones
    HWMON_SYS_CURR_NULL_ID = 0,
    HWMON_SYS_CURR_ADC_ID,
};
#define HWMON_SYS_CURRENT_IDS 1 //number of current sensors minus the fake NULL_ID

struct hw_config {
    const char *name; //the longest so far is "RR36015xs+++" (12+1)

    const struct vpci_device_stub pci_stubs[MAX_VPCI_DEVS];

    //All custom flags
    const bool emulate_rtc:1;
    const bool swap_serial:1; //Whether ttyS0 and ttyS1 are swapped (reverses CONFIG_SYNO_X86_SERIAL_PORT_SWAP)
    const bool reinit_ttyS0:1; //Should the ttyS0 be forcefully re-initialized after module loads
    const bool fix_disk_led_ctrl:1; //Disabled libata-scsi bespoke disk led control (which often crashes some v4 platforms)

    //See SYNO_HWMON_SUPPORT_ID in include/linux/synobios.h GPLed sources - it defines which ones are possible
    //These define which parts of ACPI HWMON should be emulated
    //For those with GetHwCapability() note enable DBG_HWCAP which will force bios_hwcap_shim to print original values.
    // Unless there's a good reason to diverge from the platform-defined values you should not.
    //Supported hwmon sensors; order of sensors within type IS IMPORTANT to be accurate with a real hardware. The number
    // of sensors is derived from the enums defining their types. Internally the absolute maximum number is determined
    // by MAX_SENSOR_NUM defined in include/linux/synobios.h
    const bool has_cpu_temp:1; //GetHwCapability(id = CAPABILITY_CPU_TEMP)
    const struct hw_config_hwmon {
        enum hwmon_sys_thermal_zone_id sys_thermal[HWMON_SYS_THERMAL_ZONE_IDS]; //GetHwCapability(id = CAPABILITY_THERMAL)
        enum hwmon_sys_voltage_sensor_id sys_voltage[HWMON_SYS_VOLTAGE_SENSOR_IDS];
        enum hwmon_sys_fan_rpm_id sys_fan_speed_rpm[HWMON_SYS_FAN_RPM_IDS]; //GetHwCapability(id = CAPABILITY_FAN_RPM_RPT)
        enum hwmon_sys_hdd_bp_id hdd_backplane[HWMON_SYS_HDD_BP_IDS];
        enum hw_psu_sensor_id psu_status[HWMON_PSU_SENSOR_IDS];
        enum hwmon_sys_current_id sys_current[HWMON_SYS_CURRENT_IDS];
    } hwmon;
};

#define platform_has_hwmon_thermal(hw_config_ptr) ((hw_config_ptr)->hwmon.sys_thermal[0] != HWMON_SYS_TZONE_NULL_ID)
#define platform_has_hwmon_voltage(hw_config_ptr) ((hw_config_ptr)->hwmon.sys_voltage[0] != HWMON_SYS_VSENS_NULL_ID)
#define platform_has_hwmon_fan_rpm(hw_config_ptr) ((hw_config_ptr)->hwmon.sys_fan_speed_rpm[0] != HWMON_SYS_FAN_NULL_ID)
#define platform_has_hwmon_hdd_bpl(hw_config_ptr) ((hw_config_ptr)->hwmon.hdd_backplane[0] != HWMON_SYS_HDD_BP_NULL_ID)
#define platform_has_hwmon_psu_status(hw_config_ptr) ((hw_config_ptr)->hwmon.psu_status[0] != HWMON_PSU_NULL_ID)
#define platform_has_hwmon_current_sens(hw_config_ptr) ((hw_config_ptr)->hwmon.sys_current[0] != HWMON_SYS_CURR_NULL_ID)

#endif //REDPILL_PLATFORM_TYPES_H
