/*****************************************************************************
 *
 * Filename:
 * ---------
 *    battery_common.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of mt6323 Battery charging algorithm
 *   and the Anroid Battery service for updating the battery status
 *
 * Author:
 * -------
 * Oscar Liu
 *
 ****************************************************************************/
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif
#include <linux/suspend.h>

#include <asm/scatterlist.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/system.h>
#include <mach/mt_sleep.h>

#include <mach/mt_typedefs.h>
#include <mach/mt_gpt.h>
#include <mach/mt_boot.h>

#include <cust_charging.h>
#include <mach/upmu_common.h>
#include <mach/upmu_hw.h>
#include <mach/charging.h>
#include <mach/battery_common.h>
#include <mach/battery_meter.h>
#include "cust_battery_meter.h"
#include "cust_charging.h"
#include <mach/mt_boot.h>
#include "mach/mtk_rtc.h"

#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
#include <mach/diso.h>
#endif

#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
#include "cust_pe.h"
#endif

#ifdef CONFIG_AMAZON_METRICS_LOG

#if defined(CONFIG_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include <linux/metricslog.h>

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

enum BQ_FLAGS {
	BQ_STATUS_RESUMING = 0x2,
};

struct battery_info {
	struct mutex lock;

	int flags;

	/* Time when system enters full suspend */
	struct timespec suspend_time;
	/* Time when system enters early suspend */
	struct timespec early_suspend_time;
	/* Battery capacity when system enters full suspend */
	int suspend_capacity;
	/* Battery capacity when system enters early suspend */
	int early_suspend_capacity;
#if defined(CONFIG_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
};

struct battery_info BQ_info;

/* set max time interval for metric to 10 years*/
#define MAX_TIME_INTERVAL	(3600*24*365*10)
#endif

/* ////////////////////////////////////////////////////////////////////////////// */
/* Battery Logging Entry */
/* ////////////////////////////////////////////////////////////////////////////// */
int Enable_BATDRV_LOG = BAT_LOG_CRTI;

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Smart Battery Structure */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
PMU_ChargerStruct BMT_status;
#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
DISO_ChargerStruct DISO_data;
/* Debug Msg */
static char *DISO_state_s[8] = {
	"IDLE",
	"OTG_ONLY",
	"USB_ONLY",
	"USB_WITH_OTG",
	"DC_ONLY",
	"DC_WITH_OTG",
	"DC_WITH_USB",
	"DC_USB_OTG",
};
#endif

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Thermal related flags */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
#ifdef CONFIG_AUSTIN_PROJECT
int g_battery_thermal_throttling_flag = 1;	/* 0:nothing, 1:enable batTT&chrTimer, 2:disable batTT&chrTimer, 3:enable batTT, disable chrTimer */
#else
int g_battery_thermal_throttling_flag = 3;	/* 0:nothing, 1:enable batTT&chrTimer, 2:disable batTT&chrTimer, 3:enable batTT, disable chrTimer */
#endif

int battery_cmd_thermal_test_mode = 0;
int battery_cmd_thermal_test_mode_value = 0;
int g_battery_tt_check_flag = 0;	/* 0:default enable check batteryTT, 1:default disable check batteryTT */


/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Global Variable */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
struct wake_lock battery_suspend_lock;
CHARGING_CONTROL battery_charging_control;
unsigned int g_BatteryNotifyCode = 0x0000;
unsigned int g_BN_TestMode = 0x0000;
kal_bool g_bat_init_flag = 0;
unsigned int g_call_state = CALL_IDLE;
kal_bool g_charging_full_reset_bat_meter = KAL_FALSE;
int g_platform_boot_mode = 0;
struct timespec g_bat_time_before_sleep;
int g_smartbook_update = 0;
signed int g_custom_charging_current = -1;

#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
kal_bool g_vcdt_irq_delay_flag = 0;
#endif

#if defined(MTK_TEMPERATURE_RECHARGE_SUPPORT)
kal_uint32 g_batt_temp_status = TEMP_POS_NORMAL;
#endif

kal_bool battery_suspended = KAL_FALSE;
kal_bool g_refresh_ui_soc = KAL_FALSE;
#ifdef MTK_ENABLE_AGING_ALGORITHM
extern U32 suspend_time;
#endif

#if defined(CUST_SYSTEM_OFF_VOLTAGE)
#define SYSTEM_OFF_VOLTAGE CUST_SYSTEM_OFF_VOLTAGE
#endif

#if defined(CONFIG_AUSTIN_PROJECT)
int battery_idV = 0;
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int* rawdata);

unsigned int g_custom_charging_mode = 0; /* 0=ratail unit, 1=demo unit */
#endif

#define PLUGIN_THRESHOLD (14*86400)
signed int g_custom_charging_cv = -1;
struct timespec chr_plug_in_time;
static unsigned long g_custom_plugin_time;

extern int orderly_poweroff(bool force);

/* ////////////////////////////////////////////////////////////////////////////// */
/* Integrate with NVRAM */
/* ////////////////////////////////////////////////////////////////////////////// */
#define ADC_CALI_DEVNAME "MT_pmic_adc_cali"
#define TEST_ADC_CALI_PRINT _IO('k', 0)
#define SET_ADC_CALI_Slop _IOW('k', 1, int)
#define SET_ADC_CALI_Offset _IOW('k', 2, int)
#define SET_ADC_CALI_Cal _IOW('k', 3, int)
#define ADC_CHANNEL_READ _IOW('k', 4, int)
#define BAT_STATUS_READ _IOW('k', 5, int)
#define Set_Charger_Current _IOW('k', 6, int)
/* add for meta tool----------------------------------------- */
#define Get_META_BAT_VOL _IOW('k', 10, int)
#define Get_META_BAT_SOC _IOW('k', 11, int)
/* add for meta tool----------------------------------------- */

static struct class *adc_cali_class;
static int adc_cali_major;
static dev_t adc_cali_devno;
static struct cdev *adc_cali_cdev;

int adc_cali_slop[14] =
    { 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000 };
int adc_cali_offset[14] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int adc_cali_cal[1] = { 0 };
int battery_in_data[1] = { 0 };
int battery_out_data[1] = { 0 };
int charging_level_data[1] = { 0 };

kal_bool g_ADC_Cali = KAL_FALSE;
kal_bool g_ftm_battery_flag = KAL_FALSE;
#if !defined(CONFIG_POWER_EXT)
static int g_wireless_state;
#endif

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Thread related */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
#define BAT_MS_TO_NS(x) (x * 1000 * 1000)
static kal_bool bat_thread_timeout = KAL_FALSE;
static kal_bool chr_wake_up_bat = KAL_FALSE;	/* charger in/out to wake up battery thread */
static kal_bool bat_meter_timeout = KAL_FALSE;
static DEFINE_MUTEX(bat_mutex);
static DEFINE_MUTEX(charger_type_mutex);
static DECLARE_WAIT_QUEUE_HEAD(bat_thread_wq);
static struct hrtimer charger_hv_detect_timer;
static struct task_struct *charger_hv_detect_thread = NULL;
static kal_bool charger_hv_detect_flag = KAL_FALSE;
static DECLARE_WAIT_QUEUE_HEAD(charger_hv_detect_waiter);
static struct hrtimer battery_kthread_timer;
static kal_bool g_battery_soc_ready = KAL_FALSE;
extern BOOL bat_spm_timeout;
extern U32 _g_bat_sleep_total_time;

/* ////////////////////////////////////////////////////////////////////////////// */
/* FOR ADB CMD */
/* ////////////////////////////////////////////////////////////////////////////// */
/* Dual battery */
int g_status_smb = POWER_SUPPLY_STATUS_NOT_CHARGING;
int g_capacity_smb = 50;
int g_present_smb = 0;
/* ADB charging CMD */
static int cmd_discharging = -1;
static int adjust_power = -1;
static int suspend_discharging = -1;

#if defined(CONFIG_AMAZON_METRICS_LOG) && defined(CONFIG_AUSTIN_PROJECT)

struct metrics_charge {
	struct timespec charger_time;

	int battery_charge_ticks;
	unsigned int init_charging_vol;

	unsigned long battery_peak_virtual_temp;
	unsigned long battery_peak_battery_temp;
	unsigned long battery_average_virtual_temp;
	unsigned long battery_average_battery_temp;
	unsigned long usb_charger_voltage_peak;
};

static struct metrics_charge metrics_charge_info;

struct metrics_capacity {
	struct timespec above_95_time;
	struct timespec below_15_time;

	bool bat_15_flag;
	bool bat_95_flag;
	bool battery_below_15_fired;

	int low_battery_initial_soc;
	unsigned int low_battery_initial_voltage;
	int battery_low_ticks;
};
extern unsigned long get_virtualsensor_temp(void);
extern signed int gFG_battery_cycle;
#endif

/* ////////////////////////////////////////////////////////////////////////////// */
/* FOR ANDROID BATTERY SERVICE */
/* ////////////////////////////////////////////////////////////////////////////// */

struct wireless_data {
	struct power_supply psy;
	int WIRELESS_ONLINE;
};

struct ac_data {
	struct power_supply psy;
	int AC_ONLINE;
};

struct usb_data {
	struct power_supply psy;
	int USB_ONLINE;
};

struct battery_data {
	struct power_supply psy;
	int BAT_STATUS;
	int BAT_HEALTH;
	int BAT_PRESENT;
	int BAT_TECHNOLOGY;
	int BAT_CAPACITY;
	/* Add for Battery Service */
	int BAT_VOLTAGE_NOW;
	int BAT_VOLTAGE_AVG;
	int CONSTANT_CHARGE_CURRENT;
	int BAT_batt_vol;
	int BAT_TEMP;
	/* Add for EM */
	int BAT_TemperatureR;
	int BAT_TempBattVoltage;
	int BAT_InstatVolt;
	int BAT_BatteryAverageCurrent;
	int BAT_BatterySenseVoltage;
	int BAT_ISenseVoltage;
	int BAT_ChargerVoltage;
	/* Dual battery */
	int status_smb;
	int capacity_smb;
	int present_smb;
	int adjust_power;
	/* ACOS_MOD_BEGIN {metrics_log} */
	int BAT_SuspendDrain;
	int BAT_SuspendRealtime;
	/* ACOS_MOD_END {metrics_log} */
};

static struct battery_data battery_main;

static enum power_supply_property wireless_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	/* Add for Battery Service */
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_batt_vol,
	POWER_SUPPLY_PROP_TEMP,
	/* Add for EM */
	POWER_SUPPLY_PROP_TemperatureR,
	POWER_SUPPLY_PROP_TempBattVoltage,
	POWER_SUPPLY_PROP_InstatVolt,
	POWER_SUPPLY_PROP_BatteryAverageCurrent,
	POWER_SUPPLY_PROP_BatterySenseVoltage,
	POWER_SUPPLY_PROP_ISenseVoltage,
	POWER_SUPPLY_PROP_ChargerVoltage,
	/* Dual battery */
	POWER_SUPPLY_PROP_status_smb,
	POWER_SUPPLY_PROP_capacity_smb,
	POWER_SUPPLY_PROP_present_smb,
	/* ADB CMD Discharging */
	POWER_SUPPLY_PROP_adjust_power,
	/* ACOS_MOD_BEGIN {metrics_log} */
	POWER_SUPPLY_PROP_SUSPEND_DRAIN,
	POWER_SUPPLY_PROP_SUSPEND_REALTIME,
	/* ACOS_MOD_END {metrics_log} */
};

#ifdef CONFIG_AUSTIN_PROJECT
#define SHOW_CHARGE_IC_VENDOR
static char *charge_ic_vendor_name = NULL;
static char *battery_vendor_name = NULL;
extern kal_uint32 g_fg_battery_id;
#endif

#if defined(CONFIG_AMAZON_METRICS_LOG) && defined(CONFIG_AUSTIN_PROJECT)
void metrics_battery_save_data(void)
{
	unsigned long temp = get_virtualsensor_temp();

	/* Save Data for Next Metric */
	metrics_charge_info.init_charging_vol = BMT_status.bat_vol;

	/* Set Peak temperatures to current temperature */
	metrics_charge_info.battery_peak_battery_temp = BMT_status.temperature;
	metrics_charge_info.battery_peak_virtual_temp = get_virtualsensor_temp();

	/* Save Current USB Charging Voltage as Peak */
	metrics_charge_info.usb_charger_voltage_peak = BMT_status.charger_vol;

	metrics_charge_info.battery_average_virtual_temp = temp;
	metrics_charge_info.battery_average_battery_temp = BMT_status.temperature;

	/* Set Charging Ticks to 1 */
	metrics_charge_info.battery_charge_ticks = 1;

	/* Set Time start for Next Metric */
	metrics_charge_info.charger_time = current_kernel_time();
}

static void battery_capacity_check(void)
{
	char buf[256];
	long elaps_sec;
	struct timespec diff;
	struct timespec curr;
	static struct metrics_capacity metrics_cap_info;

	if (BMT_status.SOC > 95 && !metrics_cap_info.bat_95_flag) {
		metrics_cap_info.bat_95_flag = true;
		metrics_cap_info.above_95_time = current_kernel_time();
	}

	if (BMT_status.SOC < 95 && metrics_cap_info.bat_95_flag) {
		metrics_cap_info.bat_95_flag = false;
		curr = current_kernel_time();
		diff = timespec_sub(curr, metrics_cap_info.above_95_time);
		elaps_sec = diff.tv_sec + diff.tv_nsec / NSEC_PER_SEC;

		if (elaps_sec > 0 && elaps_sec < MAX_TIME_INTERVAL) {
			snprintf(buf,sizeof(buf),
					"bq24296:def:time_soc95=1;CT;1,Elaps_Sec=%ld;CT;1:NA", elaps_sec);

			log_to_metrics(ANDROID_LOG_INFO, "battery", buf);
			memset(buf, 0, sizeof(buf));
		}
	}

	if (BMT_status.SOC < 15 && !metrics_cap_info.bat_15_flag) {
		metrics_cap_info.bat_15_flag = true;
		metrics_cap_info.below_15_time = current_kernel_time();
		metrics_cap_info.battery_low_ticks = 0;
		metrics_cap_info.low_battery_initial_voltage = BMT_status.bat_vol;
		metrics_cap_info.low_battery_initial_soc = BMT_status.SOC;
	} else if (BMT_status.SOC < 15) {
		/* Count Tickes for case when system
		* starts up from dead battery
		*/
		metrics_cap_info.battery_low_ticks++;
	}

	if (BMT_status.SOC > 15 && metrics_cap_info.bat_15_flag) {
		metrics_cap_info.bat_15_flag = false;
		metrics_cap_info.battery_below_15_fired = false;
		curr = current_kernel_time();
		diff = timespec_sub(curr, metrics_cap_info.below_15_time);

		elaps_sec = diff.tv_sec + diff.tv_nsec / NSEC_PER_SEC;

		/* If system clock changed drastically use ticks instead.
		* If not clock is probably more accurate
		*/
		if (elaps_sec > metrics_cap_info.battery_low_ticks * 20)
			elaps_sec = metrics_cap_info.battery_low_ticks * 10;

		if (elaps_sec > 0 && elaps_sec < MAX_TIME_INTERVAL) {
			snprintf(buf,sizeof(buf),
					"bq24296:def:time_soc15_soc20=1;CT;1,Init_Vol=%u;CT;1,Init_SOC=%d;CT;1,Elaps_Sec=%ld;CT;1:NA",
					metrics_cap_info.low_battery_initial_voltage, metrics_cap_info.low_battery_initial_soc, elaps_sec);


			log_to_metrics(ANDROID_LOG_INFO, "battery", buf);
			memset(buf, 0, sizeof(buf));
		}
	}

	/* Catch Events for devices that may power off */
	if (BMT_status.SOC <= 2 && metrics_cap_info.battery_below_15_fired == false)
	{
		metrics_cap_info.battery_below_15_fired = true;
		diff = timespec_sub(current_kernel_time(), metrics_cap_info.below_15_time);
		elaps_sec = diff.tv_sec + diff.tv_nsec/NSEC_PER_SEC;
		if (elaps_sec > 0 && elaps_sec < MAX_TIME_INTERVAL) {
			snprintf(buf,sizeof(buf),
					"bq24296:def:time_soc15_soc0=1;CT;1,Init_Vol=%u;CT;1,Init_SOC=%d;CT;1,Elaps_Sec=%ld;CT;1:NA",
					metrics_cap_info.low_battery_initial_voltage, metrics_cap_info.low_battery_initial_soc, elaps_sec);

			log_to_metrics(ANDROID_LOG_INFO, "battery", buf);
			memset(buf, 0, sizeof(buf));
		}
	}
}
#endif

#if defined(CONFIG_AMAZON_METRICS_LOG)
void metrics_charger(bool connect)
{
	int cap = BMT_status.UI_SOC;
	char buf[128];

	if (connect == true) {
		snprintf(buf, sizeof(buf),
			"bq24296:def:POWER_STATUS_CHARGING=1;CT;1,"
			"cap=%u;CT;1:NR", cap);

		log_to_metrics(ANDROID_LOG_INFO, "battery", buf);
	} else {
		snprintf(buf, sizeof(buf),
			"bq24296:def:POWER_STATUS_DISCHARGING=1;CT;1,"
			"cap=%u;CT;1:NR", cap);

		log_to_metrics(ANDROID_LOG_INFO, "battery", buf);
	}

}

void metrics_charger_update(int ac, int usb)
{
	static bool ischargeronline;
	int onceonline = 0;

	if (ac == 1)
		onceonline = 1;
	else if (usb == 1)
		onceonline = 1;
	if (ischargeronline != onceonline) {
		ischargeronline = onceonline;
		metrics_charger(ischargeronline);
	}
}

static void battery_critical_voltage_check(void)
{
	static bool written;

	if (BMT_status.charger_exist == KAL_TRUE)
		return;
	if (BMT_status.bat_exist != KAL_TRUE)
		return;
	if (BMT_status.UI_SOC != 0)
		return;
	if (written == false && BMT_status.bat_vol <= SYSTEM_OFF_VOLTAGE) {
		written = true;

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
		life_cycle_set_special_mode(LIFE_CYCLE_SMODE_LOW_BATTERY);
#endif
	}
}

static void battery_metrics_locked(struct battery_info *info);

static void metrics_handle(void)
{
	struct battery_info *info = &BQ_info;

	mutex_lock(&info->lock);

	/* Check for critical battery voltage */
	battery_critical_voltage_check();

	if ((info->flags & BQ_STATUS_RESUMING)) {
		info->flags &= ~BQ_STATUS_RESUMING;
		battery_metrics_locked(info);
	}

	mutex_unlock(&info->lock);

#if defined(CONFIG_AUSTIN_PROJECT)
	battery_capacity_check();
#endif

	return;
}

#if defined(CONFIG_EARLYSUSPEND)
static void bq_log_metrics(struct battery_info *info, char *msg,
		char *metricsmsg)
{
	int value = BMT_status.UI_SOC;
	struct timespec curr = current_kernel_time();
	/* Compute elapsed time and determine screen off or on drainage */
	struct timespec diff = timespec_sub(curr,
			info->early_suspend_time);
	if (diff.tv_sec > 0 && diff.tv_sec < MAX_TIME_INTERVAL) {
		if (info->early_suspend_capacity != -1) {
			char buf[512];
			snprintf(buf, sizeof(buf),
				"%s:def:value=%d;CT;1,elapsed=%ld;TI;1:NR",
				metricsmsg,
				info->early_suspend_capacity - value,
				diff.tv_sec * 1000 + diff.tv_nsec / NSEC_PER_MSEC);
			log_to_metrics(ANDROID_LOG_INFO, "drain_metrics", buf);
		}
	}
	/* Cache the current capacity */
	info->early_suspend_capacity = BMT_status.UI_SOC;
	/* Mark the suspend or resume time */
	info->early_suspend_time = curr;
}

static void battery_early_suspend(struct early_suspend *handler)
{
	struct battery_info *info = &BQ_info;

	bq_log_metrics(info, "Screen on drainage", "screen_on_drain");
}

static void battery_late_resume(struct early_suspend *handler)
{
	struct battery_info *info = &BQ_info;

	bq_log_metrics(info, "Screen off drainage", "screen_off_drain");
}
#endif

static int metrics_init(void)
{
	struct battery_info *info = &BQ_info;

	mutex_init(&info->lock);

	info->suspend_capacity = -1;
	info->early_suspend_capacity = -1;

#if defined(CONFIG_EARLYSUSPEND)
	info->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	info->early_suspend.suspend = battery_early_suspend;
	info->early_suspend.resume = battery_late_resume;
	register_early_suspend(&info->early_suspend);
#endif

	mutex_lock(&info->lock);
	info->flags = 0;
	mutex_unlock(&info->lock);
	return 0;
}

static void metrics_uninit(void)
{
	struct battery_info *info = &BQ_info;

	unregister_early_suspend(&info->early_suspend);
}

static void metrics_suspend(void)
{
	struct battery_info *info = &BQ_info;

	/* Cache the current capacity */
	info->suspend_capacity = BMT_status.UI_SOC;
	battery_critical_voltage_check();
	/* Mark the suspend time */
	info->suspend_time = current_kernel_time();
}
#endif



/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // extern function */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* extern void mt_power_off(void); */
extern bool mt_usb_is_device(void);
#if defined(CONFIG_USB_MTK_HDRC) || defined(CONFIG_USB_MU3D_DRV)
extern void mt_usb_connect(void);
extern void mt_usb_disconnect(void);
#else
#define mt_usb_connect() do { } while (0)
#define mt_usb_disconnect() do { } while (0)
#endif
/* extern int set_rtc_spare_fg_value(int val); */

void check_battery_exist(void);
void charging_suspend_enable(void)
{
    U32 charging_enable = true;

    suspend_discharging = 0;
    battery_charging_control(CHARGING_CMD_ENABLE,&charging_enable);
}

void charging_suspend_disable(void)
{
    U32 charging_enable = false;

    suspend_discharging = 1;
    battery_charging_control(CHARGING_CMD_ENABLE,&charging_enable);
}

int read_tbat_value(void)
{
	return BMT_status.temperature;
}

int get_charger_detect_status(void)
{
	kal_bool chr_status;

	battery_charging_control(CHARGING_CMD_GET_CHARGER_DET_STATUS, &chr_status);
	return chr_status;
}

#if defined(CONFIG_MTK_POWER_EXT_DETECT)
kal_bool bat_is_ext_power(void)
{
	kal_bool pwr_src = 0;

	battery_charging_control(CHARGING_CMD_GET_POWER_SOURCE, &pwr_src);
	battery_xlog_printk(BAT_LOG_FULL, "[BAT_IS_EXT_POWER] is_ext_power = %d\n", pwr_src);
	return pwr_src;
}
#endif
/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // PMIC PCHR Related APIs */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
kal_bool upmu_is_chr_det(void)
{
#if !defined(CONFIG_POWER_EXT)
	kal_uint32 tmp32;
#endif

    if(battery_charging_control == NULL)
    {
#if defined(CONFIG_MTK_BQ24296_SUPPORT) && defined(CONFIG_MTK_SN2871_SUPPORT)
		if (bq24296_is_found == KAL_TRUE)
			battery_charging_control = chr_control_interface_bq24296;
		else if (sn2871_is_found == KAL_TRUE)
			battery_charging_control = chr_control_interface_sn2871;
		else
			battery_charging_control = chr_control_interface_bq24296;
#elif defined(CONFIG_MTK_BQ24296_SUPPORT)
		battery_charging_control = chr_control_interface_bq24296;
#endif
    }
#if defined(CONFIG_POWER_EXT)
	/* return KAL_TRUE; */
	return get_charger_detect_status();
#else
        if (suspend_discharging==1)
        return KAL_FALSE;

	tmp32 = get_charger_detect_status();

#ifdef CONFIG_MTK_POWER_EXT_DETECT
	if (KAL_TRUE == bat_is_ext_power())
		return tmp32;
#endif

	if (tmp32 == 0) {
		return KAL_FALSE;
	} else {
		#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
		if (mt_usb_is_device()) {
			battery_xlog_printk(BAT_LOG_FULL,
					    "[upmu_is_chr_det] Charger exist and USB is not host\n");

			return KAL_TRUE;
		} else {
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[upmu_is_chr_det] Charger exist but USB is host\n");

			return KAL_FALSE;
		}
		#else
		return KAL_TRUE;
		#endif
	}
#endif
}
EXPORT_SYMBOL(upmu_is_chr_det);


void wake_up_bat(void)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] wake_up_bat. \n");

	chr_wake_up_bat = KAL_TRUE;
	bat_thread_timeout = KAL_TRUE;
#ifdef MTK_ENABLE_AGING_ALGORITHM
	suspend_time = 0;
#endif
    _g_bat_sleep_total_time = 0;
	wake_up(&bat_thread_wq);
}
EXPORT_SYMBOL(wake_up_bat);


static ssize_t bat_log_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char proc_bat_data;
	if ((len <= 0) || copy_from_user(&proc_bat_data, buff, 1)) {
		battery_xlog_printk(BAT_LOG_FULL, "bat_log_write error.\n");
		return -EFAULT;
	}

	if (proc_bat_data == '1') {
		battery_xlog_printk(BAT_LOG_CRTI, "enable battery driver log system\n");
		Enable_BATDRV_LOG = 1;
	} else if (proc_bat_data == '2') {
		battery_xlog_printk(BAT_LOG_CRTI, "enable battery driver log system:2\n");
		Enable_BATDRV_LOG = 2;
	} else {
		battery_xlog_printk(BAT_LOG_CRTI, "Disable battery driver log system\n");
		Enable_BATDRV_LOG = 0;
	}

	return len;
}

static const struct file_operations bat_proc_fops = {
	.write = bat_log_write,
};

int init_proc_log(void)
{
	int ret = 0;

#if 1
	proc_create("batdrv_log", 0644, NULL, &bat_proc_fops);
	battery_xlog_printk(BAT_LOG_CRTI, "proc_create bat_proc_fops\n");
#else
	proc_entry = create_proc_entry("batdrv_log", 0644, NULL);

	if (proc_entry == NULL) {
		ret = -ENOMEM;
		battery_xlog_printk(BAT_LOG_FULL, "init_proc_log: Couldn't create proc entry\n");
	} else {
		proc_entry->write_proc = bat_log_write;
		battery_xlog_printk(BAT_LOG_CRTI, "init_proc_log loaded.\n");
	}
#endif

	return ret;
}


static int wireless_get_property(struct power_supply *psy,
				 enum power_supply_property psp, union power_supply_propval *val)
{
	int ret = 0;
	struct wireless_data *data = container_of(psy, struct wireless_data, psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = data->WIRELESS_ONLINE;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int ac_get_property(struct power_supply *psy,
			   enum power_supply_property psp, union power_supply_propval *val)
{
	int ret = 0;
	struct ac_data *data = container_of(psy, struct ac_data, psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = data->AC_ONLINE;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int usb_get_property(struct power_supply *psy,
			    enum power_supply_property psp, union power_supply_propval *val)
{
	int ret = 0;
	struct usb_data *data = container_of(psy, struct usb_data, psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
#if defined(CONFIG_POWER_EXT)
		/* #if 0 */
		data->USB_ONLINE = 1;
		val->intval = data->USB_ONLINE;
#else
#if defined(CONFIG_MTK_POWER_EXT_DETECT)
		if (KAL_TRUE == bat_is_ext_power())
			data->USB_ONLINE = 1;
#endif
		val->intval = data->USB_ONLINE;
#endif
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int battery_get_property(struct power_supply *psy,
				enum power_supply_property psp, union power_supply_propval *val)
{
	int ret = 0;
	struct battery_data *data = container_of(psy, struct battery_data, psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = data->BAT_STATUS;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = data->BAT_HEALTH;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = data->BAT_PRESENT;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = data->BAT_TECHNOLOGY;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = data->BAT_CAPACITY;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = data->BAT_VOLTAGE_NOW;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = data->BAT_TEMP;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		val->intval = data->BAT_VOLTAGE_AVG;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = data->CONSTANT_CHARGE_CURRENT;
		break;
	case POWER_SUPPLY_PROP_batt_vol:
		val->intval = data->BAT_batt_vol;
		break;
	case POWER_SUPPLY_PROP_TemperatureR:
		val->intval = data->BAT_TemperatureR;
		break;
	case POWER_SUPPLY_PROP_TempBattVoltage:
		val->intval = data->BAT_TempBattVoltage;
		break;
	case POWER_SUPPLY_PROP_InstatVolt:
		val->intval = data->BAT_InstatVolt;
		break;
	case POWER_SUPPLY_PROP_BatteryAverageCurrent:
		val->intval = data->BAT_BatteryAverageCurrent;
		break;
	case POWER_SUPPLY_PROP_BatterySenseVoltage:
		val->intval = data->BAT_BatterySenseVoltage;
		break;
	case POWER_SUPPLY_PROP_ISenseVoltage:
		val->intval = data->BAT_ISenseVoltage;
		break;
	case POWER_SUPPLY_PROP_ChargerVoltage:
		val->intval = data->BAT_ChargerVoltage;
		break;
		/* Dual battery */
	case POWER_SUPPLY_PROP_status_smb:
		val->intval = data->status_smb;
		break;
	case POWER_SUPPLY_PROP_capacity_smb:
		val->intval = data->capacity_smb;
		break;
	case POWER_SUPPLY_PROP_present_smb:
		val->intval = data->present_smb;
		break;
	case POWER_SUPPLY_PROP_adjust_power :
		val->intval = data->adjust_power;
		break;
	/* ACOS_MOD_BEGIN {metrics_log} */
	case POWER_SUPPLY_PROP_SUSPEND_DRAIN:
		val->intval = data->BAT_SuspendDrain;
		break;
	case POWER_SUPPLY_PROP_SUSPEND_REALTIME:
		val->intval = data->BAT_SuspendRealtime;
		break;
	/* ACOS_MOD_END {metrics_log} */

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* wireless_data initialization */
static struct wireless_data wireless_main = {
	.psy = {
		.name = "wireless",
		.type = POWER_SUPPLY_TYPE_WIRELESS,
		.properties = wireless_props,
		.num_properties = ARRAY_SIZE(wireless_props),
		.get_property = wireless_get_property,
		},
	.WIRELESS_ONLINE = 0,
};

/* ac_data initialization */
static struct ac_data ac_main = {
	.psy = {
		.name = "ac",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.properties = ac_props,
		.num_properties = ARRAY_SIZE(ac_props),
		.get_property = ac_get_property,
		},
	.AC_ONLINE = 0,
};

/* usb_data initialization */
static struct usb_data usb_main = {
	.psy = {
		.name = "usb",
		.type = POWER_SUPPLY_TYPE_USB,
		.properties = usb_props,
		.num_properties = ARRAY_SIZE(usb_props),
		.get_property = usb_get_property,
		},
	.USB_ONLINE = 0,
};

/* battery_data initialization */
static struct battery_data battery_main = {
	.psy = {
		.name = "battery",
		.type = POWER_SUPPLY_TYPE_BATTERY,
		.properties = battery_props,
		.num_properties = ARRAY_SIZE(battery_props),
		.get_property = battery_get_property,
		},
/* CC: modify to have a full power supply status */
#if defined(CONFIG_POWER_EXT)
	.BAT_STATUS = POWER_SUPPLY_STATUS_FULL,
	.BAT_HEALTH = POWER_SUPPLY_HEALTH_GOOD,
	.BAT_PRESENT = 1,
	.BAT_TECHNOLOGY = POWER_SUPPLY_TECHNOLOGY_LION,
	.BAT_CAPACITY = 100,
	.BAT_VOLTAGE_NOW = 4200000,
	.BAT_VOLTAGE_AVG = 4200000,
	.CONSTANT_CHARGE_CURRENT = 0,
	.BAT_batt_vol = 4200,
	.BAT_TEMP = 22,
	/* Dual battery */
	.status_smb = POWER_SUPPLY_STATUS_NOT_CHARGING,
	.capacity_smb = 50,
	.present_smb = 0,
	/* ADB CMD discharging*/
	.adjust_power = -1,
#else
	.BAT_STATUS = POWER_SUPPLY_STATUS_NOT_CHARGING,
	.BAT_HEALTH = POWER_SUPPLY_HEALTH_GOOD,
	.BAT_PRESENT = 1,
	.BAT_TECHNOLOGY = POWER_SUPPLY_TECHNOLOGY_LION,
	#if defined(CONFIG_MTK_PUMP_EXPRESS_SUPPORT) || defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
	.BAT_CAPACITY = -1,
	#else
	.BAT_CAPACITY = 50,
	#endif
	.BAT_VOLTAGE_NOW = 0,
	.BAT_VOLTAGE_AVG = 0,
	.CONSTANT_CHARGE_CURRENT = 0,
	.BAT_batt_vol = 0,
	.BAT_TEMP = 0,
	/* Dual battery */
	.status_smb = POWER_SUPPLY_STATUS_NOT_CHARGING,
	.capacity_smb = 50,
	.present_smb = 0,
	/* ADB CMD discharging*/
	.adjust_power = -1,
#endif
};

#ifdef CONFIG_AMAZON_METRICS_LOG
/* must be called with info->lock held */
static void battery_metrics_locked(struct battery_info *info)
{
	struct timespec diff;

	diff = timespec_sub(current_kernel_time(), info->suspend_time);
	if (diff.tv_sec > 0 && diff.tv_sec < MAX_TIME_INTERVAL) {
		if (info->suspend_capacity != -1) {
			char buf[256];
			int drain_diff;

			snprintf(buf, sizeof(buf),
				"suspend_drain:def:value=%d;CT;1,elapsed=%ld;TI;1:NR",
				info->suspend_capacity - BMT_status.UI_SOC,
				diff.tv_sec * 1000 + diff.tv_nsec / NSEC_PER_MSEC);
			log_to_metrics(ANDROID_LOG_INFO, "drain_metrics", buf);


			snprintf(buf, sizeof(buf),
				"batt:def:cap=%d;CT;1,mv=%d;CT;1,current_avg=%d;CT;1,"
				"temp_g=%d;CT;1,charge=%d;CT;1,charge_design=%d;CT;1:NR",
				BMT_status.UI_SOC, BMT_status.bat_vol,
				BMT_status.ICharging, BMT_status.temperature,
				gFG_BATT_CAPACITY_aging, /*battery_remaining_charge,?*/
				gFG_BATT_CAPACITY /*battery_remaining_charge_design*/
				);
			log_to_metrics(ANDROID_LOG_INFO, "bq24296", buf);

			/* This delta will be negative when charging on AC, and
			 * possibly also in the case of an SOC jump. */
			drain_diff = info->suspend_capacity - BMT_status.UI_SOC;
			if (drain_diff >= 0) {
				battery_main.BAT_SuspendDrain += drain_diff;
				battery_main.BAT_SuspendRealtime += diff.tv_sec;
			}
		}
	}
}
#endif

#if !defined(CONFIG_POWER_EXT)
/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : Charging_Enable */
/* ///////////////////////////////////////////////////////////////////////////////////////// */

static ssize_t show_Charging_Enable(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	pr_info("[Battery] show_Charging_Enable : %d\n",
			    BMT_status.bat_in_charging_enable);
	return sprintf(buf, "%d\n", BMT_status.bat_in_charging_enable);
}

static ssize_t store_Charging_Enable(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	U32 enable = 0;
	pr_info("[Battery] store_Charging_Enable\n");
	if (buf != NULL && size != 0) {
		enable = simple_strtoul(buf, NULL, 32);
		if ((0 == enable) || (1 == enable)) {
			BMT_status.bat_in_charging_enable = enable;
			if (battery_charging_control) {
				if (0 == enable)
					enable = TESTMODE_DISABLE_CHARGING << 16;
				else
					enable = TESTMODE_ENABLE_CHARGING << 16;

				battery_charging_control(CHARGING_CMD_ENABLE, &enable);
			}
		} else
			pr_warn("[Battery] store_Charging_Enable: invalid input\n");
	}
	return size;
}

static DEVICE_ATTR(Charging_Enable, 0664, show_Charging_Enable, store_Charging_Enable);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Charger_Voltage */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Charger_Voltage(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] show_ADC_Charger_Voltage : %d\n",
			    BMT_status.charger_vol);
	return sprintf(buf, "%d\n", BMT_status.charger_vol);
}

static ssize_t store_ADC_Charger_Voltage(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Charger_Voltage, 0664, show_ADC_Charger_Voltage, store_ADC_Charger_Voltage);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_0_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_0_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 0));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_0_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_0_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_0_Slope, 0664, show_ADC_Channel_0_Slope, store_ADC_Channel_0_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_1_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_1_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 1));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_1_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_1_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_1_Slope, 0664, show_ADC_Channel_1_Slope, store_ADC_Channel_1_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_2_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_2_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 2));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_2_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_2_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_2_Slope, 0664, show_ADC_Channel_2_Slope, store_ADC_Channel_2_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_3_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_3_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 3));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_3_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_3_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_3_Slope, 0664, show_ADC_Channel_3_Slope, store_ADC_Channel_3_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_4_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_4_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 4));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_4_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_4_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_4_Slope, 0664, show_ADC_Channel_4_Slope, store_ADC_Channel_4_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_5_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_5_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 5));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_5_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_5_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_5_Slope, 0664, show_ADC_Channel_5_Slope, store_ADC_Channel_5_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_6_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_6_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 6));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_6_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_6_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_6_Slope, 0664, show_ADC_Channel_6_Slope, store_ADC_Channel_6_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_7_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_7_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 7));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_7_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_7_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_7_Slope, 0664, show_ADC_Channel_7_Slope, store_ADC_Channel_7_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_8_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_8_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 8));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_8_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_8_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_8_Slope, 0664, show_ADC_Channel_8_Slope, store_ADC_Channel_8_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_9_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_9_Slope(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 9));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_9_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_9_Slope(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_9_Slope, 0664, show_ADC_Channel_9_Slope, store_ADC_Channel_9_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_10_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_10_Slope(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 10));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_10_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_10_Slope(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_10_Slope, 0664, show_ADC_Channel_10_Slope,
		   store_ADC_Channel_10_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_11_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_11_Slope(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 11));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_11_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_11_Slope(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_11_Slope, 0664, show_ADC_Channel_11_Slope,
		   store_ADC_Channel_11_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_12_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_12_Slope(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 12));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_12_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_12_Slope(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_12_Slope, 0664, show_ADC_Channel_12_Slope,
		   store_ADC_Channel_12_Slope);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_13_Slope */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_13_Slope(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_slop + 13));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_13_Slope : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_13_Slope(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_13_Slope, 0664, show_ADC_Channel_13_Slope,
		   store_ADC_Channel_13_Slope);


/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_0_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_0_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 0));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_0_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_0_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_0_Offset, 0664, show_ADC_Channel_0_Offset,
		   store_ADC_Channel_0_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_1_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_1_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 1));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_1_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_1_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_1_Offset, 0664, show_ADC_Channel_1_Offset,
		   store_ADC_Channel_1_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_2_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_2_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 2));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_2_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_2_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_2_Offset, 0664, show_ADC_Channel_2_Offset,
		   store_ADC_Channel_2_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_3_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_3_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 3));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_3_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_3_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_3_Offset, 0664, show_ADC_Channel_3_Offset,
		   store_ADC_Channel_3_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_4_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_4_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 4));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_4_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_4_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_4_Offset, 0664, show_ADC_Channel_4_Offset,
		   store_ADC_Channel_4_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_5_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_5_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 5));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_5_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_5_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_5_Offset, 0664, show_ADC_Channel_5_Offset,
		   store_ADC_Channel_5_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_6_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_6_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 6));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_6_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_6_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_6_Offset, 0664, show_ADC_Channel_6_Offset,
		   store_ADC_Channel_6_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_7_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_7_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 7));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_7_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_7_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_7_Offset, 0664, show_ADC_Channel_7_Offset,
		   store_ADC_Channel_7_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_8_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_8_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 8));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_8_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_8_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_8_Offset, 0664, show_ADC_Channel_8_Offset,
		   store_ADC_Channel_8_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_9_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_9_Offset(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 9));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_9_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_9_Offset(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_9_Offset, 0664, show_ADC_Channel_9_Offset,
		   store_ADC_Channel_9_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_10_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_10_Offset(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 10));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_10_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_10_Offset(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_10_Offset, 0664, show_ADC_Channel_10_Offset,
		   store_ADC_Channel_10_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_11_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_11_Offset(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 11));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_11_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_11_Offset(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_11_Offset, 0664, show_ADC_Channel_11_Offset,
		   store_ADC_Channel_11_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_12_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_12_Offset(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 12));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_12_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_12_Offset(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_12_Offset, 0664, show_ADC_Channel_12_Offset,
		   store_ADC_Channel_12_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_13_Offset */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_13_Offset(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	int ret_value = 1;
	ret_value = (*(adc_cali_offset + 13));
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_13_Offset : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_13_Offset(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_13_Offset, 0664, show_ADC_Channel_13_Offset,
		   store_ADC_Channel_13_Offset);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : ADC_Channel_Is_Calibration */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_ADC_Channel_Is_Calibration(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	int ret_value = 2;
	ret_value = g_ADC_Cali;
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] ADC_Channel_Is_Calibration : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_ADC_Channel_Is_Calibration(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(ADC_Channel_Is_Calibration, 0664, show_ADC_Channel_Is_Calibration,
		   store_ADC_Channel_Is_Calibration);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : Power_On_Voltage */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_Power_On_Voltage(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret_value = 1;
	ret_value = 3400;
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Power_On_Voltage : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_Power_On_Voltage(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(Power_On_Voltage, 0664, show_Power_On_Voltage, store_Power_On_Voltage);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : Power_Off_Voltage */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_Power_Off_Voltage(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret_value = 1;
	ret_value = 3400;
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Power_Off_Voltage : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_Power_Off_Voltage(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(Power_Off_Voltage, 0664, show_Power_Off_Voltage, store_Power_Off_Voltage);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : Charger_TopOff_Value */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_Charger_TopOff_Value(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	int ret_value = 1;
	ret_value = 4110;
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Charger_TopOff_Value : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_Charger_TopOff_Value(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(Charger_TopOff_Value, 0664, show_Charger_TopOff_Value,
		   store_Charger_TopOff_Value);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : FG_Battery_CurrentConsumption */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_FG_Battery_CurrentConsumption(struct device *dev, struct device_attribute *attr,
						  char *buf)
{
	int ret_value = 8888;
	ret_value = battery_meter_get_battery_current();
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] FG_Battery_CurrentConsumption : %d/10 mA\n",
			    ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_FG_Battery_CurrentConsumption(struct device *dev,
						   struct device_attribute *attr, const char *buf,
						   size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(FG_Battery_CurrentConsumption, 0664, show_FG_Battery_CurrentConsumption,
		   store_FG_Battery_CurrentConsumption);

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Create File For EM : FG_SW_CoulombCounter */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_FG_SW_CoulombCounter(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	kal_int32 ret_value = 7777;
	ret_value = battery_meter_get_car();
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] FG_SW_CoulombCounter : %d\n", ret_value);
	return sprintf(buf, "%u\n", ret_value);
}

static ssize_t store_FG_SW_CoulombCounter(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
	return size;
}

static DEVICE_ATTR(FG_SW_CoulombCounter, 0664, show_FG_SW_CoulombCounter,
		   store_FG_SW_CoulombCounter);


static ssize_t show_Charging_CallState(struct device *dev, struct device_attribute *attr, char *buf)
{
	battery_xlog_printk(BAT_LOG_CRTI, "call state = %d\n", g_call_state);
	return sprintf(buf, "%u\n", g_call_state);
}

static ssize_t store_Charging_CallState(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	sscanf(buf, "%u", &g_call_state);
	battery_xlog_printk(BAT_LOG_CRTI, "call state = %d\n", g_call_state);
	return size;
}

static DEVICE_ATTR(Charging_CallState, 0664, show_Charging_CallState, store_Charging_CallState);

static ssize_t show_Charger_Type(struct device *dev,struct device_attribute *attr,
					char *buf)
{
    UINT32 chr_ype = CHARGER_UNKNOWN;
    chr_ype = BMT_status.charger_exist ? BMT_status.charger_type : CHARGER_UNKNOWN;

    battery_xlog_printk(BAT_LOG_CRTI, "CHARGER_TYPE = %d\n",chr_ype);
    return sprintf(buf, "%u\n", chr_ype);
}
static ssize_t store_Charger_Type(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
    battery_xlog_printk(BAT_LOG_CRTI, "[EM] Not Support Write Function\n");
    return size;
}
static DEVICE_ATTR(Charger_Type, 0664, show_Charger_Type, store_Charger_Type);

static ssize_t show_Custom_PlugIn_Time(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	battery_xlog_printk(BAT_LOG_CRTI, "custom plugin_time = %lu\n", g_custom_plugin_time);
	return sprintf(buf, "0");
}

static ssize_t store_Custom_PlugIn_Time(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;

	ret = kstrtoul(buf, 0, &g_custom_plugin_time);
	battery_xlog_printk(BAT_LOG_CRTI, "custom plugin_time = %lu\n", g_custom_plugin_time);
	if (g_custom_plugin_time > PLUGIN_THRESHOLD)
		g_custom_plugin_time = PLUGIN_THRESHOLD;

	wake_up_bat();
	return size;
}

static DEVICE_ATTR(Custom_PlugIn_Time, 0664, show_Custom_PlugIn_Time,
		   store_Custom_PlugIn_Time);

static ssize_t show_Custom_Charging_Current(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	battery_xlog_printk(BAT_LOG_CRTI, "custom charging current = %d\n",
			    g_custom_charging_current);
	return sprintf(buf, "%d\n", g_custom_charging_current);
}

static ssize_t store_Custom_Charging_Current(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t size)
{
	int ret, cur;

	ret = kstrtouint(buf, 0, &cur);
	g_custom_charging_current = cur;
	battery_xlog_printk(BAT_LOG_CRTI, "custom charging current = %d\n",
			    g_custom_charging_current);
	wake_up_bat();
	return size;
}

static DEVICE_ATTR(Custom_Charging_Current, 0664, show_Custom_Charging_Current,
		   store_Custom_Charging_Current);

#ifdef CONFIG_AUSTIN_PROJECT
#if defined(CONFIG_AUSTIN_PROJECT)
static ssize_t show_Custom_Charging_Mode(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_notice("Charging mode = %u\n", g_custom_charging_mode);
	return sprintf(buf, "%u", g_custom_charging_mode);
}

static ssize_t store_Custom_Charging_Mode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;

	ret = kstrtouint(buf, 0, &g_custom_charging_mode);
	pr_notice("Charging mode= %u\n", g_custom_charging_mode);

	return size;
}

static DEVICE_ATTR(Custom_Charging_Mode, 0660, show_Custom_Charging_Mode,
		   store_Custom_Charging_Mode);
#endif
static ssize_t show_charge_ic_vendor_name(struct device *dev, struct device_attribute *attr,
                    char *buf)
{
	if (sn2871_is_found == KAL_TRUE)
		charge_ic_vendor_name  = "sn2871";
	else if (bq24296_is_found == KAL_TRUE)
		charge_ic_vendor_name = "bq24296m";
	else
		charge_ic_vendor_name = "charge ic is not found";
	battery_xlog_printk(BAT_LOG_CRTI, "Charge IC: %s\n", charge_ic_vendor_name);
	return sprintf(buf, "%s\n", charge_ic_vendor_name);
}
static DEVICE_ATTR(ChargeIC_Vendor_Name, 0444, show_charge_ic_vendor_name, NULL);

static ssize_t show_battery_vendor_name(struct device *dev, struct device_attribute *attr,
                    char *buf)
{
	if (g_fg_battery_id == 0)
		battery_vendor_name  = "ATL_NVT";
	else if (g_fg_battery_id == 1)
		battery_vendor_name = "Coslight_Sunwoda";
	else
		battery_vendor_name = "battery  is not found";
	battery_xlog_printk(BAT_LOG_CRTI, "Battery Vendor: %s\n", battery_vendor_name);
	return sprintf(buf, "%s\n", battery_vendor_name);
}
static DEVICE_ATTR(Battery_Vendor_Name, 0444, show_battery_vendor_name, NULL);
#endif



#if defined(CONFIG_MTK_PUMP_EXPRESS_SUPPORT) || defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
static ssize_t show_Pump_Express(struct device *dev,struct device_attribute *attr,
					char *buf)
{
    #if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
    int icount = 20;  //max debouncing time 20 * 0.2 sec

    if (KAL_TRUE == ta_check_chr_type &&
      STANDARD_CHARGER == BMT_status.charger_type &&
      BMT_status.SOC >= TA_START_BATTERY_SOC &&
      BMT_status.SOC < TA_STOP_BATTERY_SOC)
    {
        battery_xlog_printk(BAT_LOG_CRTI, "[%s]Wait for PE detection\n", __func__);
        do
        {
            icount--;
            msleep(200);
        }while(icount && ta_check_chr_type);
    }
    #endif

    #if defined(CONFIG_MTK_PUMP_EXPRESS_SUPPORT)

    if ( (KAL_TRUE == ta_check_chr_type) &&
      (STANDARD_CHARGER == BMT_status.charger_type) )
    {
        battery_xlog_printk(BAT_LOG_CRTI, "[%s]Wait for PE detection\n", __func__);
        do
        {
            msleep(200);
        }while(ta_check_chr_type);
    }
    #endif
    battery_xlog_printk(BAT_LOG_CRTI, "Pump express = %d\n",is_ta_connect);    
    return sprintf(buf, "%u\n", is_ta_connect);
}
static ssize_t store_Pump_Express(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	sscanf(buf, "%u", &is_ta_connect);
    battery_xlog_printk(BAT_LOG_CRTI, "Pump express= %d\n",is_ta_connect);    
    return size;
}
static DEVICE_ATTR(Pump_Express, 0664, show_Pump_Express, store_Pump_Express);
#endif

static void mt_battery_update_EM(struct battery_data *bat_data)
{
	bat_data->BAT_CAPACITY = BMT_status.UI_SOC;
	bat_data->BAT_TemperatureR = BMT_status.temperatureR;	/* API */
	bat_data->BAT_TempBattVoltage = BMT_status.temperatureV;	/* API */
	bat_data->BAT_InstatVolt = BMT_status.bat_vol;	/* VBAT */
	bat_data->BAT_BatteryAverageCurrent = BMT_status.ICharging;
	bat_data->BAT_BatterySenseVoltage = BMT_status.bat_vol;
	bat_data->BAT_ISenseVoltage = BMT_status.Vsense;	/* API */
	bat_data->BAT_ChargerVoltage = BMT_status.charger_vol;
	/* Dual battery */
	bat_data->status_smb = g_status_smb;
	bat_data->capacity_smb = g_capacity_smb;
	bat_data->present_smb = g_present_smb;
	battery_xlog_printk(BAT_LOG_FULL, "status_smb = %d, capacity_smb = %d, present_smb = %d\n",
			    bat_data->status_smb, bat_data->capacity_smb, bat_data->present_smb);
	if ((BMT_status.UI_SOC == 100) && (BMT_status.charger_exist == KAL_TRUE))
		bat_data->BAT_STATUS = POWER_SUPPLY_STATUS_FULL;

#ifdef CONFIG_MTK_DISABLE_POWER_ON_OFF_VOLTAGE_LIMITATION
	if (bat_data->BAT_CAPACITY <= 0)
		bat_data->BAT_CAPACITY = 1;

	battery_xlog_printk(BAT_LOG_CRTI,
			    "BAT_CAPACITY=1, due to define CONFIG_MTK_DISABLE_POWER_ON_OFF_VOLTAGE_LIMITATION\n");
#endif
}


static kal_bool mt_battery_100Percent_tracking_check(void)
{
	kal_bool resetBatteryMeter = KAL_FALSE;

#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
	kal_uint32 cust_sync_time = CUST_SOC_JEITA_SYNC_TIME;
	static kal_uint32 timer_counter = (CUST_SOC_JEITA_SYNC_TIME / BAT_TASK_PERIOD);
#else
	kal_uint32 cust_sync_time = ONEHUNDRED_PERCENT_TRACKING_TIME;
	static kal_uint32 timer_counter = (ONEHUNDRED_PERCENT_TRACKING_TIME / BAT_TASK_PERIOD);
#endif

	if (BMT_status.bat_full == KAL_TRUE) { /* charging full first, UI tracking to 100% */

		if (BMT_status.bat_in_recharging_state == KAL_TRUE) {
			if (BMT_status.UI_SOC >= 100)
				BMT_status.UI_SOC = 100;

			resetBatteryMeter = KAL_FALSE;
		} else if (BMT_status.UI_SOC >= 100) {
			BMT_status.UI_SOC = 100;

			if ((g_charging_full_reset_bat_meter == KAL_TRUE)
			    && (BMT_status.bat_charging_state == CHR_BATFULL)) {
				resetBatteryMeter = KAL_TRUE;
				g_charging_full_reset_bat_meter = KAL_FALSE;
			} else {
				resetBatteryMeter = KAL_FALSE;
			}
		} else {
			/* increase UI percentage every xxs */
			if (timer_counter >= (cust_sync_time / BAT_TASK_PERIOD)) {
				timer_counter = 1;
				BMT_status.UI_SOC++;
			} else {
				timer_counter++;

				return resetBatteryMeter;
			}

			resetBatteryMeter = KAL_TRUE;
		}

		battery_xlog_printk(BAT_LOG_FULL, "[100percent], UI_SOC(%d), reset(%d)\n",
				    BMT_status.UI_SOC, resetBatteryMeter);
	} else {
		/* charging is not full,  UI keep 99% if reaching 100%, */

		if (BMT_status.UI_SOC >= 99) {
			BMT_status.UI_SOC = 99;
			resetBatteryMeter = KAL_FALSE;

			battery_xlog_printk(BAT_LOG_FULL, "[100percent],UI_SOC = %d\n",
					    BMT_status.UI_SOC);
		}

		timer_counter = (cust_sync_time / BAT_TASK_PERIOD);

	}

	return resetBatteryMeter;
}


static kal_bool mt_battery_nPercent_tracking_check(void)
{
	kal_bool resetBatteryMeter = KAL_FALSE;
#if defined(SOC_BY_HW_FG)
	static kal_uint32 timer_counter = (NPERCENT_TRACKING_TIME / BAT_TASK_PERIOD);

	if (BMT_status.nPrecent_UI_SOC_check_point == 0)
		return KAL_FALSE;

	/* fuel gauge ZCV < 15%, but UI > 15%,  15% can be customized */
	if ((BMT_status.ZCV <= BMT_status.nPercent_ZCV)
	    && (BMT_status.UI_SOC > BMT_status.nPrecent_UI_SOC_check_point)) {
		if (timer_counter == (NPERCENT_TRACKING_TIME / BAT_TASK_PERIOD))	/* every x sec decrease UI percentage */
		{
			BMT_status.UI_SOC--;
			timer_counter = 1;
		} else {
			timer_counter++;
			return resetBatteryMeter;
		}

		resetBatteryMeter = KAL_TRUE;

		battery_xlog_printk(BAT_LOG_CRTI,
				    "[nPercent] ZCV %d <= nPercent_ZCV %d, UI_SOC=%d., tracking UI_SOC=%d\n",
				    BMT_status.ZCV, BMT_status.nPercent_ZCV, BMT_status.UI_SOC,
				    BMT_status.nPrecent_UI_SOC_check_point);
	} else if ((BMT_status.ZCV > BMT_status.nPercent_ZCV)
		   && (BMT_status.UI_SOC == BMT_status.nPrecent_UI_SOC_check_point)) {
		/* UI less than 15 , but fuel gague is more than 15, hold UI 15% */
		timer_counter = (NPERCENT_TRACKING_TIME / BAT_TASK_PERIOD);
		resetBatteryMeter = KAL_TRUE;

		battery_xlog_printk(BAT_LOG_CRTI,
				    "[nPercent] ZCV %d > BMT_status.nPercent_ZCV %d and UI SOC=%d, then keep %d.\n",
				    BMT_status.ZCV, BMT_status.nPercent_ZCV, BMT_status.UI_SOC,
				    BMT_status.nPrecent_UI_SOC_check_point);
	} else {
		timer_counter = (NPERCENT_TRACKING_TIME / BAT_TASK_PERIOD);
	}
#endif
	return resetBatteryMeter;

}

static kal_bool mt_battery_0Percent_tracking_check(void)
{
	kal_bool resetBatteryMeter = KAL_TRUE;

	if (BMT_status.UI_SOC <= 0) {
		BMT_status.UI_SOC = 0;
	} else {
		if (BMT_status.bat_vol > SYSTEM_OFF_VOLTAGE && BMT_status.UI_SOC > 1) {
			BMT_status.UI_SOC--;
		} else if (BMT_status.bat_vol <= SYSTEM_OFF_VOLTAGE) {
			BMT_status.UI_SOC--;
		}
	}

	battery_xlog_printk(BAT_LOG_CRTI, "0Percent, VBAT < %d UI_SOC=%d\n", SYSTEM_OFF_VOLTAGE,
			    BMT_status.UI_SOC);

	return resetBatteryMeter;
}


static void mt_battery_Sync_UI_Percentage_to_Real(void)
{
	static kal_uint32 timer_counter;

	if ((BMT_status.UI_SOC > BMT_status.SOC) && ((BMT_status.UI_SOC != 1))) {
#if !defined (SYNC_UI_SOC_IMM)
		/* reduce after xxs */
		if (g_refresh_ui_soc || timer_counter == (SYNC_TO_REAL_TRACKING_TIME / BAT_TASK_PERIOD)) {
			BMT_status.UI_SOC--;
			timer_counter = 0;
			g_refresh_ui_soc = KAL_FALSE;
		} else {
			timer_counter++;
		}
#else
		BMT_status.UI_SOC--;
#endif
		battery_xlog_printk(BAT_LOG_CRTI, "[Sync_Real] UI_SOC=%d, SOC=%d, counter = %d\n",
				    BMT_status.UI_SOC, BMT_status.SOC, timer_counter);
	} else {
		timer_counter = 0;

		if (BMT_status.UI_SOC == -1)
			BMT_status.UI_SOC = BMT_status.SOC;
		else if (BMT_status.charger_exist && BMT_status.bat_charging_state != CHR_ERROR) {
			if (BMT_status.UI_SOC < BMT_status.SOC && (BMT_status.SOC - BMT_status.UI_SOC > 1))
				BMT_status.UI_SOC++;
			else
				BMT_status.UI_SOC = BMT_status.SOC;
		}
	}

	if (BMT_status.UI_SOC <= 0) {
		BMT_status.UI_SOC = 1;
		battery_xlog_printk(BAT_LOG_CRTI, "[Battery]UI_SOC get 0 first (%d)\n",
				    BMT_status.UI_SOC);
	}
}

static void battery_update(struct battery_data *bat_data)
{
	struct power_supply *bat_psy = &bat_data->psy;
	kal_bool resetBatteryMeter = KAL_FALSE;
	int cc_value;

	bat_data->BAT_TECHNOLOGY = POWER_SUPPLY_TECHNOLOGY_LION;
	if (BMT_status.temperature >= MAX_CHARGE_TEMPERATURE) {
		bat_data->BAT_HEALTH = POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if (BMT_status.temperature < MIN_CHARGE_TEMPERATURE) {
		bat_data->BAT_HEALTH = POWER_SUPPLY_HEALTH_COLD;
	} else if (BMT_status.charger_vol >= V_CHARGER_MAX) {
		bat_data->BAT_HEALTH = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else { /*other battery_health status can be adder here in future*/
		bat_data->BAT_HEALTH = POWER_SUPPLY_HEALTH_GOOD;
	}
	bat_data->BAT_VOLTAGE_AVG = BMT_status.bat_vol * 1000;
	bat_data->BAT_VOLTAGE_NOW = BMT_status.bat_vol * 1000;	/* voltage_now unit is microvolt */
	bat_data->BAT_batt_vol = BMT_status.bat_vol;
	bat_data->BAT_TEMP = BMT_status.temperature * 10;
	bat_data->BAT_PRESENT = BMT_status.bat_exist;

	if (battery_charging_control) {
		battery_charging_control(CHARGING_CMD_GET_CURRENT, &cc_value);
		bat_data->CONSTANT_CHARGE_CURRENT = cc_value * 1000; /* in uA */
	}

	if ((BMT_status.charger_exist == KAL_TRUE) && (BMT_status.bat_charging_state != CHR_ERROR)) {
		if (BMT_status.bat_exist) {	/* charging */
			if (BMT_status.bat_vol <= V_0PERCENT_TRACKING) {
				resetBatteryMeter = mt_battery_0Percent_tracking_check();
			} else {
				resetBatteryMeter = mt_battery_100Percent_tracking_check();
			}

			bat_data->BAT_STATUS = POWER_SUPPLY_STATUS_CHARGING;
		} else {	/* No Battery, Only Charger */

			bat_data->BAT_STATUS = POWER_SUPPLY_STATUS_UNKNOWN;
			BMT_status.UI_SOC = 0;
		}

	} else {		/* Only Battery */

		bat_data->BAT_STATUS = POWER_SUPPLY_STATUS_NOT_CHARGING;
		if (BMT_status.bat_vol <= V_0PERCENT_TRACKING)
			resetBatteryMeter = mt_battery_0Percent_tracking_check();
		else
			resetBatteryMeter = mt_battery_nPercent_tracking_check();
	}

	if (resetBatteryMeter == KAL_TRUE)
		battery_meter_reset(KAL_TRUE);
	else
		mt_battery_Sync_UI_Percentage_to_Real();

	battery_xlog_printk(BAT_LOG_CRTI, "UI_SOC=(%d), resetBatteryMeter=(%d)\n",
			    BMT_status.UI_SOC, resetBatteryMeter);

#ifdef CUST_CAPACITY_OCV2CV_TRANSFORM
	/* We store capacity before loading compenstation in RTC */
	if (battery_meter_get_battery_soc() <= 1)
		set_rtc_spare_fg_value(1);
	else
		set_rtc_spare_fg_value(battery_meter_get_battery_soc());  /*use battery_soc*/
#else

	/* set RTC SOC to 1 to avoid SOC jump in charger boot. */
	if (BMT_status.UI_SOC <= 1)
		set_rtc_spare_fg_value(1);
	else
		set_rtc_spare_fg_value(BMT_status.UI_SOC);
#endif
	battery_xlog_printk(BAT_LOG_FULL, "RTC_SOC=(%d)\n", get_rtc_spare_fg_value());

	mt_battery_update_EM(bat_data);

#if defined(CONFIG_AMAZON_METRICS_LOG)
	metrics_handle();
#endif

	if (cmd_discharging == 1) {
		bat_data->BAT_STATUS = POWER_SUPPLY_STATUS_CMD_DISCHARGING;
	}
	if (adjust_power != -1) {
			bat_data->adjust_power = adjust_power;
			battery_xlog_printk(BAT_LOG_CRTI, "adjust_power=(%d)\n", adjust_power);
	}

	power_supply_changed(bat_psy);
}

void update_charger_info(int wireless_state)
{
#if defined(CONFIG_POWER_VERIFY)
	battery_xlog_printk(BAT_LOG_CRTI, "[update_charger_info] no support\n");
#else
	g_wireless_state = wireless_state;
	battery_xlog_printk(BAT_LOG_CRTI, "[update_charger_info] get wireless_state=%d\n",
			    wireless_state);

	wake_up_bat();
#endif
}

static void wireless_update(struct wireless_data *wireless_data)
{
	static int wireless_status = -1;
	struct power_supply *wireless_psy = &wireless_data->psy;

	if (BMT_status.charger_exist == KAL_TRUE || g_wireless_state) {
		if ((BMT_status.charger_type == WIRELESS_CHARGER) || g_wireless_state) {
			wireless_data->WIRELESS_ONLINE = 1;
			wireless_psy->type = POWER_SUPPLY_TYPE_WIRELESS;
		} else {
			wireless_data->WIRELESS_ONLINE = 0;
		}
	} else {
		wireless_data->WIRELESS_ONLINE = 0;
	}

	if (wireless_status != wireless_data->WIRELESS_ONLINE) {
		wireless_status = wireless_data->WIRELESS_ONLINE;
		power_supply_changed(wireless_psy);
	}
}

static void ac_update(struct ac_data *ac_data)
{
	static int ac_status = -1;
	struct power_supply *ac_psy = &ac_data->psy;

	if (BMT_status.charger_exist == KAL_TRUE) {
		#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
		if ((BMT_status.charger_type == NONSTANDARD_CHARGER) ||
		    (BMT_status.charger_type == STANDARD_CHARGER)) {
		#else
		if ((BMT_status.charger_type == NONSTANDARD_CHARGER) ||
		    (BMT_status.charger_type == STANDARD_CHARGER)    ||
			(DISO_data.diso_state.cur_vdc_state == DISO_ONLINE)) {
		#endif
			ac_data->AC_ONLINE = 1;
			ac_psy->type = POWER_SUPPLY_TYPE_MAINS;
		} else {
			ac_data->AC_ONLINE = 0;
		}
	} else {
		ac_data->AC_ONLINE = 0;
	}

	if (ac_status != ac_data->AC_ONLINE) {
		ac_status = ac_data->AC_ONLINE;
		power_supply_changed(ac_psy);
	}
}

static void usb_update(struct usb_data *usb_data)
{
	static int usb_status = -1;
	struct power_supply *usb_psy = &usb_data->psy;

	if (BMT_status.charger_exist == KAL_TRUE) {
		if ((BMT_status.charger_type == STANDARD_HOST) ||
		    (BMT_status.charger_type == CHARGING_HOST)) {
			usb_data->USB_ONLINE = 1;
			usb_psy->type = POWER_SUPPLY_TYPE_USB;
		} else {
			usb_data->USB_ONLINE = 0;
		}
	} else {
		usb_data->USB_ONLINE = 0;
	}

	if (usb_status != usb_data->USB_ONLINE) {
		usb_status = usb_data->USB_ONLINE;
		power_supply_changed(usb_psy);
	}
}

#endif

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Battery Temprature Parameters and functions */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
kal_bool pmic_chrdet_status(void)
{
	if (upmu_is_chr_det() == KAL_TRUE) {
		return KAL_TRUE;
	} else {
		battery_xlog_printk(BAT_LOG_CRTI, "[pmic_chrdet_status] No charger\n");
		return KAL_FALSE;
	}
}

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Pulse Charging Algorithm */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
kal_bool bat_is_charger_exist(void)
{
	return get_charger_detect_status();
}


kal_bool bat_is_charging_full(void)
{
	if ((BMT_status.bat_full == KAL_TRUE) && (BMT_status.bat_in_recharging_state == KAL_FALSE))
		return KAL_TRUE;
	else
		return KAL_FALSE;
}


kal_uint32 bat_get_ui_percentage(void)
{
	/* for plugging out charger in recharge phase, using SOC as UI_SOC */
	if (chr_wake_up_bat == KAL_TRUE)
		return BMT_status.SOC;
	else
		return BMT_status.UI_SOC;
}

/* Full state --> recharge voltage --> full state */
kal_uint32 bat_is_recharging_phase(void)
{
	return (BMT_status.bat_in_recharging_state || BMT_status.bat_full == KAL_TRUE);
}


int get_bat_charging_current_level(void)
{
	CHR_CURRENT_ENUM charging_current;

	battery_charging_control(CHARGING_CMD_GET_CURRENT, &charging_current);

	return charging_current;
}

#if defined(MTK_TEMPERATURE_RECHARGE_SUPPORT)
PMU_STATUS do_batt_temp_state_machine(void)
{
	if (BMT_status.temperature == ERR_CHARGE_TEMPERATURE) {
		return PMU_STATUS_FAIL;
	}
#ifdef BAT_LOW_TEMP_PROTECT_ENABLE
	if (BMT_status.temperature < MIN_CHARGE_TEMPERATURE) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BATTERY] Battery Under Temperature or NTC fail !!\n\r");
		g_batt_temp_status = TEMP_POS_LOW;
		return PMU_STATUS_FAIL;
	} else if (g_batt_temp_status == TEMP_POS_LOW) {
		if (BMT_status.temperature >= MIN_CHARGE_TEMPERATURE_PLUS_X_DEGREE) {
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[BATTERY] Battery Temperature raise from %d to %d(%d), allow charging!!\n\r",
					    MIN_CHARGE_TEMPERATURE, BMT_status.temperature,
					    MIN_CHARGE_TEMPERATURE_PLUS_X_DEGREE);
			g_batt_temp_status = TEMP_POS_NORMAL;
			BMT_status.bat_charging_state = CHR_PRE;
			return PMU_STATUS_OK;
		} else {
			return PMU_STATUS_FAIL;
		}
	} else
#endif
	if (BMT_status.temperature >= MAX_CHARGE_TEMPERATURE) {
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] Battery Over Temperature !!\n\r");
		g_batt_temp_status = TEMP_POS_HIGH;
		return PMU_STATUS_FAIL;
	} else if (g_batt_temp_status == TEMP_POS_HIGH) {
		if (BMT_status.temperature < MAX_CHARGE_TEMPERATURE_MINUS_X_DEGREE) {
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[BATTERY] Battery Temperature down from %d to %d(%d), allow charging!!\n\r",
					    MAX_CHARGE_TEMPERATURE, BMT_status.temperature,
					    MAX_CHARGE_TEMPERATURE_MINUS_X_DEGREE);
			g_batt_temp_status = TEMP_POS_NORMAL;
			BMT_status.bat_charging_state = CHR_PRE;
			return PMU_STATUS_OK;
		} else {
			return PMU_STATUS_FAIL;
		}
	} else {
		g_batt_temp_status = TEMP_POS_NORMAL;
	}
	return PMU_STATUS_OK;
}
#endif

unsigned long BAT_Get_Battery_Voltage(int polling_mode)
{
	unsigned long ret_val = 0;

#if defined(CONFIG_POWER_EXT)
	ret_val = 4000;
#else
    ret_val=battery_meter_get_battery_voltage(KAL_FALSE);
#endif

	return ret_val;
}


static void mt_battery_average_method_init(kal_uint32 *bufferdata, kal_uint32 data,
					   kal_int32 *sum)
{
	kal_uint32 i;
	static kal_bool batteryBufferFirst = KAL_TRUE;
	static kal_bool previous_charger_exist = KAL_FALSE;
	static kal_bool previous_in_recharge_state = KAL_FALSE;
	static kal_uint8 index;

	/* reset charging current window while plug in/out { */
	if (BMT_status.charger_exist == KAL_TRUE) {
		if (previous_charger_exist == KAL_FALSE) {
			batteryBufferFirst = KAL_TRUE;
			previous_charger_exist = KAL_TRUE;
			#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
			if (BMT_status.charger_type == STANDARD_CHARGER) {
			#else
			if ((BMT_status.charger_type == STANDARD_CHARGER) || 
			    (DISO_data.diso_state.cur_vdc_state == DISO_ONLINE)) {
			#endif
				data = AC_CHARGER_CURRENT / 100;
			} else if (BMT_status.charger_type == CHARGING_HOST) {
				data = CHARGING_HOST_CHARGER_CURRENT / 100;
			} else if (BMT_status.charger_type == NONSTANDARD_CHARGER)
				data = NON_STD_AC_CHARGER_CURRENT / 100;	/* mA */
			else	/* USB */
				data = USB_CHARGER_CURRENT / 100;	/* mA */
		} else if ((previous_in_recharge_state == KAL_FALSE)
			   && (BMT_status.bat_in_recharging_state == KAL_TRUE)) {
			batteryBufferFirst = KAL_TRUE;
			#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
			if (BMT_status.charger_type == STANDARD_CHARGER) {
			#else
			if ((BMT_status.charger_type == STANDARD_CHARGER) || 
			    (DISO_data.diso_state.cur_vdc_state == DISO_ONLINE)) {
			#endif			
				data = AC_CHARGER_CURRENT / 100;
			} else if (BMT_status.charger_type == CHARGING_HOST) {
				data = CHARGING_HOST_CHARGER_CURRENT / 100;
			} else if (BMT_status.charger_type == NONSTANDARD_CHARGER)
				data = NON_STD_AC_CHARGER_CURRENT / 100;	/* mA */
			else	/* USB */
				data = USB_CHARGER_CURRENT / 100;	/* mA */
		}

		previous_in_recharge_state = BMT_status.bat_in_recharging_state;
	} else {
		if (previous_charger_exist == KAL_TRUE) {
			batteryBufferFirst = KAL_TRUE;
			previous_charger_exist = KAL_FALSE;
			data = 0;
		}
	}
	/* reset charging current window while plug in/out } */

	battery_xlog_printk(BAT_LOG_FULL, "batteryBufferFirst =%d, data= (%d)\n",
			    batteryBufferFirst, data);

	if (batteryBufferFirst == KAL_TRUE) {
		for (i = 0; i < BATTERY_AVERAGE_SIZE; i++) {
			bufferdata[i] = data;
		}

		*sum = data * BATTERY_AVERAGE_SIZE;
	}

	index++;
	if (index >= BATTERY_AVERAGE_DATA_NUMBER) {
		index = BATTERY_AVERAGE_DATA_NUMBER;
		batteryBufferFirst = KAL_FALSE;
	}
}


static kal_uint32 mt_battery_average_method(kal_uint32 *bufferdata, kal_uint32 data,
					    kal_int32 *sum, kal_uint8 batteryIndex)
{
	kal_uint32 avgdata;

	mt_battery_average_method_init(bufferdata, data, sum);

	*sum -= bufferdata[batteryIndex];
	*sum += data;
	bufferdata[batteryIndex] = data;
	avgdata = (*sum) / BATTERY_AVERAGE_SIZE;

	battery_xlog_printk(BAT_LOG_FULL, "bufferdata[%d]= (%d)\n", batteryIndex,
			    bufferdata[batteryIndex]);
	return avgdata;
}

void mt_battery_GetBatteryData(void)
{
	kal_uint32 bat_vol, charger_vol, Vsense, ZCV;
	kal_int32 ICharging, temperature, temperatureR, temperatureV, SOC;
	static kal_int32 bat_sum, icharging_sum, temperature_sum;
	static kal_int32 batteryVoltageBuffer[BATTERY_AVERAGE_SIZE];
	static kal_int32 batteryCurrentBuffer[BATTERY_AVERAGE_SIZE];
	static kal_int32 batteryTempBuffer[BATTERY_AVERAGE_SIZE];
	static kal_uint8 batteryIndex = 0;
	static kal_int32 previous_SOC = -1;

	bat_vol = battery_meter_get_battery_voltage(KAL_TRUE);
	Vsense = battery_meter_get_VSense();
	if( upmu_is_chr_det() == KAL_TRUE ) {
		ICharging = battery_meter_get_charging_current();
	} else {
		ICharging = 0;
	}

	charger_vol = battery_meter_get_charger_voltage();
	temperature = battery_meter_get_battery_temperature();
	temperatureV = battery_meter_get_tempV();
	temperatureR = battery_meter_get_tempR(temperatureV);

	if (bat_meter_timeout == KAL_TRUE || bat_spm_timeout == TRUE) {
		SOC = battery_meter_get_battery_percentage();
		//if (bat_spm_timeout == true)
			//BMT_status.UI_SOC = battery_meter_get_battery_percentage();

		bat_meter_timeout = KAL_FALSE;
		bat_spm_timeout = FALSE;
	} else {
		if (previous_SOC == -1)
			SOC = battery_meter_get_battery_percentage();
		else
			SOC = previous_SOC;
	}

	ZCV = battery_meter_get_battery_zcv();

	BMT_status.ICharging =
	    mt_battery_average_method(&batteryCurrentBuffer[0], ICharging, &icharging_sum,
				      batteryIndex);

    
	if (previous_SOC == -1 && bat_vol <= V_0PERCENT_TRACKING) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "battery voltage too low, use ZCV to init average data.\n");
		BMT_status.bat_vol =
		    mt_battery_average_method(&batteryVoltageBuffer[0], ZCV, &bat_sum,
					      batteryIndex);
	} else {
		BMT_status.bat_vol =
		    mt_battery_average_method(&batteryVoltageBuffer[0], bat_vol, &bat_sum,
					      batteryIndex);
	}
	BMT_status.temperature =
	    mt_battery_average_method(&batteryTempBuffer[0], temperature, &temperature_sum,
				      batteryIndex);
	BMT_status.Vsense = Vsense;
	BMT_status.charger_vol = charger_vol;
	BMT_status.temperatureV = temperatureV;
	BMT_status.temperatureR = temperatureR;
	BMT_status.SOC = SOC;
	BMT_status.ZCV = ZCV;

#if !defined(CUST_CAPACITY_OCV2CV_TRANSFORM)
	if (BMT_status.charger_exist == KAL_FALSE) {
		if (BMT_status.SOC > previous_SOC && previous_SOC >= 0)
			BMT_status.SOC = previous_SOC;
	}
#endif

	previous_SOC = BMT_status.SOC;

	batteryIndex++;
	if (batteryIndex >= BATTERY_AVERAGE_SIZE)
		batteryIndex = 0;


	if (g_battery_soc_ready == KAL_FALSE)
		g_battery_soc_ready = KAL_TRUE;

	pr_notice("AvgVbat=(%d),bat_vol=(%d),AvgI=(%d),I=(%d),VChr=(%d),AvgT=(%d),T=(%d),pre_SOC=(%d),SOC=(%d),ZCV=(%d)\n",
			BMT_status.bat_vol, bat_vol, BMT_status.ICharging, ICharging,
			BMT_status.charger_vol, BMT_status.temperature, temperature,
			previous_SOC, BMT_status.SOC, BMT_status.ZCV);


}

#if defined(CONFIG_AUSTIN_PROJECT)
static PMU_STATUS mt_battery_CheckBatteryConnect(void)
{
	PMU_STATUS status = PMU_STATUS_OK;
	int ret = 0;
	int data[4];
	int voltage = 0;

	ret = IMM_GetOneChannelValue(13, data, &voltage);
	if (ret != 0){
		printk("[mt_battery_CheckBatteryConnect]Id voltage read fail\n");
	} else {
		battery_idV = (voltage*1500)/4096;
		printk("[mt_battery_CheckBatteryConnect]Id voltage = %d\n", battery_idV);
	}

	if (battery_idV > 500) {
		battery_xlog_printk(BAT_LOG_CRTI, "[mt_battery_CheckBatteryConnect] battery ID disconnect\n");
		status = PMU_STATUS_FAIL;
	}

	return status;
}
#endif

static PMU_STATUS mt_battery_CheckBatteryTemp(void)
{
	PMU_STATUS status = PMU_STATUS_OK;

#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)

	battery_xlog_printk(BAT_LOG_FULL, "[BATTERY] support JEITA, temperature=%d\n",
			    BMT_status.temperature);

	if (do_jeita_state_machine() == PMU_STATUS_FAIL) {
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] JEITA : fail\n");
		status = PMU_STATUS_FAIL;
	}
#else

#if defined(MTK_TEMPERATURE_RECHARGE_SUPPORT)
	if (do_batt_temp_state_machine() == PMU_STATUS_FAIL) {
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] Batt temp check : fail\n");
		status = PMU_STATUS_FAIL;
	}
#else
#ifdef BAT_LOW_TEMP_PROTECT_ENABLE
	if ((BMT_status.temperature < MIN_CHARGE_TEMPERATURE)
	    || (BMT_status.temperature == ERR_CHARGE_TEMPERATURE)) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BATTERY] Battery Under Temperature or NTC fail !!\n\r");
		status = PMU_STATUS_FAIL;
	}
#endif
	if (BMT_status.temperature >= MAX_CHARGE_TEMPERATURE) {
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] Battery Over Temperature !!\n\r");
		status = PMU_STATUS_FAIL;
	}
#endif

#endif

	return status;
}


static PMU_STATUS mt_battery_CheckChargerVoltage(void)
{
	PMU_STATUS status = PMU_STATUS_OK;
	#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
	kal_uint32 v_charger_max = DISO_data.hv_voltage;
	#endif

	if (BMT_status.charger_exist == KAL_TRUE) {
#if (V_CHARGER_ENABLE == 1)
		if (BMT_status.charger_vol <= V_CHARGER_MIN) {
			battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY]Charger under voltage!!\n");
			BMT_status.bat_charging_state = CHR_ERROR;
			status = PMU_STATUS_FAIL;
		}
#endif
		#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
		if (BMT_status.charger_vol >= V_CHARGER_MAX) {
		#else
		if (BMT_status.charger_vol >= v_charger_max) {
		#endif
			battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY]Charger over voltage !!\n");
			BMT_status.charger_protect_status = charger_OVER_VOL;
			BMT_status.bat_charging_state = CHR_ERROR;
			status = PMU_STATUS_FAIL;
		}
	}

	return status;
}


static PMU_STATUS mt_battery_CheckChargingTime(void)
{
	PMU_STATUS status = PMU_STATUS_OK;

	if ((g_battery_thermal_throttling_flag == 2) || (g_battery_thermal_throttling_flag == 3)) {
		battery_xlog_printk(BAT_LOG_FULL,
				    "[TestMode] Disable Safty Timer. bat_tt_enable=%d, bat_thr_test_mode=%d, bat_thr_test_value=%d\n",
				    g_battery_thermal_throttling_flag,
				    battery_cmd_thermal_test_mode,
				    battery_cmd_thermal_test_mode_value);

	} else {
		/* Charging OT */
		if (BMT_status.total_charging_time >= MAX_CHARGING_TIME) {
			battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] Charging Over Time.\n");

			status = PMU_STATUS_FAIL;
		}
	}

	return status;

}

#if defined(STOP_CHARGING_IN_TAKLING)
static PMU_STATUS mt_battery_CheckCallState(void)
{
	PMU_STATUS status = PMU_STATUS_OK;

	if ((g_call_state == CALL_ACTIVE) && (BMT_status.bat_vol > V_CC2TOPOFF_THRES))
		status = PMU_STATUS_FAIL;

	return status;
}
#endif

static void mt_battery_CheckBatteryStatus(void)
{
	battery_xlog_printk(BAT_LOG_FULL, "[mt_battery_CheckBatteryStatus] cmd_discharging=(%d)\n",
			    cmd_discharging);
	if (cmd_discharging == 1) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[mt_battery_CheckBatteryStatus] cmd_discharging=(%d)\n",
				    cmd_discharging);
		BMT_status.bat_charging_state = CHR_ERROR;
		battery_charging_control(CHARGING_CMD_SET_ERROR_STATE, &cmd_discharging);	
		return;
	} else if (cmd_discharging == 0) {
		BMT_status.bat_charging_state = CHR_PRE;
		battery_charging_control(CHARGING_CMD_SET_ERROR_STATE, &cmd_discharging);
		cmd_discharging = -1;
	}
	if (mt_battery_CheckBatteryTemp() != PMU_STATUS_OK) {
		BMT_status.bat_charging_state = CHR_ERROR;
		return;
	}

#if defined(CONFIG_AUSTIN_PROJECT)
	if (mt_battery_CheckBatteryConnect() != PMU_STATUS_OK) {
		BMT_status.bat_charging_state = CHR_ERROR;
		return;
	}
#endif

	if (mt_battery_CheckChargerVoltage() != PMU_STATUS_OK) {
		BMT_status.bat_charging_state = CHR_ERROR;
		return;
	}
#if defined(STOP_CHARGING_IN_TAKLING)
	if (mt_battery_CheckCallState() != PMU_STATUS_OK) {
		BMT_status.bat_charging_state = CHR_HOLD;
		return;
	}
#endif

	if (mt_battery_CheckChargingTime() != PMU_STATUS_OK) {
		BMT_status.bat_charging_state = CHR_ERROR;
		return;
	}
}


static void mt_battery_notify_TotalChargingTime_check(void)
{
#if defined(BATTERY_NOTIFY_CASE_0005_TOTAL_CHARGINGTIME)
	if ((g_battery_thermal_throttling_flag == 2) || (g_battery_thermal_throttling_flag == 3)) {
		battery_xlog_printk(BAT_LOG_FULL,
				    "[TestMode] Disable Safty Timer : no UI display\n");
	} else {
		if (BMT_status.total_charging_time >= MAX_CHARGING_TIME)
			/* if(BMT_status.total_charging_time >= 60) //test */
		{
			g_BatteryNotifyCode |= 0x0010;
			battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] Charging Over Time\n");
		} else {
			g_BatteryNotifyCode &= ~(0x0010);
		}
	}

	battery_xlog_printk(BAT_LOG_CRTI,
			    "[BATTERY] BATTERY_NOTIFY_CASE_0005_TOTAL_CHARGINGTIME (%x)\n",
			    g_BatteryNotifyCode);
#endif
}


static void mt_battery_notify_VBat_check(void)
{
#if defined(BATTERY_NOTIFY_CASE_0004_VBAT)
	if (BMT_status.bat_vol > 4350)
		/* if(BMT_status.bat_vol > 3800) //test */
	{
		g_BatteryNotifyCode |= 0x0008;
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] bat_vlot(%ld) > 4350mV\n",
				    BMT_status.bat_vol);
	} else {
		g_BatteryNotifyCode &= ~(0x0008);
	}

	battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] BATTERY_NOTIFY_CASE_0004_VBAT (%x)\n",
			    g_BatteryNotifyCode);

#endif
}


static void mt_battery_notify_ICharging_check(void)
{
#if defined(BATTERY_NOTIFY_CASE_0003_ICHARGING)
	if ((BMT_status.ICharging > 1000) && (BMT_status.total_charging_time > 300)) {
		g_BatteryNotifyCode |= 0x0004;
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] I_charging(%ld) > 1000mA\n",
				    BMT_status.ICharging);
	} else {
		g_BatteryNotifyCode &= ~(0x0004);
	}

	battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] BATTERY_NOTIFY_CASE_0003_ICHARGING (%x)\n",
			    g_BatteryNotifyCode);

#endif
}


static void mt_battery_notify_VBatTemp_check(void)
{
#if defined(BATTERY_NOTIFY_CASE_0002_VBATTEMP)

	if (BMT_status.temperature >= MAX_CHARGE_TEMPERATURE) {
		g_BatteryNotifyCode |= 0x0002;
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] bat_temp(%d) out of range(too high)\n",
				    BMT_status.temperature);
	}
#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
	else if (BMT_status.temperature < TEMP_NEG_10_THRESHOLD) {
		g_BatteryNotifyCode |= 0x0020;
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] bat_temp(%d) out of range(too low)\n",
				    BMT_status.temperature);
	}
#else
#ifdef BAT_LOW_TEMP_PROTECT_ENABLE
	else if (BMT_status.temperature < MIN_CHARGE_TEMPERATURE) {
		g_BatteryNotifyCode |= 0x0020;
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] bat_temp(%d) out of range(too low)\n",
				    BMT_status.temperature);
	}
#endif
#endif

	battery_xlog_printk(BAT_LOG_FULL, "[BATTERY] BATTERY_NOTIFY_CASE_0002_VBATTEMP (%x)\n",
			    g_BatteryNotifyCode);

#endif
}


static void mt_battery_notify_VCharger_check(void)
{
#if defined(BATTERY_NOTIFY_CASE_0001_VCHARGER)
	#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
	kal_uint32 v_charger_max = DISO_data.hv_voltage;
	#endif

	#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
	if (BMT_status.charger_vol > V_CHARGER_MAX) {
	#else
	if (BMT_status.charger_vol > v_charger_max) {
	#endif
		g_BatteryNotifyCode |= 0x0001;
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] BMT_status.charger_vol(%d) > %d mV\n",
				    BMT_status.charger_vol, V_CHARGER_MAX);
	} else {
		g_BatteryNotifyCode &= ~(0x0001);
	}
	if (g_BatteryNotifyCode != 0x0000)
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BATTERY] BATTERY_NOTIFY_CASE_0001_VCHARGER (%x)\n",
				    g_BatteryNotifyCode);
#endif
}


static void mt_battery_notify_UI_test(void)
{
	if (g_BN_TestMode == 0x0001) {
		g_BatteryNotifyCode = 0x0001;
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BATTERY_TestMode] BATTERY_NOTIFY_CASE_0001_VCHARGER\n");
	} else if (g_BN_TestMode == 0x0002) {
		g_BatteryNotifyCode = 0x0002;
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BATTERY_TestMode] BATTERY_NOTIFY_CASE_0002_VBATTEMP\n");
	} else if (g_BN_TestMode == 0x0003) {
		g_BatteryNotifyCode = 0x0004;
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BATTERY_TestMode] BATTERY_NOTIFY_CASE_0003_ICHARGING\n");
	} else if (g_BN_TestMode == 0x0004) {
		g_BatteryNotifyCode = 0x0008;
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BATTERY_TestMode] BATTERY_NOTIFY_CASE_0004_VBAT\n");
	} else if (g_BN_TestMode == 0x0005) {
		g_BatteryNotifyCode = 0x0010;
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BATTERY_TestMode] BATTERY_NOTIFY_CASE_0005_TOTAL_CHARGINGTIME\n");
	} else {
		battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] Unknown BN_TestMode Code : %x\n",
				    g_BN_TestMode);
	}
}


void mt_battery_notify_check(void)
{
	g_BatteryNotifyCode = 0x0000;

	if (g_BN_TestMode == 0x0000) {	/* for normal case */
		battery_xlog_printk(BAT_LOG_FULL, "[BATTERY] mt_battery_notify_check\n");

		mt_battery_notify_VCharger_check();

		mt_battery_notify_VBatTemp_check();

		mt_battery_notify_ICharging_check();

		mt_battery_notify_VBat_check();

		mt_battery_notify_TotalChargingTime_check();
	} else {		/* for UI test */

		mt_battery_notify_UI_test();
	}
}

static void mt_battery_thermal_check(void)
{
	if ((g_battery_thermal_throttling_flag == 1) || (g_battery_thermal_throttling_flag == 3)) {
		if (battery_cmd_thermal_test_mode == 1) {
			BMT_status.temperature = battery_cmd_thermal_test_mode_value;
			battery_xlog_printk(BAT_LOG_FULL,
					    "[Battery] In thermal_test_mode , Tbat=%d\n",
					    BMT_status.temperature);
		}
#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
		/* ignore default rule */
	if (BMT_status.temperature <= -10) {
		pr_notice("[Battery] Tbat(%d)<= -10, system need power down.\n", BMT_status.temperature);
		life_cycle_set_thermal_shutdown_reason(THERMAL_SHUTDOWN_REASON_BATTERY);
		orderly_poweroff(true);
	}
#else
		if (BMT_status.temperature >= 60) {
#if defined(CONFIG_POWER_EXT)
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[BATTERY] CONFIG_POWER_EXT, no update battery update power down.\n");
#else
			{
				if ((g_platform_boot_mode == META_BOOT)
				    || (g_platform_boot_mode == ADVMETA_BOOT)
				    || (g_platform_boot_mode == ATE_FACTORY_BOOT)) {
					battery_xlog_printk(BAT_LOG_FULL,
							    "[BATTERY] boot mode = %d, bypass temperature check\n",
							    g_platform_boot_mode);
				} else {
					struct battery_data *bat_data = &battery_main;
					struct power_supply *bat_psy = &bat_data->psy;

					battery_xlog_printk(BAT_LOG_CRTI,
							    "[Battery] Tbat(%d)>=60, system need power down.\n",
							    BMT_status.temperature);

					bat_data->BAT_CAPACITY = 0;

					power_supply_changed(bat_psy);

					if (BMT_status.charger_exist == KAL_TRUE) {
						/* can not power down due to charger exist, so need reset system */
						battery_charging_control
						    (CHARGING_CMD_SET_PLATFORM_RESET, NULL);
					}
					/* avoid SW no feedback */
					battery_charging_control(CHARGING_CMD_SET_POWER_OFF, NULL);
					/* mt_power_off(); */
				}
			}
#endif
		}
#endif

	}

}

#if defined(CONFIG_AMAZON_METRICS_LOG) && defined(CONFIG_AUSTIN_PROJECT)
static void battery_charge_metric(void)
{
	unsigned long virtual_temp = get_virtualsensor_temp();
	char buf[512];
	long elaps_sec;
	struct timespec diff;
	static unsigned int last_vol;
	static bool batt_metrics_first_run = true;
	static int battery_was_charging;
	static int battery_was_charging_ticks;
	static unsigned int usb_charger_voltage_previous;

	if (batt_metrics_first_run)
	{
		batt_metrics_first_run = false;

		/* Default data for first boot */
		metrics_battery_save_data();

	}

	/* Record Peak  Battery temperature during charge or discharge cycle */
	if (BMT_status.temperature > metrics_charge_info.battery_peak_battery_temp)
		metrics_charge_info.battery_peak_battery_temp = BMT_status.temperature;

	/* Record Peak Virtual temperature during charge or discharge cycle */
	if (virtual_temp > metrics_charge_info.battery_peak_virtual_temp)
		metrics_charge_info.battery_peak_virtual_temp = virtual_temp;

	if (metrics_charge_info.usb_charger_voltage_peak < BMT_status.charger_vol)
		metrics_charge_info.usb_charger_voltage_peak = BMT_status.charger_vol;

	/* Record Average Temperature Values */
	metrics_charge_info.battery_average_virtual_temp =(((metrics_charge_info.battery_average_virtual_temp * metrics_charge_info.battery_charge_ticks) + virtual_temp) / (metrics_charge_info.battery_charge_ticks + 1));
	metrics_charge_info.battery_average_battery_temp = ((metrics_charge_info.battery_average_battery_temp * metrics_charge_info.battery_charge_ticks) + BMT_status.temperature) / (metrics_charge_info.battery_charge_ticks + 1);

	/* Increase Charging Ticks */
	metrics_charge_info.battery_charge_ticks++;


	/* Check to see of Charging event Status has changed. Add a 6 cycle (1 min) de-bounce to prevent extra events from firing.  */
	if (battery_was_charging != battery_main.BAT_STATUS)
		battery_was_charging_ticks++;
	else {
		if (battery_was_charging_ticks > 0)
			battery_was_charging_ticks--;
		else
		{
			/* Save Last Voltage - Most of the time this voltage will be from Before Charge Started, or Before Charge Stopped. On average 8 seconds before. Since there is a few microscend delay  */
			/* between Actual charge start/Stop and FW knowing charge start/stop less then 1% of devices  */
			last_vol = BMT_status.bat_vol;
			usb_charger_voltage_previous = BMT_status.charger_vol;
		}
	}

	if (battery_was_charging_ticks > 5)
	{
		/* Reset ticks to 0; */
		battery_was_charging_ticks = 0;

		/* Calculate Elapsed Time */
		diff = timespec_sub(current_kernel_time(), metrics_charge_info.charger_time);
		elaps_sec = diff.tv_sec + diff.tv_nsec/NSEC_PER_SEC;

		/* Handle Case where Clock changes during Charging,
		* be very generous with timing as clock may be
		* more accurate then ticks.
		*/
		if (elaps_sec > metrics_charge_info.battery_charge_ticks * 30)
			elaps_sec = metrics_charge_info.battery_charge_ticks * 15;

		if (elaps_sec > 0 && elaps_sec < MAX_TIME_INTERVAL) {
			/* Log Metric for Charge / Discharge Cycle */
			snprintf(buf, sizeof(buf),
				"bq24296:def:Charger_Status=%d;CT;1,Elaps_Sec=%ld;CT;1,"
				"iVol=%d;CT;1,fVol=%d;CT;1,lVol=%d;CT;1,SOC=%d;CT;1,"
				"Bat_aTemp=%d;CT;1,Vir_aTemp=%d;CT;1,Bat_pTemp=%ld;CT;1,"
				"Vir_pTemp=%ld;CT;1,bTemp=%d;CT;1,Cycles=%d;CT;1,"
				"pVUsb=%ld;CT;1,fVUsb=%d;CT;1:NA", battery_was_charging,
				elaps_sec, metrics_charge_info.init_charging_vol,
				BMT_status.bat_vol, last_vol, BMT_status.SOC,
				(int)metrics_charge_info.battery_average_battery_temp,
				(int)metrics_charge_info.battery_average_virtual_temp,
				metrics_charge_info.battery_peak_battery_temp,
				metrics_charge_info.battery_peak_virtual_temp,
				BMT_status.temperature, gFG_battery_cycle,
				metrics_charge_info.usb_charger_voltage_peak,
				usb_charger_voltage_previous);

			log_to_metrics(ANDROID_LOG_INFO, "battery", buf);
			memset(buf, 0, sizeof(buf));
		}

		/*Save data for Next Metric */
		metrics_battery_save_data();

		/* Set Was charging Status */
		battery_was_charging = battery_main.BAT_STATUS;

	}
}
#endif

static void mt_battery_update_status(void)
{
#if defined(CONFIG_POWER_EXT)
	battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] CONFIG_POWER_EXT, no update Android.\n");
#else
	{
		wireless_update(&wireless_main);
		battery_update(&battery_main);
		ac_update(&ac_main);
		usb_update(&usb_main);
#if defined(CONFIG_AMAZON_METRICS_LOG)
		metrics_charger_update(ac_main.AC_ONLINE, usb_main.USB_ONLINE);
#ifdef CONFIG_AUSTIN_PROJECT
		battery_charge_metric();
#endif
#endif
	}

#endif
}


CHARGER_TYPE mt_charger_type_detection(void)
{
	CHARGER_TYPE CHR_Type_num = CHARGER_UNKNOWN;

	mutex_lock(&charger_type_mutex);

#if defined(CONFIG_MTK_WIRELESS_CHARGER_SUPPORT)
	battery_charging_control(CHARGING_CMD_GET_CHARGER_TYPE, &CHR_Type_num);
	BMT_status.charger_type = CHR_Type_num;
#else
	#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
	if (BMT_status.charger_type == CHARGER_UNKNOWN) {
	#else
	if ((BMT_status.charger_type == CHARGER_UNKNOWN) &&
	    (DISO_data.diso_state.cur_vusb_state == DISO_ONLINE)) {
	#endif
		battery_charging_control(CHARGING_CMD_GET_CHARGER_TYPE, &CHR_Type_num);
		BMT_status.charger_type = CHR_Type_num;

	getrawmonotonic(&chr_plug_in_time);
	g_custom_plugin_time = 0;
	g_custom_charging_cv = -1;
	battery_xlog_printk(BAT_LOG_FULL, "[%s]init charger plug-in timer\n", __func__);

#if defined(CONFIG_MTK_KERNEL_POWER_OFF_CHARGING)&&(defined(CONFIG_MTK_PUMP_EXPRESS_SUPPORT) || defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT))
 	    if (BMT_status.UI_SOC == 100)
		{
			BMT_status.bat_charging_state = CHR_BATFULL;
			BMT_status.bat_full = KAL_TRUE;
			g_charging_full_reset_bat_meter = KAL_TRUE;
		}

		 if(g_battery_soc_ready == KAL_FALSE) {
			if(BMT_status.nPercent_ZCV == 0)
				battery_meter_initial();

			BMT_status.SOC = battery_meter_get_battery_percentage();
		}

		if (BMT_status.bat_vol > 0)
			mt_battery_update_status();
#endif
	}
#endif
	mutex_unlock(&charger_type_mutex);

	return BMT_status.charger_type;
}

CHARGER_TYPE mt_get_charger_type(void)
{
#if defined(CONFIG_POWER_EXT) || defined(CONFIG_MTK_FPGA)
    return STANDARD_HOST;
#else
	return BMT_status.charger_type;
#endif
}

static void mt_battery_charger_detect_check(void)
{
	if (upmu_is_chr_det() == KAL_TRUE) {
		wake_lock(&battery_suspend_lock);

		#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
		BMT_status.charger_exist = KAL_TRUE;
		#endif

#if defined(CONFIG_MTK_WIRELESS_CHARGER_SUPPORT)
		mt_charger_type_detection();

		if ((BMT_status.charger_type == STANDARD_HOST)
		    || (BMT_status.charger_type == CHARGING_HOST)) {
			mt_usb_connect();
		}
#else
		#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
		if (BMT_status.charger_type == CHARGER_UNKNOWN) {
		#else
		if ((BMT_status.charger_type == CHARGER_UNKNOWN) &&
		    (DISO_data.diso_state.cur_vusb_state == DISO_ONLINE)) {
		#endif

			mt_charger_type_detection();

			if ((BMT_status.charger_type == STANDARD_HOST)
			    || (BMT_status.charger_type == CHARGING_HOST)) {
				mt_usb_connect();
			}
		}
#endif

		battery_xlog_printk(BAT_LOG_CRTI, "[BAT_thread]Cable in, CHR_Type_num=%d\n",
				    BMT_status.charger_type);

	} else {
		wake_unlock(&battery_suspend_lock);

		BMT_status.charger_exist = KAL_FALSE;
		BMT_status.charger_type = CHARGER_UNKNOWN;
		BMT_status.bat_full = KAL_FALSE;
		BMT_status.bat_in_recharging_state = KAL_FALSE;
		BMT_status.bat_charging_state = CHR_PRE;
		BMT_status.total_charging_time = 0;
		BMT_status.PRE_charging_time = 0;
		BMT_status.CC_charging_time = 0;
		BMT_status.TOPOFF_charging_time = 0;
		BMT_status.POSTFULL_charging_time = 0;

		battery_xlog_printk(BAT_LOG_CRTI, "[BAT_thread]Cable out \n");

		mt_usb_disconnect();
	}
}

static void mt_kpoc_power_off_check(void)
{
#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
	battery_xlog_printk(BAT_LOG_CRTI,
			    "chr_vol=%d, boot_mode=%d\n", BMT_status.charger_vol,
			    g_platform_boot_mode);
	if (g_platform_boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT
	    || g_platform_boot_mode == LOW_POWER_OFF_CHARGING_BOOT) {
		if ((upmu_is_chr_det() == KAL_FALSE) && (BMT_status.charger_vol < 2500))	/* vbus < 2.5V */
		{
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[bat_thread_kthread] Unplug Charger/USB In Kernel Power Off Charging Mode!  Shutdown OS!\n");
			battery_charging_control(CHARGING_CMD_SET_POWER_OFF, NULL);
		}
	}
#endif
}

void update_battery_2nd_info(int status_smb, int capacity_smb, int present_smb)
{
#if defined(CONFIG_POWER_VERIFY)
	battery_xlog_printk(BAT_LOG_CRTI, "[update_battery_smb_info] no support\n");
#else
	g_status_smb = status_smb;
	g_capacity_smb = capacity_smb;
	g_present_smb = present_smb;
	battery_xlog_printk(BAT_LOG_CRTI,
			    "[update_battery_smb_info] get status_smb=%d,capacity_smb=%d,present_smb=%d\n",
			    status_smb, capacity_smb, present_smb);

	wake_up_bat();
	g_smartbook_update = 1;
#endif
}

void do_chrdet_int_task(void)
{
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[128] = {0};
#endif

	if (g_bat_init_flag == KAL_TRUE) {
		#if !defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
		if (upmu_is_chr_det() == KAL_TRUE) {
		#else
		battery_charging_control(CHARGING_CMD_GET_DISO_STATE, &DISO_data);
		if ((DISO_data.diso_state.cur_vusb_state == DISO_ONLINE) ||
		    (DISO_data.diso_state.cur_vdc_state == DISO_ONLINE)) {
		#endif
			battery_xlog_printk(BAT_LOG_CRTI, "[do_chrdet_int_task] charger exist!\n");
			BMT_status.charger_exist = KAL_TRUE;

#ifdef CONFIG_AMAZON_METRICS_LOG
			snprintf(buf, sizeof(buf),
				"%s:bq24296:vbus_on=1;CT;1;DV;1:NR",
				__func__);
			log_to_metrics(ANDROID_LOG_INFO, "USBCableEvent", buf);
#endif

			wake_lock(&battery_suspend_lock);

#if defined(CONFIG_POWER_EXT)
			mt_usb_connect();
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[do_chrdet_int_task] call mt_usb_connect() in EVB\n");
#elif defined(CONFIG_MTK_POWER_EXT_DETECT)
			if (KAL_TRUE == bat_is_ext_power()) {
				mt_usb_connect();
				battery_xlog_printk(BAT_LOG_CRTI,
						    "[do_chrdet_int_task] call mt_usb_connect() in EVB\n");
				return;
			}
#endif
		} else {
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[do_chrdet_int_task] charger NOT exist!\n");
			BMT_status.charger_exist = KAL_FALSE;

#ifdef CONFIG_AMAZON_METRICS_LOG
			memset(buf, '\0', sizeof(buf));
			snprintf(buf, sizeof(buf),
				"%s:bq24296:vbus_off=1;CT;1;DV;1:NR",
				__func__);
			log_to_metrics(ANDROID_LOG_INFO, "USBCableEvent", buf);
#endif

			#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
			battery_xlog_printk(BAT_LOG_CRTI,
					    "turn off charging for no avaliable charging source\n");
			battery_charging_control(CHARGING_CMD_ENABLE,&BMT_status.charger_exist);
			#endif

#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
			if (g_platform_boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT
			    || g_platform_boot_mode == LOW_POWER_OFF_CHARGING_BOOT) {
				battery_xlog_printk(BAT_LOG_CRTI,
						    "[pmic_thread_kthread] Unplug Charger/USB In Kernel Power Off Charging Mode!  Shutdown OS!\n");
				battery_charging_control(CHARGING_CMD_SET_POWER_OFF, NULL);
				/* mt_power_off(); */
			}
#endif

			wake_unlock(&battery_suspend_lock);

#if defined(CONFIG_POWER_EXT)
			mt_usb_disconnect();
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[do_chrdet_int_task] call mt_usb_disconnect() in EVB\n");
#elif defined(CONFIG_MTK_POWER_EXT_DETECT)
			if (KAL_TRUE == bat_is_ext_power()) {
				mt_usb_disconnect();
				battery_xlog_printk(BAT_LOG_CRTI,
						    "[do_chrdet_int_task] call mt_usb_disconnect() in EVB\n");
				return;
			}
#endif
			#if defined(CONFIG_MTK_PUMP_EXPRESS_SUPPORT) || defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
				 is_ta_connect = KAL_FALSE;
				 ta_check_chr_type = KAL_TRUE;
				 ta_cable_out_occur = KAL_TRUE;
			#endif

		}

		/* Place charger detection and battery update here is used to speed up charging icon display. */

		mt_battery_charger_detect_check();

#ifdef CONFIG_AMAZON_METRICS_LOG
		memset(buf, '\0', sizeof(buf));
		if (BMT_status.charger_type > CHARGER_UNKNOWN) {
			snprintf(buf, sizeof(buf),
				"%s:bq24296:detect=1;CT;1;type=%d;DV;1:NR",
				__func__, BMT_status.charger_type);
			log_to_metrics(ANDROID_LOG_INFO, "USBCableEvent", buf);
		}
#endif

		if (BMT_status.UI_SOC == 100 && BMT_status.charger_exist == KAL_TRUE) {
			BMT_status.bat_charging_state = CHR_BATFULL;
			BMT_status.bat_full = KAL_TRUE;
			g_charging_full_reset_bat_meter = KAL_TRUE;
		}

		if (g_battery_soc_ready == KAL_FALSE) {
			if (BMT_status.nPercent_ZCV == 0)
				battery_meter_initial();

			BMT_status.SOC = battery_meter_get_battery_percentage();
		}

		if (BMT_status.bat_vol > 0) {
			mt_battery_update_status();
		}

		#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
		DISO_data.chr_get_diso_state = KAL_TRUE;
		#endif

		wake_up_bat();
	} else {
		#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
		g_vcdt_irq_delay_flag  = KAL_TRUE;
		#endif
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[do_chrdet_int_task] battery thread not ready, will do after bettery init.\n");
	}

}


void BAT_thread(void)
{
	static kal_bool battery_meter_initilized = KAL_FALSE;
	struct timespec now_time;
	unsigned long total_time_plug_in;
#if defined(CONFIG_AMAZON_METRICS_LOG) && defined(CONFIG_AUSTIN_PROJECT)
	char buf[256] = {0};
	unsigned long virtual_temp = get_virtualsensor_temp();
	static bool bat_14days_flag;
	static bool bat_demo_flag;
#endif

	if (battery_meter_initilized == KAL_FALSE) {
		battery_meter_initial();	/* move from battery_probe() to decrease booting time */
		BMT_status.nPercent_ZCV = battery_meter_get_battery_nPercent_zcv();
		battery_meter_initilized = KAL_TRUE;

	}

	mt_battery_charger_detect_check();
	mt_battery_GetBatteryData();
	if (BMT_status.charger_exist == KAL_TRUE) {
		check_battery_exist();
	}
	mt_battery_thermal_check();
	mt_battery_notify_check();

	if (BMT_status.charger_exist == KAL_TRUE) {
		getrawmonotonic(&now_time);

		total_time_plug_in = g_custom_plugin_time;
		if ((now_time.tv_sec - chr_plug_in_time.tv_sec) > 0)
			total_time_plug_in += now_time.tv_sec - chr_plug_in_time.tv_sec;

		if (total_time_plug_in > PLUGIN_THRESHOLD) {
			g_custom_charging_cv = BATTERY_VOLT_04_100000_V;
#if defined(CONFIG_AMAZON_METRICS_LOG) && defined(CONFIG_AUSTIN_PROJECT)
			if (!bat_14days_flag) {
				bat_14days_flag = true;
				snprintf(buf, sizeof(buf),
				"bq24296:def:Charing_Over_14days=%d;CT;1,Total_Plug_Time=%ld;CT;1,"
				"Bat_Vol=%d;CT;1,UI_SOC=%d;CT;1,SOC=%d;CT;1,Bat_Temp=%d;CT;1,"
				"Vir_Avg_Temp=%ld;CT;1,Bat_Cycle_Count=%d;CT;1:NA",
				1, total_time_plug_in, BMT_status.bat_vol, BMT_status.UI_SOC, BMT_status.SOC, BMT_status.temperature,
				virtual_temp, gFG_battery_cycle);

				log_to_metrics(ANDROID_LOG_INFO, "battery", buf);
				memset(buf, 0, 256);
			}

			if (total_time_plug_in <= PLUGIN_THRESHOLD && bat_14days_flag)
				bat_14days_flag = false;
#endif
		}
		else
			g_custom_charging_cv = -1;

#if defined(CONFIG_AMAZON_METRICS_LOG) && defined(CONFIG_AUSTIN_PROJECT)
		if (g_custom_charging_mode == 1 && !bat_demo_flag) {
			bat_demo_flag = true;
			snprintf(buf, sizeof(buf),
			"bq24296:def:Store_Demo_Mode=%d;CT;1,Total_Plug_Time=%ld;CT;1,"
			"Bat_Vol=%d;CT;1,UI_SOC=%d;CT;1,SOC=%d;CT;1,Bat_Temp=%d;CT;1,"
			"Vir_Avg_Temp=%ld;CT;1,Bat_Cycle_Count=%d;CT;1:NA",
			g_custom_charging_mode, total_time_plug_in, BMT_status.bat_vol, BMT_status.UI_SOC, BMT_status.SOC, BMT_status.temperature,
			virtual_temp, gFG_battery_cycle);

			log_to_metrics(ANDROID_LOG_INFO, "battery", buf);

		}

		if (g_custom_charging_mode != 1 && bat_demo_flag)
			bat_demo_flag = false;
#endif
		battery_xlog_printk(BAT_LOG_FULL, "total plug-in time(%lu), cv(%d)\r\n",
			total_time_plug_in, g_custom_charging_cv);

		mt_battery_CheckBatteryStatus();
		mt_battery_charging_algorithm();
	}

	mt_battery_update_status();
	mt_kpoc_power_off_check();
}

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Internal API */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
int bat_thread_kthread(void *x)
{
	ktime_t ktime = ktime_set(3, 0);	/* 10s, 10* 1000 ms */

	/* Run on a process content */
	while (1) {
		mutex_lock(&bat_mutex);

		if ((chargin_hw_init_done == KAL_TRUE) && (battery_suspended == KAL_FALSE))
			BAT_thread();

		mutex_unlock(&bat_mutex);

		battery_xlog_printk(BAT_LOG_FULL, "wait event \n" );

		wait_event(bat_thread_wq, (bat_thread_timeout == KAL_TRUE));

		bat_thread_timeout = KAL_FALSE;
		hrtimer_start(&battery_kthread_timer, ktime, HRTIMER_MODE_REL);
		ktime = ktime_set(BAT_TASK_PERIOD, 0);	/* 10s, 10* 1000 ms */
		if (chr_wake_up_bat == KAL_TRUE && g_smartbook_update != 1)	/* for charger plug in/ out */
		{
			#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
			if (DISO_data.chr_get_diso_state) {
				DISO_data.chr_get_diso_state = KAL_FALSE;
				battery_charging_control(CHARGING_CMD_GET_DISO_STATE, &DISO_data);
			}
			#endif

			g_smartbook_update = 0;
			battery_meter_reset(KAL_FALSE);
			chr_wake_up_bat = KAL_FALSE;

			battery_xlog_printk(BAT_LOG_CRTI,
					    "[BATTERY] Charger plug in/out, Call battery_meter_reset. (%d)\n",
					    BMT_status.UI_SOC);
		}

	}

	return 0;
}

void bat_thread_wakeup(void)
{
	battery_xlog_printk(BAT_LOG_FULL, "******** battery : bat_thread_wakeup  ********\n");

	bat_thread_timeout = KAL_TRUE;
	bat_meter_timeout = KAL_TRUE;
#ifdef MTK_ENABLE_AGING_ALGORITHM
    suspend_time = 0;
#endif
    _g_bat_sleep_total_time = 0;
	wake_up(&bat_thread_wq);
}

#if defined(CONFIG_AMAZON_METRICS_LOG)
static void metrics_resume(void)
{
	struct battery_info *info = &BQ_info;

	/* invalidate all the measurements */
	mutex_lock(&info->lock);
	info->flags |= BQ_STATUS_RESUMING;
	mutex_unlock(&info->lock);
	bat_thread_wakeup();
}
#endif

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // fop API */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static long adc_cali_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int *user_data_addr;
	int *naram_data_addr;
	int i = 0;
	int ret = 0;
	int adc_in_data[2] = { 1, 1 };
	int adc_out_data[2] = { 1, 1 };

	mutex_lock(&bat_mutex);

	switch (cmd) {
	case TEST_ADC_CALI_PRINT:
		g_ADC_Cali = KAL_FALSE;
		break;

	case SET_ADC_CALI_Slop:
		naram_data_addr = (int *)arg;
		ret = copy_from_user(adc_cali_slop, naram_data_addr, 36);
		g_ADC_Cali = KAL_FALSE;	/* enable calibration after setting ADC_CALI_Cal */
		/* Protection */
		for (i = 0; i < 14; i++) {
			if ((*(adc_cali_slop + i) == 0) || (*(adc_cali_slop + i) == 1)) {
				*(adc_cali_slop + i) = 1000;
			}
		}
		for (i = 0; i < 14; i++)
			battery_xlog_printk(BAT_LOG_CRTI, "adc_cali_slop[%d] = %d\n", i,
					    *(adc_cali_slop + i));
		battery_xlog_printk(BAT_LOG_FULL,
				    "**** unlocked_ioctl : SET_ADC_CALI_Slop Done!\n");
		break;

	case SET_ADC_CALI_Offset:
		naram_data_addr = (int *)arg;
		ret = copy_from_user(adc_cali_offset, naram_data_addr, 36);
		g_ADC_Cali = KAL_FALSE;	/* enable calibration after setting ADC_CALI_Cal */
		for (i = 0; i < 14; i++)
			battery_xlog_printk(BAT_LOG_CRTI, "adc_cali_offset[%d] = %d\n", i,
					    *(adc_cali_offset + i));
		battery_xlog_printk(BAT_LOG_FULL,
				    "**** unlocked_ioctl : SET_ADC_CALI_Offset Done!\n");
		break;

	case SET_ADC_CALI_Cal:
		naram_data_addr = (int *)arg;
		ret = copy_from_user(adc_cali_cal, naram_data_addr, 4);
		g_ADC_Cali = KAL_TRUE;
		if (adc_cali_cal[0] == 1) {
			g_ADC_Cali = KAL_TRUE;
		} else {
			g_ADC_Cali = KAL_FALSE;
		}
		for (i = 0; i < 1; i++)
			battery_xlog_printk(BAT_LOG_CRTI, "adc_cali_cal[%d] = %d\n", i,
					    *(adc_cali_cal + i));
		battery_xlog_printk(BAT_LOG_FULL, "**** unlocked_ioctl : SET_ADC_CALI_Cal Done!\n");
		break;

	case ADC_CHANNEL_READ:
		/* g_ADC_Cali = KAL_FALSE; */ /* 20100508 Infinity */
		user_data_addr = (int *)arg;
		ret = copy_from_user(adc_in_data, user_data_addr, 8);	/* 2*int = 2*4 */

		if (adc_in_data[0] == 0)	/* I_SENSE */
		{
			adc_out_data[0] = battery_meter_get_VSense() * adc_in_data[1];
		} else if (adc_in_data[0] == 1)	/* BAT_SENSE */
		{
			adc_out_data[0] = battery_meter_get_battery_voltage(KAL_TRUE) * adc_in_data[1];
		} else if (adc_in_data[0] == 3)	/* V_Charger */
		{
			adc_out_data[0] = battery_meter_get_charger_voltage() * adc_in_data[1];
			/* adc_out_data[0] = adc_out_data[0] / 100; */
		} else if (adc_in_data[0] == 30)	/* V_Bat_temp magic number */
		{
			adc_out_data[0] = battery_meter_get_battery_temperature() * adc_in_data[1];
		} else if (adc_in_data[0] == 66) {
			adc_out_data[0] = (battery_meter_get_battery_current()) / 10;

			if (battery_meter_get_battery_current_sign() == KAL_TRUE) {
				adc_out_data[0] = 0 - adc_out_data[0];	/* charging */
			}
		} else {
			battery_xlog_printk(BAT_LOG_FULL, "unknown channel(%d,%d)\n",
					    adc_in_data[0], adc_in_data[1]);
		}

		if (adc_out_data[0] < 0)
			adc_out_data[1] = 1;	/* failed */
		else
			adc_out_data[1] = 0;	/* success */

		if (adc_in_data[0] == 30)
			adc_out_data[1] = 0;	/* success */

		if (adc_in_data[0] == 66)
			adc_out_data[1] = 0;	/* success */

		ret = copy_to_user(user_data_addr, adc_out_data, 8);
		battery_xlog_printk(BAT_LOG_CRTI,
				    "**** unlocked_ioctl : Channel %d * %d times = %d\n",
				    adc_in_data[0], adc_in_data[1], adc_out_data[0]);
		break;

	case BAT_STATUS_READ:
		user_data_addr = (int *)arg;
		ret = copy_from_user(battery_in_data, user_data_addr, 4);
		/* [0] is_CAL */
		if (g_ADC_Cali) {
			battery_out_data[0] = 1;
		} else {
			battery_out_data[0] = 0;
		}
		ret = copy_to_user(user_data_addr, battery_out_data, 4);
		battery_xlog_printk(BAT_LOG_CRTI, "**** unlocked_ioctl : CAL:%d\n",
				    battery_out_data[0]);
		break;

	case Set_Charger_Current:	/* For Factory Mode */
		user_data_addr = (int *)arg;
		ret = copy_from_user(charging_level_data, user_data_addr, 4);
		g_ftm_battery_flag = KAL_TRUE;
		if (charging_level_data[0] == 0) {
			charging_level_data[0] = CHARGE_CURRENT_70_00_MA;
		} else if (charging_level_data[0] == 1) {
			charging_level_data[0] = CHARGE_CURRENT_200_00_MA;
		} else if (charging_level_data[0] == 2) {
			charging_level_data[0] = CHARGE_CURRENT_400_00_MA;
		} else if (charging_level_data[0] == 3) {
			charging_level_data[0] = CHARGE_CURRENT_450_00_MA;
		} else if (charging_level_data[0] == 4) {
			charging_level_data[0] = CHARGE_CURRENT_550_00_MA;
		} else if (charging_level_data[0] == 5) {
			charging_level_data[0] = CHARGE_CURRENT_650_00_MA;
		} else if (charging_level_data[0] == 6) {
			charging_level_data[0] = CHARGE_CURRENT_700_00_MA;
		} else if (charging_level_data[0] == 7) {
			charging_level_data[0] = CHARGE_CURRENT_800_00_MA;
		} else if (charging_level_data[0] == 8) {
			charging_level_data[0] = CHARGE_CURRENT_900_00_MA;
		} else if (charging_level_data[0] == 9) {
			charging_level_data[0] = CHARGE_CURRENT_1000_00_MA;
		} else if (charging_level_data[0] == 10) {
			charging_level_data[0] = CHARGE_CURRENT_1100_00_MA;
		} else if (charging_level_data[0] == 11) {
			charging_level_data[0] = CHARGE_CURRENT_1200_00_MA;
		} else if (charging_level_data[0] == 12) {
			charging_level_data[0] = CHARGE_CURRENT_1300_00_MA;
		} else if (charging_level_data[0] == 13) {
			charging_level_data[0] = CHARGE_CURRENT_1400_00_MA;
		} else if (charging_level_data[0] == 14) {
			charging_level_data[0] = CHARGE_CURRENT_1500_00_MA;
		} else if (charging_level_data[0] == 15) {
			charging_level_data[0] = CHARGE_CURRENT_1600_00_MA;
		} else {
			charging_level_data[0] = CHARGE_CURRENT_450_00_MA;
		}
		wake_up_bat();
		battery_xlog_printk(BAT_LOG_CRTI, "**** unlocked_ioctl : set_Charger_Current:%d\n",
				    charging_level_data[0]);
		break;
		/* add for meta tool------------------------------- */
	case Get_META_BAT_VOL:
		user_data_addr = (int *)arg;
		ret = copy_from_user(adc_in_data, user_data_addr, 8);
		adc_out_data[0] = BMT_status.bat_vol;
		ret = copy_to_user(user_data_addr, adc_out_data, 8);

		break;
	case Get_META_BAT_SOC:
		user_data_addr = (int *)arg;
		ret = copy_from_user(adc_in_data, user_data_addr, 8);
		adc_out_data[0] = BMT_status.UI_SOC;
		ret = copy_to_user(user_data_addr, adc_out_data, 8);

		break;
		/* add bing meta tool------------------------------- */

	default :
		g_ADC_Cali = KAL_FALSE;
		break;
	}

	mutex_unlock(&bat_mutex);

	return 0;
}

static int adc_cali_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int adc_cali_release(struct inode *inode, struct file *file)
{
	return 0;
}


static struct file_operations adc_cali_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = adc_cali_ioctl,
	.open = adc_cali_open,
	.release = adc_cali_release,
};


void check_battery_exist(void)
{
#if defined(CONFIG_DIS_CHECK_BATTERY)
	battery_xlog_printk(BAT_LOG_CRTI, "[BATTERY] Disable check battery exist.\n");
#else
	kal_uint32 baton_count = 0;
	kal_uint32 charging_enable = KAL_FALSE;
	kal_uint32 battery_status;
	kal_uint32 i;

	for (i = 0; i < 3; i++) {
		battery_charging_control(CHARGING_CMD_GET_BATTERY_STATUS, &battery_status);
		baton_count += battery_status;

	}

	if (baton_count >= 3) {
		if ((g_platform_boot_mode == META_BOOT) || (g_platform_boot_mode == ADVMETA_BOOT)
		    || (g_platform_boot_mode == ATE_FACTORY_BOOT)) {
			battery_xlog_printk(BAT_LOG_FULL,
					    "[BATTERY] boot mode = %d, bypass battery check\n",
					    g_platform_boot_mode);
		} else {
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[BATTERY] Battery is not exist, power off FAN5405 and system (%d)\n",
					    baton_count);

			battery_charging_control(CHARGING_CMD_ENABLE, &charging_enable);
			battery_charging_control(CHARGING_CMD_SET_POWER_OFF, NULL);
		}
	}
#endif
}


int charger_hv_detect_sw_thread_handler(void *unused)
{
	ktime_t ktime;
	kal_uint32 charging_enable;
	kal_uint32 hv_voltage = V_CHARGER_MAX*1000;
	kal_bool hv_status;

	#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
	hv_voltage = DISO_data.hv_voltage;
	#endif

	do {
		ktime = ktime_set(0, BAT_MS_TO_NS(1000));

		if (chargin_hw_init_done)
			battery_charging_control(CHARGING_CMD_SET_HV_THRESHOLD, &hv_voltage);

		wait_event_interruptible(charger_hv_detect_waiter,
					 (charger_hv_detect_flag == KAL_TRUE));

		if ((upmu_is_chr_det() == KAL_TRUE)) {
			check_battery_exist();
		}

		charger_hv_detect_flag = KAL_FALSE;

		if (chargin_hw_init_done)
			battery_charging_control(CHARGING_CMD_GET_HV_STATUS, &hv_status);

		if (hv_status == KAL_TRUE) {
			battery_xlog_printk(BAT_LOG_CRTI,
					    "[charger_hv_detect_sw_thread_handler] charger hv\n");

			charging_enable = KAL_FALSE;
			if (chargin_hw_init_done)
				battery_charging_control(CHARGING_CMD_ENABLE, &charging_enable);
		} else {
			battery_xlog_printk(BAT_LOG_FULL,
					    "[charger_hv_detect_sw_thread_handler] upmu_chr_get_vcdt_hv_det() != 1\n");
		}

		if (chargin_hw_init_done)
			battery_charging_control(CHARGING_CMD_RESET_WATCH_DOG_TIMER, NULL);

		hrtimer_start(&charger_hv_detect_timer, ktime, HRTIMER_MODE_REL);

	} while (!kthread_should_stop());

	return 0;
}

enum hrtimer_restart charger_hv_detect_sw_workaround(struct hrtimer *timer)
{
	charger_hv_detect_flag = KAL_TRUE;
	wake_up_interruptible(&charger_hv_detect_waiter);

	battery_xlog_printk(BAT_LOG_FULL, "[charger_hv_detect_sw_workaround]\n");

	return HRTIMER_NORESTART;
}

void charger_hv_detect_sw_workaround_init(void)
{
	ktime_t ktime;

	ktime = ktime_set(0, BAT_MS_TO_NS(2000));
	hrtimer_init(&charger_hv_detect_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	charger_hv_detect_timer.function = charger_hv_detect_sw_workaround;
	hrtimer_start(&charger_hv_detect_timer, ktime, HRTIMER_MODE_REL);

	charger_hv_detect_thread =
	    kthread_run(charger_hv_detect_sw_thread_handler, 0,
			"mtk charger_hv_detect_sw_workaround");
	if (IS_ERR(charger_hv_detect_thread)) {
		battery_xlog_printk(BAT_LOG_FULL,
				    "[%s]: failed to create charger_hv_detect_sw_workaround thread\n",
				    __func__);
	}
	check_battery_exist();
	battery_xlog_printk(BAT_LOG_CRTI, "charger_hv_detect_sw_workaround_init : done\n");
}


enum hrtimer_restart battery_kthread_hrtimer_func(struct hrtimer *timer)
{
	bat_thread_wakeup();

	return HRTIMER_NORESTART;
}

void battery_kthread_hrtimer_init(void)
{
	ktime_t ktime;

	ktime = ktime_set(1, 0);	/* 3s, 10* 1000 ms */
	hrtimer_init(&battery_kthread_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	battery_kthread_timer.function = battery_kthread_hrtimer_func;
	hrtimer_start(&battery_kthread_timer, ktime, HRTIMER_MODE_REL);

	battery_xlog_printk(BAT_LOG_CRTI, "battery_kthread_hrtimer_init : done\n");
}


static void get_charging_control(void)
{
#if defined(CONFIG_MTK_BQ24296_SUPPORT) && defined(CONFIG_MTK_SN2871_SUPPORT)
	if (bq24296_is_found == KAL_TRUE)
		battery_charging_control = chr_control_interface_bq24296;
	else if (sn2871_is_found == KAL_TRUE)
		battery_charging_control = chr_control_interface_sn2871;
	else
		battery_charging_control = chr_control_interface_bq24296;
#elif defined(CONFIG_MTK_BQ24296_SUPPORT)
		battery_charging_control = chr_control_interface_bq24296;
#endif
}

#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
static irqreturn_t diso_auxadc_irq_thread(int irq, void *dev_id)
{
	int pre_diso_state = (DISO_data.diso_state.pre_otg_state |
		(DISO_data.diso_state.pre_vusb_state << 1) |
		(DISO_data.diso_state.pre_vdc_state << 2)) & 0x7;

	battery_xlog_printk(BAT_LOG_CRTI,
			    "[DISO]auxadc IRQ threaded handler triggered, pre_diso_state is %s\n",
			    DISO_state_s[pre_diso_state]);

	switch (pre_diso_state) {
	#ifdef MTK_DISCRETE_SWITCH /*for DSC DC plugin handle */
	case USB_ONLY:
	#endif
	case OTG_ONLY:
		BMT_status.charger_exist = KAL_TRUE;
		wake_lock(&battery_suspend_lock);
		wake_up_bat();
		break;
	case DC_WITH_OTG:
		BMT_status.charger_exist = KAL_FALSE;
		battery_charging_control(CHARGING_CMD_ENABLE,&BMT_status.charger_exist); // need stop charger quickly
		BMT_status.charger_exist = KAL_FALSE; //reset charger status
		BMT_status.charger_type = CHARGER_UNKNOWN;
		wake_unlock(&battery_suspend_lock);
		wake_up_bat();
		break;
	case DC_WITH_USB:
		/*usb delayed work will reflact BMT_staus , so need update state ASAP*/
		if((BMT_status.charger_type==STANDARD_HOST) || (BMT_status.charger_type==CHARGING_HOST))
			mt_usb_disconnect(); /* disconnect if connected */
		BMT_status.charger_type = CHARGER_UNKNOWN;// reset chr_type
		break;
	case DC_ONLY:
		BMT_status.charger_type = CHARGER_UNKNOWN;
		mt_battery_charger_detect_check(); //plug in VUSB, check if need connect usb
		break;
	default:
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[DISO]VUSB auxadc threaded handler triggered ERROR OR TEST\n");
		break;
	}
	return IRQ_HANDLED;
}

static void dual_input_init(void)
{
	DISO_data.irq_callback_func = diso_auxadc_irq_thread;
	battery_charging_control(CHARGING_CMD_DISO_INIT, &DISO_data);
}
#endif
static int battery_probe(struct platform_device *dev)
{
	struct class_device *class_dev = NULL;
	int ret = 0;

	battery_xlog_printk(BAT_LOG_CRTI, "******** battery driver probe!! ********\n");

	/* Integrate with NVRAM */
	ret = alloc_chrdev_region(&adc_cali_devno, 0, 1, ADC_CALI_DEVNAME);
	if (ret)
		battery_xlog_printk(BAT_LOG_CRTI, "Error: Can't Get Major number for adc_cali\n");
	adc_cali_cdev = cdev_alloc();
	adc_cali_cdev->owner = THIS_MODULE;
	adc_cali_cdev->ops = &adc_cali_fops;
	ret = cdev_add(adc_cali_cdev, adc_cali_devno, 1);
	if (ret)
		battery_xlog_printk(BAT_LOG_CRTI, "adc_cali Error: cdev_add\n");
	adc_cali_major = MAJOR(adc_cali_devno);
	adc_cali_class = class_create(THIS_MODULE, ADC_CALI_DEVNAME);
	class_dev = (struct class_device *)device_create(adc_cali_class,
							 NULL,
							 adc_cali_devno, NULL, ADC_CALI_DEVNAME);
	battery_xlog_printk(BAT_LOG_CRTI, "[BAT_probe] adc_cali prepare : done !!\n ");

	get_charging_control();

	battery_charging_control(CHARGING_CMD_GET_PLATFORM_BOOT_MODE, &g_platform_boot_mode);
	battery_xlog_printk(BAT_LOG_CRTI, "[BAT_probe] g_platform_boot_mode = %d\n ",
			    g_platform_boot_mode);

	wake_lock_init(&battery_suspend_lock, WAKE_LOCK_SUSPEND, "battery suspend wakelock");
	#if defined(CONFIG_MTK_PUMP_EXPRESS_SUPPORT) || defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
	wake_lock_init(&TA_charger_suspend_lock, WAKE_LOCK_SUSPEND, "TA charger suspend wakelock");  
	#endif

	/* Integrate with Android Battery Service */
	ret = power_supply_register(&(dev->dev), &ac_main.psy);
	if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI, "[BAT_probe] power_supply_register AC Fail !!\n");
		return ret;
	}
	battery_xlog_printk(BAT_LOG_CRTI, "[BAT_probe] power_supply_register AC Success !!\n");

	ret = power_supply_register(&(dev->dev), &usb_main.psy);
	if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BAT_probe] power_supply_register USB Fail !!\n");
		return ret;
	}
	battery_xlog_printk(BAT_LOG_CRTI, "[BAT_probe] power_supply_register USB Success !!\n");

	ret = power_supply_register(&(dev->dev), &wireless_main.psy);
	if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BAT_probe] power_supply_register WIRELESS Fail !!\n");
		return ret;
	}
	battery_xlog_printk(BAT_LOG_CRTI,
			    "[BAT_probe] power_supply_register WIRELESS Success !!\n");

	ret = power_supply_register(&(dev->dev), &battery_main.psy);
	if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "[BAT_probe] power_supply_register Battery Fail !!\n");
		return ret;
	}
	battery_xlog_printk(BAT_LOG_CRTI, "[BAT_probe] power_supply_register Battery Success !!\n");

#if defined(CONFIG_AMAZON_METRICS_LOG)
	metrics_init();
#endif

#if !defined(CONFIG_POWER_EXT)

#ifdef CONFIG_MTK_POWER_EXT_DETECT
	if (KAL_TRUE == bat_is_ext_power()) {
		battery_main.BAT_STATUS = POWER_SUPPLY_STATUS_FULL;
		battery_main.BAT_HEALTH = POWER_SUPPLY_HEALTH_GOOD;
		battery_main.BAT_PRESENT = 1;
		battery_main.BAT_TECHNOLOGY = POWER_SUPPLY_TECHNOLOGY_LION;
		battery_main.BAT_CAPACITY = 100;
		battery_main.BAT_batt_vol = 4200;
		battery_main.BAT_TEMP = 220;

		g_bat_init_flag = KAL_TRUE;
		return 0;
	}
#endif
	/* For EM */
	{
		int ret_device_file = 0;
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Charging_Enable);

		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Charger_Voltage);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_0_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_1_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_2_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_3_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_4_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_5_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_6_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_7_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_8_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_9_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_10_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_11_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_12_Slope);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_13_Slope);

		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_0_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_1_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_2_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_3_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_4_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_5_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_6_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_7_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_8_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_9_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_10_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_11_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_12_Offset);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ADC_Channel_13_Offset);

		ret_device_file =
		    device_create_file(&(dev->dev), &dev_attr_ADC_Channel_Is_Calibration);

		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Power_On_Voltage);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Power_Off_Voltage);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Charger_TopOff_Value);

		ret_device_file =
		    device_create_file(&(dev->dev), &dev_attr_FG_Battery_CurrentConsumption);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_SW_CoulombCounter);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Charging_CallState);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Charger_Type);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Custom_PlugIn_Time);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Custom_Charging_Current);
	#ifdef CONFIG_AUSTIN_PROJECT
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Custom_Charging_Mode);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_ChargeIC_Vendor_Name);
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Battery_Vendor_Name);
	#endif
#if defined(CONFIG_MTK_PUMP_EXPRESS_SUPPORT) || defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
		ret_device_file = device_create_file(&(dev->dev), &dev_attr_Pump_Express);
#endif
	}

	/* battery_meter_initial();      //move to mt_battery_GetBatteryData() to decrease booting time */

	/* Initialization BMT Struct */
	BMT_status.bat_exist = KAL_TRUE;	/* phone must have battery */
	BMT_status.charger_exist = KAL_FALSE;	/* for default, no charger */
	BMT_status.bat_vol = 0;
	BMT_status.ICharging = 0;
	BMT_status.temperature = 0;
	BMT_status.charger_vol = 0;
	BMT_status.total_charging_time = 0;
	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.POSTFULL_charging_time = 0;
	BMT_status.SOC = 0;
	BMT_status.UI_SOC = -1;

	BMT_status.bat_charging_state = CHR_PRE;
	BMT_status.bat_in_recharging_state = KAL_FALSE;
	BMT_status.bat_full = KAL_FALSE;
	BMT_status.nPercent_ZCV = 0;
	BMT_status.nPrecent_UI_SOC_check_point = battery_meter_get_battery_nPercent_UI_SOC();
	BMT_status.bat_in_charging_enable = KAL_TRUE;

	#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
	dual_input_init();
	#endif

	getrawmonotonic(&chr_plug_in_time);
	g_custom_plugin_time = 0;
	g_custom_charging_cv = -1;

	/* battery kernel thread for 10s check and charger in/out event */
	/* Replace GPT timer by hrtime */
	battery_kthread_hrtimer_init();

	kthread_run(bat_thread_kthread, NULL, "bat_thread_kthread");
	battery_xlog_printk(BAT_LOG_CRTI, "[battery_probe] bat_thread_kthread Done\n");

	charger_hv_detect_sw_workaround_init();

	/*LOG System Set */
	init_proc_log();
	
#else
	//keep HW alive
	charger_hv_detect_sw_workaround_init();
#endif
	g_bat_init_flag = KAL_TRUE;
	
#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
    if (g_vcdt_irq_delay_flag == KAL_TRUE)
        do_chrdet_int_task();
#endif

	return 0;

}

static void battery_timer_pause(void)
{
	struct timespec xts, tom;

    //battery_xlog_printk(BAT_LOG_CRTI, "******** battery driver suspend!! ********\n" );
#ifdef CONFIG_POWER_EXT
#else

#ifdef CONFIG_MTK_POWER_EXT_DETECT
	if (KAL_TRUE == bat_is_ext_power())
		return 0;
#endif
	mutex_lock(&bat_mutex);
	//cancel timer
	hrtimer_cancel(&battery_kthread_timer);
	hrtimer_cancel(&charger_hv_detect_timer);

	battery_suspended = KAL_TRUE;
	mutex_unlock(&bat_mutex);

	battery_xlog_printk(BAT_LOG_CRTI, "@bs=1@\n" );
#endif

    get_xtime_and_monotonic_and_sleep_offset(&xts, &tom, &g_bat_time_before_sleep);
}

static void battery_timer_resume(void)
{
#ifdef CONFIG_POWER_EXT
#else
	kal_bool is_pcm_timer_trigger = KAL_FALSE;
	struct timespec xts, tom, bat_time_after_sleep;
    ktime_t ktime, hvtime;

#ifdef CONFIG_MTK_POWER_EXT_DETECT
	if (KAL_TRUE == bat_is_ext_power())
		return 0;
#endif

    ktime = ktime_set(BAT_TASK_PERIOD, 0);  // 10s, 10* 1000 ms
    hvtime = ktime_set(0, BAT_MS_TO_NS(2000));

	get_xtime_and_monotonic_and_sleep_offset(&xts, &tom, &bat_time_after_sleep);
	battery_charging_control(CHARGING_CMD_GET_IS_PCM_TIMER_TRIGGER,&is_pcm_timer_trigger);

	if(is_pcm_timer_trigger == KAL_TRUE || bat_spm_timeout)
	{	
		mutex_lock(&bat_mutex);
		BAT_thread();
		mutex_unlock(&bat_mutex);
	}
	else
	{
		battery_xlog_printk(BAT_LOG_CRTI, "battery resume NOT by pcm timer!!\n" );
	}

	if(g_call_state == CALL_ACTIVE && (bat_time_after_sleep.tv_sec - g_bat_time_before_sleep.tv_sec >= TALKING_SYNC_TIME))	// phone call last than x min
	{
		BMT_status.UI_SOC = battery_meter_get_battery_percentage();
		battery_xlog_printk(BAT_LOG_CRTI, "Sync UI SOC to SOC immediately\n" );
	}	

	mutex_lock(&bat_mutex);
    
	//restore timer
	hrtimer_start(&battery_kthread_timer, ktime, HRTIMER_MODE_REL);
	hrtimer_start(&charger_hv_detect_timer, hvtime, HRTIMER_MODE_REL);
        
	battery_suspended = KAL_FALSE;
	g_refresh_ui_soc = KAL_TRUE;
	battery_xlog_printk(BAT_LOG_CRTI, "@bs=0@\n");
	mutex_unlock(&bat_mutex);
	
#endif
}

static int battery_remove(struct platform_device *dev)
{
#if defined(CONFIG_AMAZON_METRICS_LOG)
	metrics_uninit();
#endif

	battery_xlog_printk(BAT_LOG_CRTI, "******** battery driver remove!! ********\n");

	return 0;
}

static void battery_shutdown(struct platform_device *dev)
{
	battery_xlog_printk(BAT_LOG_CRTI, "******** battery driver shutdown!! ********\n");

}

/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // Battery Notify API */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
static ssize_t show_BatteryNotify(struct device *dev, struct device_attribute *attr, char *buf)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[Battery] show_BatteryNotify : %x\n",
			    g_BatteryNotifyCode);

	return sprintf(buf, "%u\n", g_BatteryNotifyCode);
}

static ssize_t store_BatteryNotify(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	char *pvalue = NULL;
	unsigned int reg_BatteryNotifyCode = 0;
	battery_xlog_printk(BAT_LOG_CRTI, "[Battery] store_BatteryNotify\n");
	if (buf != NULL && size != 0) {
		battery_xlog_printk(BAT_LOG_CRTI, "[Battery] buf is %s and size is %Zu\n", buf,
				    size);
		reg_BatteryNotifyCode = simple_strtoul(buf, &pvalue, 16);
		g_BatteryNotifyCode = reg_BatteryNotifyCode;
		battery_xlog_printk(BAT_LOG_CRTI, "[Battery] store code : %x\n",
				    g_BatteryNotifyCode);
	}
	return size;
}

static DEVICE_ATTR(BatteryNotify, 0664, show_BatteryNotify, store_BatteryNotify);

static ssize_t show_BN_TestMode(struct device *dev, struct device_attribute *attr, char *buf)
{
	battery_xlog_printk(BAT_LOG_CRTI, "[Battery] show_BN_TestMode : %x\n", g_BN_TestMode);
	return sprintf(buf, "%u\n", g_BN_TestMode);
}

static ssize_t store_BN_TestMode(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t size)
{
	char *pvalue = NULL;
	unsigned int reg_BN_TestMode = 0;
	battery_xlog_printk(BAT_LOG_CRTI, "[Battery] store_BN_TestMode\n");
	if (buf != NULL && size != 0) {
		battery_xlog_printk(BAT_LOG_CRTI, "[Battery] buf is %s and size is %Zu\n", buf,
				    size);
		reg_BN_TestMode = simple_strtoul(buf, &pvalue, 16);
		g_BN_TestMode = reg_BN_TestMode;
		battery_xlog_printk(BAT_LOG_CRTI, "[Battery] store g_BN_TestMode : %x\n",
				    g_BN_TestMode);
	}
	return size;
}

static DEVICE_ATTR(BN_TestMode, 0664, show_BN_TestMode, store_BN_TestMode);


/* ///////////////////////////////////////////////////////////////////////////////////////// */
/* // platform_driver API */
/* ///////////////////////////////////////////////////////////////////////////////////////// */
#if 0
static int battery_cmd_read(char *buf, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;
	char *p = buf;

	p += sprintf(p,
		     "g_battery_thermal_throttling_flag=%d,\nbattery_cmd_thermal_test_mode=%d,\nbattery_cmd_thermal_test_mode_value=%d\n",
		     g_battery_thermal_throttling_flag, battery_cmd_thermal_test_mode,
		     battery_cmd_thermal_test_mode_value);

	*start = buf + off;

	len = p - buf;
	if (len > off)
		len -= off;
	else
		len = 0;

	return len < count ? len : count;
}
#endif

static ssize_t battery_cmd_write(struct file *file, const char *buffer, size_t count, loff_t *data)
{
	int len = 0, bat_tt_enable = 0, bat_thr_test_mode = 0, bat_thr_test_value = 0;
	char desc[32];

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len)) {
		return 0;
	}
	desc[len] = '\0';

	if (sscanf(desc, "%d %d %d", &bat_tt_enable, &bat_thr_test_mode, &bat_thr_test_value) == 3) {
		g_battery_thermal_throttling_flag = bat_tt_enable;
		battery_cmd_thermal_test_mode = bat_thr_test_mode;
		battery_cmd_thermal_test_mode_value = bat_thr_test_value;

		battery_xlog_printk(BAT_LOG_CRTI,
				    "bat_tt_enable=%d, bat_thr_test_mode=%d, bat_thr_test_value=%d\n",
				    g_battery_thermal_throttling_flag,
				    battery_cmd_thermal_test_mode,
				    battery_cmd_thermal_test_mode_value);

		return count;
	} else {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "  bad argument, echo [bat_tt_enable] [bat_thr_test_mode] [bat_thr_test_value] > battery_cmd\n");
	}

	return -EINVAL;
}

static int proc_utilization_show(struct seq_file *m, void *v)
{
	seq_printf(m,
		   "=> g_battery_thermal_throttling_flag=%d,\nbattery_cmd_thermal_test_mode=%d,\nbattery_cmd_thermal_test_mode_value=%d\n",
		   g_battery_thermal_throttling_flag, battery_cmd_thermal_test_mode,
		   battery_cmd_thermal_test_mode_value);

	seq_printf(m, "=> get_usb_current_unlimited=%d,\ncmd_discharging = %d\n",
		   get_usb_current_unlimited(), cmd_discharging);
	return 0;
}

static int proc_utilization_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_utilization_show, NULL);
}

static const struct file_operations battery_cmd_proc_fops = {
	.open = proc_utilization_open,
	.read = seq_read,
	.write = battery_cmd_write,
};

static ssize_t current_cmd_write(struct file *file, const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32];
	int cmd_current_unlimited = false;
	U32 charging_enable = false;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len)) {
		return 0;
	}
	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &cmd_current_unlimited, &cmd_discharging) == 2) {
		set_usb_current_unlimited(cmd_current_unlimited);
		if (cmd_discharging == 1) {
			charging_enable = false;
			adjust_power = -1;
		} else if (cmd_discharging == 0) {
			charging_enable = true;
			adjust_power = -1;
		}
		battery_charging_control(CHARGING_CMD_ENABLE, &charging_enable);

		battery_xlog_printk(BAT_LOG_CRTI,
				    "[current_cmd_write] cmd_current_unlimited=%d, cmd_discharging=%d\n",
				    cmd_current_unlimited, cmd_discharging);
		return count;
	} else {
		battery_xlog_printk(BAT_LOG_CRTI, "  bad argument, echo [enable] > current_cmd\n");
	}

	return -EINVAL;
}

static int current_cmd_read(struct seq_file *m, void *v)
{
	U32 charging_enable = false;

	cmd_discharging = 1;
	charging_enable = false;
	adjust_power = -1;

	battery_charging_control(CHARGING_CMD_ENABLE, &charging_enable);

	battery_xlog_printk(BAT_LOG_CRTI,
			    "[current_cmd_write] cmd_discharging=%d\n", cmd_discharging);

	return 0;
}

static int proc_utilization_open_cur_stop(struct inode *inode, struct file *file)
{
	return single_open(file, current_cmd_read, NULL);
}
static ssize_t discharging_cmd_write(struct file *file, const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32];
	U32 charging_enable = false;
    
	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len)) {
		return 0;
	}
	desc[len] = '\0';
    
	if (sscanf(desc, "%d %d", &charging_enable, &adjust_power) == 2) {
		battery_xlog_printk(BAT_LOG_CRTI, "[current_cmd_write] adjust_power = %d\n", adjust_power);
		return count;
	} else {
		battery_xlog_printk(BAT_LOG_CRTI, "  bad argument, echo [enable] > current_cmd\n");
	}

    	return -EINVAL;
}

static const struct file_operations discharging_cmd_proc_fops = { 
	.open  = proc_utilization_open, 
	.read  = seq_read,
	.write = discharging_cmd_write,
};

static const struct file_operations current_cmd_proc_fops = {
	.open = proc_utilization_open_cur_stop,
	.read = seq_read,
	.write = current_cmd_write,
};

static int mt_batteryNotify_probe(struct platform_device *dev)
{
	int ret_device_file = 0;
	/* struct proc_dir_entry *entry = NULL; */
	struct proc_dir_entry *battery_dir = NULL;

	battery_xlog_printk(BAT_LOG_CRTI, "******** mt_batteryNotify_probe!! ********\n");

	ret_device_file = device_create_file(&(dev->dev), &dev_attr_BatteryNotify);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_BN_TestMode);

	battery_dir = proc_mkdir("mtk_battery_cmd", NULL);
	if (!battery_dir) {
		pr_err("[%s]: mkdir /proc/mtk_battery_cmd failed\n", __func__);
	} else {
#if 1
		proc_create("battery_cmd", S_IRUGO | S_IWUSR, battery_dir, &battery_cmd_proc_fops);
		battery_xlog_printk(BAT_LOG_CRTI, "proc_create battery_cmd_proc_fops\n");

		proc_create("current_cmd", S_IRUGO | S_IWUSR, battery_dir, &current_cmd_proc_fops);
		battery_xlog_printk(BAT_LOG_CRTI, "proc_create current_cmd_proc_fops\n");
		proc_create("discharging_cmd", S_IRUGO | S_IWUSR, battery_dir, &discharging_cmd_proc_fops);
		battery_xlog_printk(BAT_LOG_CRTI, "proc_create discharging_cmd_proc_fops\n");
            

#else
		entry = create_proc_entry("battery_cmd", S_IRUGO | S_IWUSR, battery_dir);
		if (entry) {
			entry->read_proc = battery_cmd_read;
			entry->write_proc = battery_cmd_write;
		}
#endif
	}

	battery_xlog_printk(BAT_LOG_CRTI, "******** mtk_battery_cmd!! ********\n");

	return 0;

}
#ifdef CONFIG_OF
static const struct of_device_id mt_battery_of_match[] = {
	{ .compatible = "mediatek,battery", },
	{},
};

MODULE_DEVICE_TABLE(of, mt_battery_of_match);
#endif

static int battery_pm_suspend(struct device *device)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(device);
	BUG_ON(pdev == NULL);

#if defined(CONFIG_AMAZON_METRICS_LOG)
	metrics_suspend();
#endif

	return ret;
}

static int battery_pm_resume(struct device *device)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(device);
	BUG_ON(pdev == NULL);

#if defined(CONFIG_AMAZON_METRICS_LOG)
	metrics_resume();
#endif

	return ret;
}

static int battery_pm_freeze(struct device *device)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(device);
	BUG_ON(pdev == NULL);

	return ret;
}

static int battery_pm_restore(struct device *device)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(device);
	BUG_ON(pdev == NULL);

	return ret;
}

static int battery_pm_restore_noirq(struct device *device)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(device);
	BUG_ON(pdev == NULL);

	return ret;
}

struct dev_pm_ops battery_pm_ops = {
	.suspend = battery_pm_suspend,
	.resume = battery_pm_resume,
	.freeze = battery_pm_freeze,
	.thaw = battery_pm_restore,
	.restore = battery_pm_restore,
	.restore_noirq = battery_pm_restore_noirq,
};

#if defined(CONFIG_OF) || defined(BATTERY_MODULE_INIT)
struct platform_device battery_device = {
    .name   = "battery",
    .id        = -1,
};
#endif

static struct platform_driver battery_driver = {
	.probe = battery_probe,
	.remove = battery_remove,
	.shutdown = battery_shutdown,
	.driver = {
		.name = "battery",
		.pm = &battery_pm_ops,
	},
};

#ifdef CONFIG_OF
static int battery_dts_probe(struct platform_device *dev)
{
	int ret = 0;
	/* struct proc_dir_entry *entry = NULL; */
	struct proc_dir_entry *battery_dir = NULL;

	battery_xlog_printk(BAT_LOG_CRTI, "******** battery_dts_probe!! ********\n");

	battery_device.dev.of_node = dev->dev.of_node;
	ret = platform_device_register(&battery_device);
    if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "****[battery_dts_probe] Unable to register device (%d)\n", ret);
		return ret;
	}
	return 0;

}

static struct platform_driver battery_dts_driver = {
	.probe = battery_dts_probe,
	.remove = NULL,
	.shutdown = NULL,
	.driver = {
		   .name = "battery-dts",
        #ifdef CONFIG_OF 
 		.of_match_table = mt_battery_of_match,
        #endif
	},
};

//--------------------------------------------------------

static const struct of_device_id mt_bat_notify_of_match[] = {
	{ .compatible = "mediatek,bat_notify", },
	{},
};

MODULE_DEVICE_TABLE(of, mt_bat_notify_of_match);
#endif

struct platform_device MT_batteryNotify_device = {
	.name = "mt-battery",
	.id = -1,
};

static struct platform_driver mt_batteryNotify_driver = {
	.probe = mt_batteryNotify_probe,
	.driver = {
		   .name = "mt-battery",
	},
};

#ifdef CONFIG_OF
static int mt_batteryNotify_dts_probe(struct platform_device *dev)
{
	int ret = 0;
	/* struct proc_dir_entry *entry = NULL; */
	struct proc_dir_entry *battery_dir = NULL;

	battery_xlog_printk(BAT_LOG_CRTI, "******** mt_batteryNotify_dts_probe!! ********\n");

	MT_batteryNotify_device.dev.of_node = dev->dev.of_node;
	ret = platform_device_register(&MT_batteryNotify_device);
    if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "****[mt_batteryNotify_dts] Unable to register device (%d)\n", ret);
		return ret;
	}
	return 0;

}


static struct platform_driver mt_batteryNotify_dts_driver = {
	.probe = mt_batteryNotify_dts_probe,
	.driver = {
		   .name = "mt-dts-battery",
        #ifdef CONFIG_OF
		.of_match_table = mt_bat_notify_of_match,    
        #endif
	},
};
#endif
//--------------------------------------------------------

static int battery_pm_event(struct notifier_block *notifier, unsigned long pm_event, void *unused)
{
	switch(pm_event) {
	case PM_HIBERNATION_PREPARE: /* Going to hibernate */
	case PM_RESTORE_PREPARE: /* Going to restore a saved image */
	case PM_SUSPEND_PREPARE: /* Going to suspend the system */
		pr_warn("[%s] pm_event %lu\n", __func__, pm_event);
		battery_timer_pause();
		return NOTIFY_DONE;
	case PM_POST_HIBERNATION: /* Hibernation finished */
	case PM_POST_SUSPEND: /* Suspend finished */
	case PM_POST_RESTORE: /* Restore failed */
		pr_warn("[%s] pm_event %lu\n", __func__, pm_event);
		battery_timer_resume();
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static struct notifier_block battery_pm_notifier_block = {
    .notifier_call = battery_pm_event,
    .priority = 0,
};

static int __init battery_init(void)
{
	int ret;

	printk("battery_init\n");

#ifdef CONFIG_OF
	//
#else
    
#ifdef BATTERY_MODULE_INIT
	ret = platform_device_register(&battery_device);
	if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "****[battery_device] Unable to device register(%d)\n", ret);
		return ret;
	}
#endif
#endif

	ret = platform_driver_register(&battery_driver);
	if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "****[battery_driver] Unable to register driver (%d)\n", ret);
		return ret;
	}
	/* battery notofy UI */
#ifdef CONFIG_OF
    //
#else
	ret = platform_device_register(&MT_batteryNotify_device);
	if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "****[mt_batteryNotify] Unable to device register(%d)\n", ret);
		return ret;
	}
#endif
	ret = platform_driver_register(&mt_batteryNotify_driver);
	if (ret) {
		battery_xlog_printk(BAT_LOG_CRTI,
				    "****[mt_batteryNotify] Unable to register driver (%d)\n", ret);
		return ret;
	}
#ifdef CONFIG_OF
	ret = platform_driver_register(&battery_dts_driver);
	ret = platform_driver_register(&mt_batteryNotify_dts_driver);
#endif	
	ret = register_pm_notifier(&battery_pm_notifier_block);
	if (ret)
		printk("[%s] failed to register PM notifier %d\n", __func__, ret);

	battery_xlog_printk(BAT_LOG_CRTI, "****[battery_driver] Initialization : DONE !!\n");
	return 0;
}

#ifdef BATTERY_MODULE_INIT
late_initcall(battery_init);
#else
static void __exit battery_exit(void)
{
}
module_init(battery_init);
module_exit(battery_exit);
#endif

MODULE_AUTHOR("Oscar Liu");
MODULE_DESCRIPTION("Battery Device Driver");
MODULE_LICENSE("GPL");
