/*
 * Proxy between an ACPI RTC and mfgBIOS calls
 *
 * Some platforms don't use a standard RTC chip but implement a custom platform-specific one. To handle different chips
 * mfgBIOS uses a standardized interface. This works perfectly fine when mfgBIOS expects an ACPI-complaint RTC to be
 * present. However, it does not work when a given platform is expected to contain some 3rd-party I2C clock chip.
 *
 * Motorola MC146818 was a de facto standard RTC chip when PC/AT emerged. Later on other clones started emulating the
 * interface. This become so prevalent that ACPI standardized the basic interface of RTC on PC-compatibile systems as
 * MC146818 interface. Thus, this module assumes that mfgBIOS calls can be proxied to MC146818 interface (which will
 * work on any ACPI-complaint system and any sane hypervisor).
 *
 * As some of the functions are rarely used (and often even completely broken on many systems), like RTC wakeup they're
 * not really implemented but instead mocked to look "just good enough".
 *
 * References:
 *  - https://www.kernel.org/doc/html/latest/admin-guide/rtc.html
 *  - https://embedded.fm/blog/2018/6/5/an-introduction-to-bcd
 */
#include "../../common.h"
#include "rtc_proxy.h"
#include "../shim_base.h" //shim_*()
#include <linux/mc146818rtc.h>
#include <linux/bcd.h>

#define SHIM_NAME "RTC proxy"

//Confused? See https://slate.com/technology/2016/02/the-math-behind-leap-years.html
#define year_is_leap(year) !((year)%((year)%25?4:16))
#define mfg_year_to_full(val) ((val)+1900) //MfgCompatTime counts years as offset from 1900
#define mfg_month_to_normal(val) ((val)+1) //MfgCompatTime has 0-based months
#define normal_month_to_mfg(val) ((val)-1)
static const unsigned char months_to_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static struct MfgCompatAutoPwrOn *auto_power_on_mock = NULL;

inline static void debug_print_mfg_time(struct MfgCompatTime *mfgTime)
{
    pr_loc_dbg("MfgCompatTime raw data: sec=%u min=%u hr=%u wkd=%u day=%u mth=%u yr=%u", mfgTime->second,
               mfgTime->minute, mfgTime->hours, mfgTime->wkday, mfgTime->day, mfgTime->month, mfgTime->year);
}

/**
 * Standardizes & abstracts RTC reading
 *
 * Reading & writing RTC requires conversion of values based on some registers and chips. This function simply accept
 * pointers to YY-MM-DD WeekDay HHmmss values and does all the conversions for you after reading.
 */
static void read_rtc_num(unsigned char *yy, unsigned char *mm, unsigned char *dd, unsigned char *wd, unsigned char *hr,
                         unsigned char *mi, unsigned char *ss)
{
    //As the clock uses IRQ 8 normally we need to atomically stop it to read all values and restore it later
    unsigned long flags;
    spin_lock_irqsave(&rtc_lock, flags);
    const unsigned char rtc_control = CMOS_READ(RTC_CONTROL);

    //There are two formats how RTCs can report time: normal numbers or an ancient BCD. Currently (at least in Linux v4)
    //BCD is always used for MC146818 (but this can change). This we need to handle both cases.
    if (likely(RTC_ALWAYS_BCD) || (rtc_control & RTC_DM_BINARY)) { //a common idiom, search for RTC_ALWAYS_BCD in kernel
        pr_loc_dbg("Reading BCD-based RTC");
        *yy = bcd2bin(CMOS_READ(RTC_YEAR));
        *mm = bcd2bin(CMOS_READ(RTC_MONTH));
        *dd = bcd2bin(CMOS_READ(RTC_DAY_OF_MONTH));
        *wd = bcd2bin(CMOS_READ(RTC_DAY_OF_WEEK));
        *hr = bcd2bin(CMOS_READ(RTC_HOURS));
        *mi = bcd2bin(CMOS_READ(RTC_MINUTES));
        *ss = bcd2bin(CMOS_READ(RTC_SECONDS));
    } else {
        pr_loc_dbg("Reading binary-based RTC");
        *yy = CMOS_READ(RTC_YEAR);
        *mm = CMOS_READ(RTC_MONTH);
        *dd = CMOS_READ(RTC_DAY_OF_MONTH);
        *wd = CMOS_READ(RTC_DAY_OF_WEEK);
        *hr = CMOS_READ(RTC_HOURS);
        *mi = CMOS_READ(RTC_MINUTES);
        *ss = CMOS_READ(RTC_SECONDS);
    }
    spin_unlock_irqrestore(&rtc_lock, flags);
}

/**
 * Standardizes & abstracts RTC time setting
 *
 * Reading & writing RTC requires conversion of values based on some registers and chips. This function simply accepts
 * values to be set in YY-MM-DD WeekDay HHmmss format and does all the conversions/locking/freq resets for you.
 */
static void write_rtc_num(unsigned char yy, unsigned char mm, unsigned char dd, unsigned char wd, unsigned char hr,
                          unsigned char mi, unsigned char ss)
{
    unsigned long flags;
    spin_lock_irqsave(&rtc_lock, flags);
    unsigned char rtc_control = CMOS_READ(RTC_CONTROL); //RTC control register value locked for us
    unsigned char rtc_freq_tick = CMOS_READ(RTC_FREQ_SELECT);
    CMOS_WRITE((rtc_control|RTC_SET), RTC_CONTROL); //enter clock setting state
    CMOS_WRITE(rtc_freq_tick|RTC_DIV_RESET2, RTC_FREQ_SELECT); //this should reset the ticks

    if (likely(RTC_ALWAYS_BCD) || (rtc_control & RTC_DM_BINARY)) { //a common idiom, search for RTC_ALWAYS_BCD in kernel
        pr_loc_dbg("Writing BCD-based RTC");
        CMOS_WRITE(bin2bcd(yy), RTC_YEAR);
        CMOS_WRITE(bin2bcd(mm), RTC_MONTH);
        CMOS_WRITE(bin2bcd(dd), RTC_DAY_OF_MONTH);
        CMOS_WRITE(bin2bcd(wd), RTC_DAY_OF_WEEK);
        CMOS_WRITE(bin2bcd(hr), RTC_HOURS);
        CMOS_WRITE(bin2bcd(mi), RTC_MINUTES);
        CMOS_WRITE(bin2bcd(ss), RTC_SECONDS);
    } else {
        pr_loc_dbg("Writing binary-based RTC");
        CMOS_WRITE(yy, RTC_YEAR);
        CMOS_WRITE(mm, RTC_MONTH);
        CMOS_WRITE(dd, RTC_DAY_OF_MONTH);
        CMOS_WRITE(wd, RTC_DAY_OF_WEEK);
        CMOS_WRITE(hr, RTC_HOURS);
        CMOS_WRITE(mi, RTC_MINUTES);
        CMOS_WRITE(ss, RTC_SECONDS);
    }

    CMOS_WRITE(rtc_control, RTC_CONTROL); //restore original control register
    CMOS_WRITE(rtc_freq_tick, RTC_FREQ_SELECT); //...and the ticks too
    spin_unlock_irqrestore(&rtc_lock, flags);
}

int rtc_proxy_get_time(struct MfgCompatTime *mfgTime)
{
    if (mfgTime == NULL) {
        pr_loc_wrn("Got an invalid call to %s", __FUNCTION__);
        return -EPERM;
    }

    debug_print_mfg_time(mfgTime);

    unsigned char rtc_year; //mfgTime uses offset from 1900 while RTC uses 2-digit format (see below)
    unsigned char rtc_month; //mfgTime uses 0-11 while RTC uses 1-12
    read_rtc_num(&rtc_year, &rtc_month, &mfgTime->day, &mfgTime->wkday, &mfgTime->hours, &mfgTime->minute,
                 &mfgTime->second);

    //So yeah, it's 2021 and PC RTCs still use 2 digit year so we have to do a classic Y2K hack with epoch
    //RTC nowadays is assumed to have a range of 1970-2069 which forces two assumptions:
    // - Values  0-69 indicate 2000-2069
    // - Values 70-99 indicate 1970-1999
    //As the mfgTime->year uses value of years since 1900 without magic rollovers we need to correct it by 100 for the
    //2000s epoch. Search for e.g. "mc146818_decode_year" in Linux, that's (sadly) a common method.
    mfgTime->year = (likely(rtc_year < 70)) ? rtc_year + 100 : rtc_year;
    mfgTime->month = normal_month_to_mfg(rtc_month);

    pr_loc_inf("Time got from RTC is %4d-%02d-%02d %2d:%02d:%02d (UTC)", mfg_year_to_full(mfgTime->year),
               mfg_month_to_normal(mfgTime->month), mfgTime->day, mfgTime->hours, mfgTime->minute, mfgTime->second);
    debug_print_mfg_time(mfgTime);

    return 0;
}

int rtc_proxy_set_time(struct MfgCompatTime *mfgTime)
{
    if (mfgTime == NULL) {
        pr_loc_wrn("Got an invalid call to %s", __FUNCTION__);
        return -EPERM;
    }

    debug_print_mfg_time(mfgTime);

    //Ok, this is PROBABLY not needed but we don't want to crash the RTC if an invalid value is passed to this function
    //Also, we are aware of leap seconds but do you think 1984 hardware is? (spoiler alert: no)
    if (unlikely(mfgTime->second > 59 || mfgTime->minute > 59 || mfgTime->hours > 24 || mfgTime->wkday > 6 ||
                  mfgTime->day == 0 || mfgTime->month > 11)) {
        pr_loc_wrn("Got invalid generic RTC data in %s", __FUNCTION__);
        return -EINVAL;
    }

    //Year validation needs to take leap years into account. This code can be shorter but it's expended for readability
    if (unlikely(mfgTime->month == 1 && year_is_leap(mfgTime->year))) {
        if (mfgTime->day > (months_to_days[mfgTime->month] + 1)) {
            pr_loc_wrn("Invalid RTC leap year day (%u > %u) of month %u in %s", mfgTime->day,
                       (months_to_days[mfgTime->month] + 1), mfgTime->month, __FUNCTION__);
            return -EINVAL;
        }
    } else if (mfgTime->day > months_to_days[mfgTime->month]) {
        pr_loc_wrn("Invalid RTC regular year day (%u > %u) of month %u in %s", mfgTime->day,
                   months_to_days[mfgTime->month], mfgTime->month, __FUNCTION__);
        return -EINVAL;
    }

    //mfgTime->year uses a positive offset since 1900. However, ACPI-complain RTC cannot handle range higher than
    //1970-2069 (see comment in rtc_proxy_get_time()).
    unsigned char rtc_year = mfgTime->year; //mfgTime uses offset from 1900 while RTC uses 2-digit format (see below)
    if (unlikely(rtc_year > 169)) { //This cannot be valid as RTC cannot handle >2069
        pr_loc_wrn("Year overflow in %s", __FUNCTION__);
        return -EINVAL;
    } else if(likely(rtc_year > 100)) {
        rtc_year -= 100; //RTC uses 0-69 for 2000s so we need to shift mfgTime 1900-now offset by 100
    }
    
    unsigned char rtc_month = mfg_month_to_normal(mfgTime->month); //mfgTime uses 0-11 while RTC uses 1-12
    
    write_rtc_num(rtc_year, rtc_month, mfgTime->day, mfgTime->wkday, mfgTime->hours, mfgTime->minute, mfgTime->second);

    pr_loc_inf("RTC time set to %4d-%02d-%02d %2d:%02d:%02d (UTC)", mfg_year_to_full(mfgTime->year),
               mfg_month_to_normal(mfgTime->month), mfgTime->day, mfgTime->hours, mfgTime->minute, mfgTime->second);

    return 0;
}

int rtc_proxy_init_auto_power_on(void)
{
    pr_loc_dbg("RTC power-on \"enabled\" via %s", __FUNCTION__);

    return 0;
}

int rtc_proxy_get_auto_power_on(struct MfgCompatAutoPwrOn *mfgPwrOn)
{
    if (unlikely(!auto_power_on_mock)) {
        pr_loc_bug("Auto power-on mock is not initialized - did you forget to call register?");
        return -EINVAL;
    }

    pr_loc_dbg("Mocking auto-power GET on RTC");
    memcpy(mfgPwrOn, auto_power_on_mock, sizeof(struct MfgCompatAutoPwrOn));

    return 0;
}

int rtc_proxy_set_auto_power_on(struct MfgCompatAutoPwrOn *mfgPwrOn)
{
    if (!mfgPwrOn || mfgPwrOn->num < 0) { //That's just either a bogus call or a stupid call
        pr_loc_wrn("Got an invalid call to %s", __FUNCTION__);
        return -EINVAL;
    }

    pr_loc_dbg("Mocking auto-power SET on RTC");
    memcpy(auto_power_on_mock, mfgPwrOn, sizeof(struct MfgCompatAutoPwrOn));

    return 0;
}

int rtc_proxy_uinit_auto_power_on(void)
{
    pr_loc_dbg("RTC power-on \"disabled\" via %s", __FUNCTION__);

    return 0;
}

int unregister_rtc_proxy_shim(void)
{
    shim_ureg_in();

    //This is not an error as bios shim collections calls unregister blindly
    if (!auto_power_on_mock) {
        pr_loc_dbg("The %s shim is not registered - ignoring", SHIM_NAME);
        return 0;
    }

    kfree(auto_power_on_mock);
    auto_power_on_mock = NULL;
    shim_ureg_ok();
    return 0;
}

int register_rtc_proxy_shim(void)
{
    shim_reg_in();

    if (unlikely(auto_power_on_mock)) {
        pr_loc_wrn("The %s shim is already registered - unregistering first", SHIM_NAME);
        unregister_rtc_proxy_shim();
    }

    kzalloc_or_exit_int(auto_power_on_mock, sizeof(struct MfgCompatAutoPwrOn));
    shim_reg_ok();
    return 0;
}
