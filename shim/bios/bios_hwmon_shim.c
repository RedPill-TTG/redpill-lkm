/**
 * Responds to all HWMON ("hardware monitor") calls coming to the mfgBIOS
 *
 * This submodule emulates both legitimate HWMON calls as well as "legacy" hardware monitoring calls get_fan_status()
 */
#include "bios_hwmon_shim.h"
#include "../shim_base.h" //shim_reg_in(), shim_reg_ok(), shim_reset_in(), shim_reset_ok()
#include "bios_shims_collection.h" //_shim_bios_module_entry()
#include "../../common.h"
#include "../../internal/helper/math_helper.h" //prandom_int_range_stable
#include "mfgbios_types.h" //HWMON_*
#include "../../config/platform_types.h" //HWMON_*_ID

#define SHIM_NAME "mfgBIOS HW Monitor"
#ifdef DBG_HWMON
#define hwmon_pr_loc_dbg(...) pr_loc_dbg(__VA_ARGS__)
#else
#define hwmon_pr_loc_dbg(...) //noop
#endif

/************************************* Standards for generating fake sensor readings **********************************/
//Standard deviations for ongoing sensor readings
#define FAN_SPEED_DEV 50 //Fan speed (RPM) deviation
#define VOLT_DEV 5 //Voltage (mV) deviation
#define TEMP_DEV 2 //Temperature (°C) deviation

#define FAKE_SURFACE_TEMP_MIN 25
#define FAKE_SURFACE_TEMP_MAX 35
#define FAKE_CPU_TEMP_MIN 55
#define FAKE_CPU_TEMP_MAX 65
#define FAKE_RPM_MIN 980
#define FAKE_RPM_MAX 1000

//These percentages are precalculated as we cannot use FPU [safely and easily] in kernel space
#define FAKE_V33_MIN 3135 // mV (-5% of 3.3V)
#define FAKE_V33_MAX 3465 // mV (+5% of 3.3V)
#define FAKE_V5_MIN 4750 // mV (-5% of 5.0V)
#define FAKE_V5_MAX 5250 // mV (+5% of 5.0V)
#define FAKE_V12_MIN 11400 // mV (-5% of 12.0V)
#define FAKE_V12_MAX 12600 // mV (+5% of 12.0V)
#define fake_volt_min(type) (hwmon_sys_vsens_type_base[(type)][0]) // mV
#define fake_volt_max(type) (hwmon_sys_vsens_type_base[(type)][1]) // mV

/************************************* Maps between hwmon sensor types & their names **********************************/
static const char *hwmon_sys_thermal_zone_id_map[] = {
    [HWMON_SYS_TZONE_NULL_ID] = "",
    [HWMON_SYS_TZONE_REMOTE1_ID] = HWMON_SYS_TZONE_REMOTE1_NAME,
    [HWMON_SYS_TZONE_REMOTE2_ID] = HWMON_SYS_TZONE_REMOTE2_NAME,
    [HWMON_SYS_TZONE_LOCAL_ID] = HWMON_SYS_TZONE_LOCAL_NAME,
    [HWMON_SYS_TZONE_SYSTEM_ID] = HWMON_SYS_TZONE_SYSTEM_NAME,
    [HWMON_SYS_TZONE_ADT1_LOC_ID] = HWMON_SYS_TZONE_ADT1_LOC_NAME,
    [HWMON_SYS_TZONE_ADT2_LOC_ID] = HWMON_SYS_TZONE_ADT2_LOC_NAME,
};

static const char *hwmon_sys_vsens_id_map[] = {
    [HWMON_SYS_VSENS_NULL_ID] = "",
    [HWMON_SYS_VSENS_VCC_ID] = HWMON_SYS_VSENS_VCC_NAME,
    [HWMON_SYS_VSENS_VPP_ID] = HWMON_SYS_VSENS_VPP_NAME,
    [HWMON_SYS_VSENS_V33_ID] = HWMON_SYS_VSENS_V33_NAME,
    [HWMON_SYS_VSENS_V5_ID] = HWMON_SYS_VSENS_V5_NAME,
    [HWMON_SYS_VSENS_V12_ID] = HWMON_SYS_VSENS_V12_NAME,
    [HWMON_SYS_VSENS_ADT1_V33_ID] = HWMON_SYS_VSENS_ADT1_V33_NAME,
    [HWMON_SYS_VSENS_ADT2_V33_ID] = HWMON_SYS_VSENS_ADT2_V33_NAME,
};

static const int hwmon_sys_vsens_type_base[][2] = {
    [HWMON_SYS_VSENS_NULL_ID] = {0, 0},
    [HWMON_SYS_VSENS_VCC_ID] = { FAKE_V12_MIN, FAKE_V12_MAX }, //todo: this is probably per-model
    [HWMON_SYS_VSENS_VPP_ID] = { 100, 500 }, //todo: if this is really peak-to-peak it should be small
    [HWMON_SYS_VSENS_V33_ID] = { FAKE_V33_MIN, FAKE_V33_MAX },
    [HWMON_SYS_VSENS_V5_ID] = { FAKE_V5_MIN, FAKE_V5_MAX },
    [HWMON_SYS_VSENS_V12_ID] = { FAKE_V12_MIN, FAKE_V12_MAX },
    [HWMON_SYS_VSENS_ADT1_V33_ID] = { FAKE_V33_MIN, FAKE_V33_MAX },
    [HWMON_SYS_VSENS_ADT2_V33_ID] = { FAKE_V33_MIN, FAKE_V33_MAX },
};

static const char *hwmon_sys_fan_id_map[] = {
    [HWMON_SYS_FAN_NULL_ID] = "",
    [HWMON_SYS_FAN1_ID] = HWMON_SYS_FAN1_RPM,
    [HWMON_SYS_FAN2_ID] = HWMON_SYS_FAN2_RPM,
    [HWMON_SYS_FAN3_ID] = HWMON_SYS_FAN3_RPM,
    [HWMON_SYS_FAN4_ID] = HWMON_SYS_FAN4_RPM,
};

static const char *hwmon_hdd_bp_id_map[] = {
    [HWMON_SYS_HDD_BP_NULL_ID] = "",
    [HWMON_SYS_HDD_BP_DETECT_ID] = HWMON_HDD_BP_DETECT,
    [HWMON_SYS_HDD_BP_ENABLE_ID] = HWMON_HDD_BP_ENABLE,
};

//todo: it's defined as __used as we know the structure but don't implement it yet
static const __used char *hwmon_psu_id_map[] = {
    [HWMON_PSU_NULL_ID] = "",
    [HWMON_PSU_PWR_IN_ID] = HWMON_PSU_SENSOR_PIN,
    [HWMON_PSU_PWR_OUT_ID] = HWMON_PSU_SENSOR_POUT,
#if RP_MODULE_TARGET_VER == 6
    [HWMON_PSU_TEMP_ID] = HWMON_PSU_SENSOR_TEMP,
#elif RP_MODULE_TARGET_VER == 7
    [HWMON_PSU_TEMP1_ID] = HWMON_PSU_SENSOR_TEMP1,
    [HWMON_PSU_TEMP2_ID] = HWMON_PSU_SENSOR_TEMP2,
    [HWMON_PSU_TEMP3_ID] = HWMON_PSU_SENSOR_TEMP3,
    [HWMON_PSU_FAN_VOLT] = HWMON_PSU_SENSOR_FAN_VOLT,
#endif
    [HWMON_PSU_FAN_RPM_ID] = HWMON_PSU_SENSOR_FAN,
    [HWMON_PSU_STATUS_ID] = HWMON_PSU_SENSOR_STATUS,
};

//todo: it's defined as __used as we know the structure but don't implement it yet
static const __used char *hwmon_current_id_map[] = {
    [HWMON_SYS_CURR_NULL_ID] = "",
    [HWMON_SYS_CURR_ADC_ID] = HWMON_SYS_CURR_ADC_NAME,
};

/************************************************ Various small tools *************************************************/
static const struct hw_config_hwmon *hwmon_cfg = NULL;
#define guard_hwmon_cfg() \
    if (unlikely(!hwmon_cfg)) { \
        pr_loc_bug("Called %s without hwmon_cfg context being populated", __FUNCTION__); \
        return -EIO; \
    }

#define guarded_strscpy(dest, src, count)                     \
    if (unlikely(strscpy(dest, src, count) == -E2BIG)) {      \
        pr_loc_err("Failed to copy %lu bytes string", count); \
        return -EFAULT;                                       \
    }

/******************************************* mfgBIOS LKM replacement functions ****************************************/
/**
 * Provides fan status
 * 
 * Currently the fan is always assumed to be running
 */
static int bios_get_fan_state(int no, enum MfgCompatFanStatus *status)
{
    hwmon_pr_loc_dbg("mfgBIOS: GET_FAN_STATE(%d) => MFGC_FAN_RUNNING", no);
    *status = MFGC_FAN_RUNNING;
    return 0;
}

static int cur_cpu_temp = 0;
/**
 * Returns CPU temperature across all cores
 *
 * Currently it always returns a fake value. However, it should only do so if running under hypervisor. In bare-metal
 * scenario we can simply proxy to syno_cpu_temperature() [or not override that part at all].
 */
static int bios_get_cpu_temp(SYNOCPUTEMP *temp)
{
    int fake_temp = prandom_int_range_stable(&cur_cpu_temp, TEMP_DEV, FAKE_CPU_TEMP_MIN, FAKE_CPU_TEMP_MAX);
    temp->cpu_num = MAX_CPU;
    for(int i=0; i < MAX_CPU; ++i)
        temp->cpu_temp[i] = fake_temp;

    hwmon_pr_loc_dbg("mfgBIOS: GET_CPU_TEMP(surf=%d, cpuNum=%d) => %d°C", temp->blSurface, temp->cpu_num, fake_temp);

    return 0;
}

static int *hwmon_thermals = NULL;
/**
 * Returns various HWMON temperatures
 *
 * @param reading Pointer to save results
 * @return 0 on success, -E on error
 */
static int bios_hwmon_get_thermal(SYNO_HWMON_SENSOR_TYPE *reading)
{
    guard_hwmon_cfg();
    if (unlikely(!hwmon_thermals))
        kzalloc_or_exit_int(hwmon_thermals, sizeof(int) * HWMON_SYS_THERMAL_ZONE_IDS);

    guarded_strscpy(reading->type_name, HWMON_SYS_THERMAL_NAME, sizeof(reading->type_name));
    hwmon_pr_loc_dbg("mfgBIOS: => %s(type=%s)", __FUNCTION__, reading->type_name);

    for (int i = 0; i < HWMON_SYS_THERMAL_ZONE_IDS; i++) {
        if (hwmon_cfg->sys_thermal[i] == HWMON_SYS_TZONE_NULL_ID)
            break;

        guarded_strscpy(reading->sensor[i].sensor_name, hwmon_sys_thermal_zone_id_map[hwmon_cfg->sys_thermal[i]],
                        sizeof(reading->sensor[i].sensor_name)); //Save the name of the sensor
        hwmon_thermals[i] = prandom_int_range_stable(&hwmon_thermals[i], TEMP_DEV, FAKE_SURFACE_TEMP_MIN,
                                                     FAKE_SURFACE_TEMP_MAX);
        snprintf(reading->sensor[i].value, sizeof(reading->sensor[i].value), "%d", hwmon_thermals[i]);
        ++reading->sensor_num;

        hwmon_pr_loc_dbg("mfgBIOS: <= %s() %s->%d °C", __FUNCTION__,
                         hwmon_sys_thermal_zone_id_map[hwmon_cfg->sys_thermal[i]], hwmon_thermals[i]);
    }

    return 0;
}

static int *hwmon_voltages = NULL;
/**
 * Returns various HWMON voltages
 *
 * @param reading Pointer to save results
 * @return 0 on success, -E on error
 */
static int bios_hwmon_get_voltages(SYNO_HWMON_SENSOR_TYPE *reading)
{
    guard_hwmon_cfg();
    if (unlikely(!hwmon_voltages))
        kzalloc_or_exit_int(hwmon_voltages, sizeof(int) * HWMON_SYS_VOLTAGE_SENSOR_IDS);

    guarded_strscpy(reading->type_name, HWMON_SYS_VOLTAGE_NAME, sizeof(reading->type_name));
    hwmon_pr_loc_dbg("mfgBIOS: => %s(type=%s)", __FUNCTION__, reading->type_name);

    for (int i = 0; i < HWMON_SYS_VOLTAGE_SENSOR_IDS; i++) {
        if (hwmon_cfg->sys_voltage[i] == HWMON_SYS_VSENS_NULL_ID)
            break;

        guarded_strscpy(reading->sensor[i].sensor_name, hwmon_sys_vsens_id_map[hwmon_cfg->sys_voltage[i]],
                        sizeof(reading->sensor[i].sensor_name)); //Save the name of the sensor
        hwmon_voltages[i] = prandom_int_range_stable(&hwmon_voltages[i], VOLT_DEV,
                                                     fake_volt_min(hwmon_cfg->sys_voltage[i]),
                                                     fake_volt_max(hwmon_cfg->sys_voltage[i]));
        snprintf(reading->sensor[i].value, sizeof(reading->sensor[i].value), "%d", hwmon_voltages[i]);
        ++reading->sensor_num;

        hwmon_pr_loc_dbg("mfgBIOS: <= %s() %s->%d mV", __FUNCTION__, hwmon_sys_vsens_id_map[hwmon_cfg->sys_voltage[i]],
                         hwmon_voltages[i]);
    }

    return 0;
}

static int *hwmon_fans_rpm = NULL;
/**
 * Returns HWMON fan speeds
 *
 * @param reading Pointer to save results
 * @return 0 on success, -E on error
 */
static int bios_hwmon_get_fans_rpm(SYNO_HWMON_SENSOR_TYPE *reading)
{
    guard_hwmon_cfg();
    if (unlikely(!hwmon_fans_rpm))
        kzalloc_or_exit_int(hwmon_fans_rpm, sizeof(int) * HWMON_SYS_FAN_RPM_IDS);

    guarded_strscpy(reading->type_name, HWMON_SYS_FAN_RPM_NAME, sizeof(reading->type_name));
    hwmon_pr_loc_dbg("mfgBIOS: => %s(type=%s)", __FUNCTION__, reading->type_name);

    for (int i = 0; i < HWMON_SYS_FAN_RPM_IDS; i++) {
        if (hwmon_cfg->sys_fan_speed_rpm[i] == HWMON_SYS_FAN_NULL_ID)
            break;

        guarded_strscpy(reading->sensor[i].sensor_name, hwmon_sys_fan_id_map[hwmon_cfg->sys_fan_speed_rpm[i]],
                        sizeof(reading->sensor[i].sensor_name)); //Save the name of the sensor
        hwmon_fans_rpm[i] = prandom_int_range_stable(&hwmon_fans_rpm[i], FAN_SPEED_DEV, FAKE_RPM_MIN, FAKE_RPM_MAX);
        snprintf(reading->sensor[i].value, sizeof(reading->sensor[i].value), "%d", hwmon_fans_rpm[i]);
        ++reading->sensor_num;

        hwmon_pr_loc_dbg("mfgBIOS: <= %s() %s->%d RPM", __FUNCTION__,
                         hwmon_sys_fan_id_map[hwmon_cfg->sys_fan_speed_rpm[i]], hwmon_fans_rpm[i]);
    }

    return 0;
}

/**
 * Returns HWMON disk backplane status
 *
 * Currently values here are just a guesstimation - we don't have a platform to see the real values but based on their
 * names it's assumed these are number of detected and enabled disks.
 * This probably should ask the SCSI driver for the number of disks overall (as no PC architecture has any clue about
 * number of disks present physically if they don't register with the system). On a real hardware it's probably checked
 * by some contact switch/IR sensor to check if a given slot for a disk isn't empty.
 *
 * @param reading Pointer to save results
 * @return 0 on success, -E on error
 */
static int bios_hwmon_get_hdd_backplane(SYNO_HWMON_SENSOR_TYPE *reading)
{
    guard_hwmon_cfg();
    const int hdd_num = 1; //todo: this should be taken from SCSI layer

    guarded_strscpy(reading->type_name, HWMON_HDD_BP_STATUS_NAME, sizeof(reading->type_name));
    hwmon_pr_loc_dbg("mfgBIOS: => %s(type=%s)", __FUNCTION__, reading->type_name);

    for (int i = 0; i < HWMON_SYS_HDD_BP_IDS; i++) {
        if (hwmon_cfg->hdd_backplane[i] == HWMON_SYS_HDD_BP_NULL_ID)
            break;

        guarded_strscpy(reading->sensor[i].sensor_name, hwmon_hdd_bp_id_map[hwmon_cfg->hdd_backplane[i]],
                        sizeof(reading->sensor[i].sensor_name)); //Save the name of the sensor
        snprintf(reading->sensor[i].value, sizeof(reading->sensor[i].value), "%d", hdd_num);
        ++reading->sensor_num;

        hwmon_pr_loc_dbg("mfgBIOS: <= %s() %s->%d", __FUNCTION__, hwmon_hdd_bp_id_map[hwmon_cfg->hdd_backplane[i]],
                         hdd_num);
    }

    return 0;
}

/**
 * (Should) Return HWMON power supplies status
 *
 * Currently this command is not implemented and always return an error as we haven't yet seen any devices using it.
 *
 * @param reading Pointer to save results
 * @return 0 on success, -E on error
 */
static int bios_hwmon_get_psu_status(struct hw_config_hwmon *hwc, SYNO_HWMON_SENSOR_TYPE *reading)
{
    pr_loc_wrn("mfgBIOS: **UNIMPLEMENTED** %s(type=%s)", __FUNCTION__, HWMON_PSU_STATUS_NAME);
    return -EIO; //todo: we haven't [yet] seen a device using this
}

/**
 * (Should) Return HWMON power consumption
 *
 * Currently this command is not implemented and always return an error as we haven't yet seen any devices using it.
 *
 * @param hwc Platform HWMON configuration
 * @param reading Pointer to save results
 *
 * @return 0 on success, -E on error
 */
static int bios_hwmon_get_current(struct hw_config_hwmon *hwc, SYNO_HWMON_SENSOR_TYPE *reading)
{
    pr_loc_wrn("mfgBIOS: **UNIMPLEMENTED** %s(type=%s)", __FUNCTION__, HWMON_SYS_CURRENT_NAME);
    return -EIO; //todo: we haven't [yet] seen a device using this
}


/************************************************ mfgBIOS shim interface **********************************************/
int shim_bios_module_hwmon_entries(const struct hw_config *hw)
{
    shim_reg_in();
    hwmon_cfg = &hw->hwmon;

    _shim_bios_module_entry(VTK_GET_FAN_STATE, bios_get_fan_state);

    if (hw->has_cpu_temp)
        _shim_bios_module_entry(VTK_GET_CPU_TEMP, bios_get_cpu_temp);

    if (platform_has_hwmon_thermal(hw))
        _shim_bios_module_entry(VTK_GET_HWMON_THERMAL, bios_hwmon_get_thermal);
    
    if (platform_has_hwmon_voltage(hw))
        _shim_bios_module_entry(VTK_GET_HWMON_VOLTAGE, bios_hwmon_get_voltages);
        
    if (platform_has_hwmon_fan_rpm(hw))
        _shim_bios_module_entry(VTK_GET_HWMON_FAN_RPM, bios_hwmon_get_fans_rpm);
    
    if (platform_has_hwmon_hdd_bpl(hw))
        _shim_bios_module_entry(VTK_GET_HWMON_HDD_BKPLANE, bios_hwmon_get_hdd_backplane);
    
    if (platform_has_hwmon_psu_status(hw))
        _shim_bios_module_entry(VTK_GET_HWMON_PSU_STATUS, bios_hwmon_get_psu_status);
    
    if (platform_has_hwmon_current_sens(hw))
        _shim_bios_module_entry(VTK_GET_HWMON_CURRENT, bios_hwmon_get_current);
    
    shim_reg_ok();
    return 0;
}

int reset_bios_module_hwmon_shim(void)
{
    shim_reset_in();

    hwmon_cfg = NULL;
    cur_cpu_temp = 0;
    try_kfree(hwmon_thermals);
    try_kfree(hwmon_voltages);
    try_kfree(hwmon_fans_rpm);

    shim_reset_ok();
    return 0;
}