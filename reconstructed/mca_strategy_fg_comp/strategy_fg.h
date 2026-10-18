/* SPDX-License-Identifier: GPL-2.0 */
/*
 * strategy_fg.h - fg strategy interface (Xiaomi 15 "dada" Qualcomm variant).
 *
 * Reconstructed: member order/offsets/sizes of every structure are taken from the BTF of the stock mca_strategy_fg_comp.ko,
 * member names from the XRING sources of the same code base where the semantics match (else inferred from the DT property /
 * log strings).
 */
#ifndef __STRATEGY_FG_H__
#define __STRATEGY_FG_H__

#include <linux/power_supply.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include "mca_battery_shutdown.h"

#define STRATEGY_FG_FAKE_SOC_NONE -1
#define STRATEGY_FG_FAKE_TEMP_NONE -999
#define STRATEGY_FG_FAKE_BAP_MATCH_NONE -1

#define SOC_DECIMAL_MAX_LEVEL 16
#define MAX_CC_ZONE 3
#define MAX_DOD_ZONE 3
#define SAFE_ITERM_CYCLE_LEVEL 5
#define SAFE_ITERM_TEMP_LEVEL 7

enum weight_count_level_elem {
	WEIGHT_LEVEL_1 = 0,
	WEIGHT_LEVEL_2,
	WEIGHT_LEVEL_3,
	WEIGHT_LEVEL_MAX,
};

enum soc_decimal_iterm {
	SOC_DECIMAL_SOC = 0,
	SOC_DECIMAL_RATE,
	SOC_DECIMAL_MAX,
};

enum strategy_fg_type {
	MCA_FG_TYPE_SINGLE = 0,
	MCA_FG_TYPE_PARALLEL,
	MCA_FG_TYPE_SERIES,
};

struct strategy_fg_soc_decimal {
	int soc;
	int rate;
};

struct lossless_rechg_cfg {
	int temp_low;
	int temp_high;
	int rest_time;
};

#define FG_EXTREME_COLD_MAX_GROUP 10
enum fg_extreme_cold_para_ele {
	FG_EXTREME_COLD_CYCLE_COUNT_MAX = 0,
	FG_EXTREME_COLD_TEMP_MAX,
	FG_EXTREME_COLD_COMPENSATION,
	FG_EXTREME_COLD_SIZE,
};

struct fg_extreme_cold_para {
	int cycle_count_max;
	int temp_max;
	int compensation;
};

struct term_curr_data {
	int batt_temp;
	int batt_temp_offset;
	int normal_iterm;
	int ffc_iterm;
};

struct term_curr_para {
	int iterm_para_size;
	struct term_curr_data *iterm_data;
};

struct strategy_fg_cfg {
	int report_full_raw_soc;
	int fg_type;
	int soc_proportion;
	int soc_proportion_c;
	int extreme_batt_temp_cool_thr;
	int extreme_batt_temp_cold_thr;
	int extreme_vbat_low_normal_thr;
	int extreme_vbat_low_cool_thr;
	int extreme_vbat_low_cold_thr;
	int soc_decimal_cnt;
	int adapt_power;
	int support_dtpt;
	int cool_threshold;
	int design_capacity;
	int terminated_by_cp;
	int clear_CC_per_hundred;
	int show_model_by_country_version;

	const char *model_name;
	const char *model_name_gl;
	const char *model_name_in;
	struct strategy_fg_soc_decimal soc_decimal[SOC_DECIMAL_MAX_LEVEL];
	struct lossless_rechg_cfg rechg_cfg;
	int support_lossless_rechg;
	bool support_lossless_curr_limit;
	int lossless_curr_limit;
	int extreme_cold_para_size;
	struct fg_extreme_cold_para extreme_cold_para[FG_EXTREME_COLD_MAX_GROUP];
	int support_base_flip;
	int base_flip_same;
	int support_pnc;
	int wr_for_sw_cv_wake;
	struct term_curr_para term_curr_data[FG_IC_MAX];
	int fg_hightemp_vterm;
	int force_full_hys;
	int force_full_high_hys;
	int normal_force_full_hys;
	int fast_force_full_vol_delta;
	int support_fl4p0;
	int ffc_safe_item[SAFE_ITERM_CYCLE_LEVEL][SAFE_ITERM_TEMP_LEVEL];
	int vterm_delta;
	int support_nvt1000_ota;
	int support_batt_xvdd_dfx;
	int support_pmic_vterm_dynamics_adjust;
	int support_qbg;
	int capacity_comp;
	int support_fast_mode_when_charging;
	int fast_mode_charging_times;
	bool support_clear_cycle;
	int drop_fv_cycle;
	bool support_isc_li_plating;
};

struct fg_batt_info {
	int curr;
	int volt;
	int temp;
	int original_temp;
	int rsoc;
	int rm;
	int fcc;
	int cycle_count;
	int soh;
};

#define VOL_SMOOTH_LEN		5
#define CURR_SMOOTH_LEN		4
#define TEMP_SMOOTH_LEN		2
struct strategy_fg_smooth {
	int suppot_RSOC_0_smooth;
	int smooth_curr_unit[CURR_SMOOTH_LEN];
	int smooth_vol_offset[VOL_SMOOTH_LEN];
	int smooth_temp_unit[TEMP_SMOOTH_LEN];
	int dischg_smooth_low_temp_unit[VOL_SMOOTH_LEN][CURR_SMOOTH_LEN];
	int dischg_smooth_high_temp_unit[VOL_SMOOTH_LEN][CURR_SMOOTH_LEN];
};

struct fg_chg_data {
	bool quick_chg_en;
	int fcc;
	int iterm;
	int vterm;
	int cv;
};

struct batt_ov_data {
	int last_vfloat;
	int ov_count;
	int ov_vfloat;
	bool drop_fv_flag;
	bool ov_flag;
};

enum isc_state {
	ISC_STATE_WAITING = 0,
	ISC_STATE_SAMPLING,
	ISC_STATE_SAMPLING_2,
};

enum li_plating_state {
	LI_PLATING_BEFORE_FULL = 0,
	LI_PLATING_AFTER_FULL,
};

#define ISC_DATA_NUM 10
#define LI_DATA1_NUM 10
#define LI_DATA2_NUM 30

struct batterydata {
	time64_t time;
	int temp;
	int volt;
	int curr;
	int dod;
	int soc;
};

struct isc_detect_info {
	bool isDetecting;
	bool need_stop;
	int full_count;
	int sample_index;
	int average_point_done_cnt;
	int count;
	int collect_done;
	enum isc_state state;
	struct batterydata isc_data[2][5];
};

struct li_plating_info {
	bool isDetecting;
	int full_count;
	int sample_index;
	int before_full_index;
	enum li_plating_state state;
	struct batterydata li_data_1[LI_DATA1_NUM];
	struct batterydata li_data_2[LI_DATA2_NUM];
};

struct mca_votable;

struct strategy_fg {
	struct device *dev;

	struct delayed_work monitor_work;
	struct delayed_work get_strategy_fg_ops_work;
	struct delayed_work delay_reset_full_flag_work;
	struct delayed_work dtpt_monitor_work;
	struct delayed_work fl4p0_calibration_work;
	struct delayed_work force_report_full_work;
	struct delayed_work nvt1000_ota_monitor_work;
	struct delayed_work batt_abnormal_dfx_work;
	struct delayed_work batt_overvoltage_dfx_work;
	struct delayed_work update_vcoutoff_v2_work;
	struct delayed_work reset_default_co_work;
	struct delayed_work isc_detect_work;
	struct delayed_work isc_detect_2_work;
	struct delayed_work li_plating_detect_work;

	struct notifier_block panel_nb;
	struct notifier_block thermal_board_nb;
	struct strategy_fg_cfg cfg;
	struct strategy_fg_smooth smooth;
	bool fg_init_flag;
	int chg_status;
	ktime_t suspend_time;
	bool fg_error;
	bool charging_done;
	time64_t charging_done_time;
	bool recharging;
	bool lossless_recharge;
	bool is_eu_model;
	bool en_smooth_full;
	int real_type;

	bool voter_ok;
	struct mca_votable *en_voter;
	struct mca_votable *vterm_voter;
	struct mca_votable *iterm_voter;
	struct mca_votable *charge_limit_voter;
	struct mca_votable *flip_fcc_voter;
	struct mca_votable *buck_input_voter;
	struct mca_votable *input_suppend_voter;

	int batt_current;
	int batt_current_mean;
	int batt_voltage;
	int batt_voltage_mean;
	int batt_rsoc;
	int batt_cyclecount;
	int batt_temperature;
	int batt_original_temperature;
	int batt_status;
	int batt_soh;
	int batt_rm;
	int batt_fcc;
	int batt_raw_soc;
	int batt_mapped_soc;
	int batt_mapped_soc_decimal;
	int batt_ui_soc;
	int batt_ui_soc_decimal;
	int batt_index;
	int batt_dc;
	int batt_health;
	int thermal_board_temp;

	time64_t plugin_time;
	bool keep_full_flag;
	time64_t xmpps_initialtime;
	int initial_rm;
	int initial_soc;
	bool nonxmpps_flag;
	time64_t online_time;

	struct power_supply *batt_psy;
	bool fast_charge;
	bool screen_status;
	int dod_count;
	int vcutoff_fw;
	int vcutoff_shutdown_delay;
	int vcutoff_sw;
	bool vbatt_empty;
	bool support_cc_vcutoff;
	bool support_cc_unique_cn_nvt;
	bool support_dod_vcutoff;
	bool support_dod_vcutoff_gl;
	bool support_dod_vcutoff_v2;
	bool support_cc_vcutoff_gl;
	bool notsupport_dod_vcutoff_global;
	bool disable_rollback_shutdown_volt;
	bool support_vpack_low_shutdown;
	bool support_full_design_gl;
	bool support_normal_force_full;
	int weight[WEIGHT_LEVEL_MAX];
	struct mca_battery_shutdown_para_info batt_spec_para;
	struct mca_battery_shutdown_para_info batt_health_para;
	int fake_bap_match;
	int fake_soc;
	int fake_temp;
	int fake_dod_count;
	bool enable_rollback;
	int batt_auth;
	int batt_slave_auth;
	struct fg_batt_info batt_info;
	struct fg_batt_info master_batt_info;
	struct fg_batt_info slave_batt_info;
	int update_period;
	int update_interval;
	int pvbat;
	int pibat;
	int ptbat;
	int cyclecount_hundred;
	int calibration_temp;
	bool start_force_full;
	bool done_force_full;
	bool power_present;
	bool wls_online;
	bool usb_online;
	bool resume_update_soc;
	bool boot_update_soc;
	bool fg1_batt_ctr_enabled;
	bool fg2_batt_ctr_enabled;
	bool fg_lock_flag;
	bool dual_force_full[FG_IC_MAX];
	int dual_iterm[FG_IC_MAX];
	bool monitor_soc_flag;
	bool first_termination;
	bool near_vterm;
	bool ffc_continue_charge;
	bool self_equal_flag[FG_IC_MAX];
	bool self_equal_count[FG_IC_MAX];
	int self_equal_count_max;
	bool fg_master_init_flag;
	bool fg_slave_init_flag;
	bool onewire_init_flag;
	bool fg_master_error;
	bool fg_slave_error;
	int last_final_term_curr;
	bool base_batt_close;
	bool temp_offset_flags[FG_IC_MAX];
	int cold_zone;
	int last_target_iterm;
	time64_t last_dfx_check_time_not_stop_chg_err;
	time64_t last_dfx_check_time_rechg_soc_err;
	time64_t last_dfx_check_time_batt_capacity;
	struct notifier_block shutdown_notifier;
	struct fg_chg_data chg_data;
	struct mutex temp_index_lock;
	struct batt_ov_data ov_data;
	bool support_new_soc_smooth;
	int allow_new_soc_smooth;
	bool new_soc_smooth_active;
	bool check_soc_smooth_flag;
	bool start_pmic_chg_flag;
	time64_t start_pmic_chg_time;
	bool need_lossless_recharge;
	int chg_done_cnt;
	struct isc_detect_info isc_det_info;
	struct isc_detect_info isc_det_2_info;
	struct li_plating_info li_plating_det_info;
};

enum fg_auth_attr_list	{
	FG_PROP_MAIN_AUTH = 0,
	FG_PROP_SLAVE_AUTH,
	FG_PROP_BATT_INDEX,
	FG_PROP_VERIFY_DIGEST,
	FG_PROP_BATT_POWER_MATCH,
	FG_PROP_FAST_CHARGE,
	FG_PROP_SOC_DECIMAL,
	FG_PROP_SOC_DECIMAL_RATE,
	FG_PROP_FAKE_SOC,
	FG_PROP_FAKE_TEMP,
	FG_PROP_BATTERY_NUM,
	FG_PROP_UPDATE_PERIOD,
	FG_PROP_PACK_VOLTAGE,
	FG_PROP_DOD_COUNT,
	FG_PROP_ENABLE_ROLLBACK,
	FG_PROP_SOH_NEW,
	FG_PROP_MASTER_SELF_EQUAL_COUNT,
	FG_PROP_SLAVE_SELF_EQUAL_COUNT,
	FG_PROP_SELF_EQUAL_MAX_COUNT,
	FG_PROP_PACK_TBAT,
	FG_PROP_RAW_SOC,
	FG_PROP_CALC_RVALUE,
	FG_PROP_MANUFACTURING_DATE,
	FG_PROP_FIRST_USAGE_DATE,
	FG_PROP_I_AVER_GEAR,
	FG_PROP_SILOSS_GEAR,
};

/* offsets/sizes proven by the OS4 stock BTF */
_Static_assert(sizeof(struct strategy_fg_cfg) == 632, "strategy_fg_cfg");
_Static_assert(sizeof(struct strategy_fg_smooth) == 208, "strategy_fg_smooth");
_Static_assert(sizeof(struct fg_batt_info) == 36, "fg_batt_info");
_Static_assert(sizeof(struct isc_detect_info) == 352, "isc_detect_info");
_Static_assert(sizeof(struct li_plating_info) == 1304, "li_plating_info");
_Static_assert(sizeof(struct strategy_fg) == 5576, "strategy_fg");
_Static_assert(offsetof(struct strategy_fg, cfg) == 1960, "cfg");
_Static_assert(offsetof(struct strategy_fg, smooth) == 2592, "smooth");
_Static_assert(offsetof(struct strategy_fg, batt_info) == 3204, "batt_info");
_Static_assert(offsetof(struct strategy_fg, temp_offset_flags) == 3385, "temp_offset_flags");
_Static_assert(offsetof(struct strategy_fg, temp_index_lock) == 3472, "temp_index_lock");
_Static_assert(offsetof(struct strategy_fg, li_plating_det_info) == 4272, "li_plating_det_info");

int fg_ui_soc_smooth(struct strategy_fg *fg, bool prohibit_jump);
int onewire_get_shutdown_uisoc(int *uisoc);
int onewire_set_shutdown_uisoc(int uisoc);
/* battery-safe TU (isc / li plating) */
void stop_isc_detect_work(struct isc_detect_info *info);
void stop_isc_detect_2_work(struct isc_detect_info *info);
void stop_li_plating_detect_work(struct li_plating_info *info);
void strategy_isc_detect_work(struct work_struct *work);
void strategy_isc_detect_2_work(struct work_struct *work);
void mca_strategy_start_isc_detect_2_work(void *data);
void strategy_li_plating_work(struct work_struct *work);
int mca_strategy_check_li_plating(void *data);
void strategy_batt_overvoltage_dfx_work(struct work_struct *work);
void strategy_update_vcoutoff_v2_work(struct work_struct *work);
int mca_battery_shutdown_update_cc_para(void *data, int *vcutoff_fw, int *vcutoff_shutdown_delay, int *vcutoff_sw);
int strategy_soc_smooth_parse_dt(struct strategy_fg *fg);
void onewire_check_auth_pass(bool *auth_pass);
void onewire_check_chip_ok(bool *chip_ok);
void strategy_fg_record_volt_mean(struct strategy_fg *fg);
void strategy_fg_update_batt_info(struct strategy_fg *fg);
int strategy_lossless_rechg_parse_dt(struct strategy_fg *fg);
int strategy_full_design_parse_dt(struct strategy_fg *fg);
int strategy_fg_ops_is_init_ok(void *data);
int strategy_fg_ops_is_chip_ok(void *data);
int strategy_fg_dual_ops_is_chip_ok(void *data, int fg_index);

#endif /* __STRATEGY_FG_H__ */
