/* SPDX-License-Identifier: GPL-2.0 */
/* platform_fg_ic_ops.h - reconstructed API of mca_platform_fg_ic_ops.ko (ops struct layout from the stock BTF, prototypes CRC-checked against the stock exporter) */
#ifndef __PLATFORM_FG_IC_OPS_H__
#define __PLATFORM_FG_IC_OPS_H__

#include <linux/types.h>

enum fg_ic_role {
	FG_IC_MASTER = 0,
	FG_IC_SLAVE = 1,
	FG_IC_MAX,
};

/* G13g: from the OS4 stock BTF of bq27z561.ko (enum fg_attr_list, 114 values) */
enum fg_attr_list {
	FG_IC_PROP_CHIP_OK = 0,
	FG_IC_PROP_VOL = 1,
	FG_IC_PROP_PACKVOL = 2,
	FG_IC_PROP_CO_STATUS = 3,
	FG_IC_PROP_CURRENT = 4,
	FG_IC_PROP_RSOC = 5,
	FG_IC_PROP_TEMP = 6,
	FG_IC_PROP_ORIGINAL_TEMP = 7,
	FG_IC_PROP_CYCLE = 8,
	FG_IC_PROP_QMAX = 9,
	FG_IC_PROP_RM = 10,
	FG_IC_PROP_FCC = 11,
	FG_IC_PROP_SOH = 12,
	FG_IC_PROP_FCC_SOH = 13,
	FG_IC_PROP_FAST_CHARGE = 14,
	FG_IC_PROP_RESISTANCE_ID = 15,
	FG_IC_PROP_TSIM = 16,
	FG_IC_PROP_TAMBIENT = 17,
	FG_IC_PROP_TREMQ = 18,
	FG_IC_PROP_TFULLCHGQ = 19,
	FG_IC_PROP_AVERCURRENT = 20,
	FG_IC_PROP_SEAL = 21,
	FG_IC_PROP_DF_CHECK = 22,
	FG_IC_PROP_CHEM_DF_SIGN = 23,
	FG_IC_PROP_IF_SIG = 24,
	FG_IC_PROP_VENDOR_ID = 25,
	FG_IC_PROP_PACK_VENDOR_ID = 26,
	FG_IC_PROP_CELL_VENDOR_ID = 27,
	FG_IC_PROP_EEPROM_VERSION = 28,
	FG_IC_PROP_DESIGN_CAPACITY = 29,
	FG_IC_PROP_CALC_RVALUE = 30,
	FG_IC_PROP_AGED_FLAG = 31,
	FG_IC_PROP_UI_SOH = 32,
	FG_IC_PROP_BATT_SN = 33,
	FG_IC_PROP_MANUFACTURING_DATE = 34,
	FG_IC_PROP_FIRST_USAGE_DATE = 35,
	FG_IC_PROP_FAKE_FIRST_USAGE_DATE = 36,
	FG_IC_PROP_SOH_NEW = 37,
	FG_IC_PROP_DOD_COUNT = 38,
	FG_IC_PROP_COUNT_LEVEL1 = 39,
	FG_IC_PROP_COUNT_LEVEL2 = 40,
	FG_IC_PROP_COUNT_LEVEL3 = 41,
	FG_IC_PROP_COUNT_LOWTEMP = 42,
	FG_IC_PROP_CUTOFF_VOL = 43,
	FG_IC_PROP_DEVICE_NAME = 44,
	FG_IC_PROP_MAX_TEMP_OCCUR_TIME = 45,
	FG_IC_PROP_RUN_TIME = 46,
	FG_IC_PROP_MAX_TEMP_TIME = 47,
	FG_IC_PROP_TOTAL_FW_RUN_TIME = 48,
	FG_IC_PROP_TEMP_MAX = 49,
	FG_IC_PROP_TIME_HT = 50,
	FG_IC_PROP_TIME_OT = 51,
	FG_IC_PROP_OVER_PEAK_FLAG = 52,
	FG_IC_PROP_MONITOR_ISC = 53,
	FG_IC_PROP_MONITOR_SOA = 54,
	FG_IC_PROP_CURRENT_DEVIATION = 55,
	FG_IC_PROP_POWER_DEVIATION = 56,
	FG_IC_PROP_AVERAGE_CURRENT = 57,
	FG_IC_PROP_AVERAGE_TEMPERATURE = 58,
	FG_IC_PROP_START_LEARNING = 59,
	FG_IC_PROP_STOP_LEARNING = 60,
	FG_IC_PROP_ACTUAL_POWER = 61,
	FG_IC_PROP_LEARNING_POWER = 62,
	FG_IC_PROP_LEARNING_POWER_DEV = 63,
	FG_IC_PROP_LEARNING_TIME_DEV = 64,
	FG_IC_PROP_CONSTANT_POWER = 65,
	FG_IC_PROP_REMAINING_TIME = 66,
	FG_IC_PROP_REFERANCE_POWER = 67,
	FG_IC_PROP_NVT_REFERANCE_POWER = 68,
	FG_IC_PROP_REFERANCE_CURRENT = 69,
	FG_IC_PROP_START_LEARNING_B = 70,
	FG_IC_PROP_STOP_LEARNING_B = 71,
	FG_IC_PROP_LEARNING_POWER_B = 72,
	FG_IC_PROP_ACTUAL_POWER_B = 73,
	FG_IC_PROP_LEARNING_POWER_DEV_B = 74,
	FG_IC_PROP_REL_SOH = 75,
	FG_IC_PROP_REL_SOH_CYCLECOUNT = 76,
	FG_IC_PROP_EIS_SOH = 77,
	FG_IC_PROP_EIS_SOH_CYCLECOUNT = 78,
	FG_IC_PROP_QMAX_CYCLECOUNT = 79,
	FG_IC_PROP_BATT_USE_ENVIRRONMENT = 80,
	FG_IC_PROP_MAX_LIFE_VOL = 81,
	FG_IC_PROP_MIN_LIFE_VOL = 82,
	FG_IC_PROP_MAX_LIFE_TEMP = 83,
	FG_IC_PROP_MIN_LIFE_TEMP = 84,
	FG_IC_PROP_OVER_VOL_DURATION = 85,
	FG_IC_PROP_FC = 86,
	FG_IC_PROP_CSD_FLAG = 87,
	FG_IC_PROP_CSD_R1 = 88,
	FG_IC_PROP_CSD_R2 = 89,
	FG_IC_PROP_DCR_SLOPE1 = 90,
	FG_IC_PROP_DCR_SLOPE2 = 91,
	FG_IC_PROP_DCR_SLOPE3 = 92,
	FG_IC_PROP_CHARGE_ABSOC = 93,
	FG_IC_PROP_MAX_DSG_CURRENT = 94,
	FG_IC_PROP_MIN_TEMP = 95,
	FG_IC_PROP_MIN_VOLTAGE = 96,
	FG_IC_PROP_CUV_COUNT = 97,
	FG_IC_PROP_CUV_LAST_CYCLE = 98,
	FG_IC_PROP_HCUV_COUNT = 99,
	FG_IC_PROP_HCUV_LAST_CYCLE = 100,
	FG_IC_PROP_OCD_COUNT = 101,
	FG_IC_PROP_OCD_LAST_CYCLE = 102,
	FG_IC_PROP_HOCD_COUNT = 103,
	FG_IC_PROP_HOCD_LAST_CYCLE = 104,
	FG_IC_PROP_HSCD_COUNT = 105,
	FG_IC_PROP_HSCD_LAST_CYCLE = 106,
	FG_IC_PROP_CO_AUTO_OPEN = 107,
	FG_IC_PROP_V_SILOSS = 108,
	FG_IC_PROP_I_AVER = 109,
	FG_IC_PROP_SOH_AGING_TEST = 110,
	FG_IC_PROP_FAST_MODE_TIMES = 111,
	FG_IC_PROP_SUPPORT_BATT_MANAGER_4P0 = 112,
	FG_IC_PROP_FC_HOURS_REAL = 113,
	FG_IC_PROP_NON_FC_HOURS_REAL = 114,
	FG_IC_PROP_DCR = 115,
	FG_IC_PROP_DCR_INC_RATIO = 116,
	FG_IC_PROP_DCR_REFRESH_TIME = 117,
	FG_IC_PROP_FC_DAMAGE = 118,
	FG_IC_PROP_ALARM_GAS_FINAL = 119,
	FG_IC_PROP_DCR_SLOPE_GE = 120,
	FG_IC_PROP_UCL_GE = 121,
	FG_IC_PROP_DCR_RATIO = 122,
	FG_IC_PROP_CLOUND_FG_FW_UPDATE = 123,	/* GLOBAL OS4.0.0.7: 10 new ids 113..122 (G14b/G14g), upgrade_fw moved 113 -> 123 */
};

struct fuelguage_ic_ops {   /* G13l: member list and order from the stock BTF (77 members, 616 bytes; G14g: +2 global members = 79, 632 bytes) */
	int (*fg_ic_probe_ok)(void *data, bool *);
	int (*fg_ic_get_batt_info)(void *data, void *);
	int (*fg_ic_get_soc)(void *data);
	int (*fg_ic_get_rsoc)(void *data, int *);
	int (*fg_ic_get_curr)(void *data, int *);
	int (*fg_ic_get_volt)(void *data, int *);
	int (*fg_ic_set_temp)(void *data, int);
	int (*fg_ic_get_temp)(void *data, int *);
	int (*fg_ic_get_original_temp)(void *data, int *);
	int (*fg_ic_set_iterm)(void *data, int);
	int (*fg_ic_get_charge_status)(void *data);
	int (*fg_ic_get_rm)(void *data, int *);
	int (*fg_ic_get_fastcharge)(void *data, int *);
	int (*fg_ic_set_fastcharge)(void *data, bool);
	int (*fg_ic_get_chg_vol)(void *data, int *);
	int (*fg_ic_get_chip_ok)(void *data, int *);
	int (*fg_ic_get_cyclecount)(void *data, int *);
	int (*fg_ic_get_capacity_level)(void *data);
	int (*fg_ic_get_tte)(void *data, int *);
	int (*fg_ic_get_ttf)(void *data, int *);
	int (*fg_ic_get_fcc)(void *data, int *);
	int (*fg_ic_get_fcc_default)(void *data, bool *);
	int (*fg_ic_get_full_design)(void *data, int *);
	int (*fg_ic_get_decimal_rate)(void *data, int *);
	int (*fg_ic_get_decimal)(void *data, int *);
	int (*fg_ic_get_soh)(void *data, int *);
	int (*fg_ic_get_temp_max)(void *data, int *);
	int (*fg_ic_get_time_ot)(void *data, int *);
	int (*fg_ic_get_batt_cell_info)(void *data, const char **);
	int (*fg_ic_set_verify_digest)(void *data, char *);
	int (*fg_ic_get_verify_digest)(void *data, char *);
	int (*fg_ic_set_authentic)(void *data, int);
	int (*fg_ic_get_authentic)(void *data, int *);
	int (*fg_ic_get_error_state)(void *data, bool *);
	int (*fg_ic_get_cutoff_voltage)(void *data, int *);
	int (*fg_ic_set_cutoff_voltage)(void *data, int);
	int (*fg_ic_get_dod_count)(void *data);
	int (*fg_ic_get_count_level1)(void *data, int *);
	int (*fg_ic_get_count_level2)(void *data, int *);
	int (*fg_ic_get_count_level3)(void *data, int *);
	int (*fg_ic_get_vsiloss)(void *data, int *);
	int (*fg_ic_get_iaver)(void *data, int *);
	int (*fg_ic_get_count_lowtemp)(void *data, int *);
	int (*fg_ic_set_clear_count_data)(void *data);
	int (*fg_ic_get_adapt_power)(void *data, int *);
	int (*fg_ic_get_aged_flag)(void *data, int *);
	int (*fg_ic_get_isc_alert_level)(void *data, int *);
	int (*fg_ic_get_soa_alert_level)(void *data, int *);
	int (*fg_ic_get_raw_soc)(void *data, int *);
	void (*fg_ic_update_fw)(void *data);
	int (*fg_ic_get_device_name)(void *data, const char **);
	int (*fg_ic_get_temp_min)(void *data, int *);
	void (*fg_ic_set_force_report_full)(void *data);
	int (*fg_ic_get_fc)(void *data, bool *);
	int (*fg_ic_set_co)(void *data, bool);
	int (*fg_ic_get_calibration_ffc_iterm)(void *data, int *);
	int (*fg_ic_get_real_supplement_energy)(void *data, int *);
	int (*fg_ic_get_calibration_charge_energy)(void *data, int *);
	void (*fg_ic_fl4p0_enable_check)(void *data);
	void (*fg_ic_get_ui_soh)(void *data, int *);
	void (*fg_ic_qbg_send_chg_data)(void *data, void *);
	void (*fg_ic_get_pack_vendor)(void *data, int *);
	unsigned long (*fg_ic_get_calc_rvalue)(void *data);
	void (*fg_ic_get_ota_update_flag)(void *data, unsigned int *);
	void (*fg_ic_ota_update_check)(void *data);
	void (*fg_ic_get_average_current)(void *data, int *);
	void (*fg_ic_get_batt_abnormal_info)(void *data, void *);
	int (*fg_ic_get_manufacturing_date)(void *data, unsigned char *);
	void (*fg_ic_set_first_usage_date)(void *data, const char *);
	int (*fg_ic_get_first_usage_date)(void *data, unsigned char *);
	int (*fg_ic_read_one_co)(void *data, int *);
	int (*fg_ic_get_fast_mode_times)(void *data, int *);
	int (*fg_ic_set_fast_mode_times)(void *data, int);
	int (*fg_ic_get_fg_vendor)(void *data, int *);
	int (*fg_ic_get_csd_flag)(void *data, int *);
	int (*fg_ic_get_batt_qmax)(void *data, int *);
	int (*fg_ic_get_cc_coulomb_integral)(void *data, int *);
	int (*fg_ic_get_batt_sn)(void *data, char *);	/* GLOBAL OS4.0.0.7 +616 (mca_platform_fg_ic_ops BTF) */
	int (*fg_ic_set_dynamic_fv)(void *data, int);	/* GLOBAL OS4.0.0.7 +624 */
};

int platform_fg_ops_get_batt_sn(unsigned int ic_role, char *batt_sn);
int platform_fg_ops_set_dynamic_fv(unsigned int ic_role, int fv);
int platform_fg_ic_ops_register(unsigned int ic_role, void *data, struct fuelguage_ic_ops *platform_fg_ops);
int platform_fg_ops_probe_ok(unsigned int ic_role, bool *ok);
int platform_fg_ops_get_batt_info(unsigned int ic_role, void *info);
void platform_fg_ops_get_batt_abnormal_info(unsigned int ic_role, void *info);
int platform_fg_ops_get_soc(unsigned int ic_role);
int platform_fg_ops_get_rsoc(unsigned int ic_role, int *rsoc);
int platform_fg_ops_get_vsiloss(unsigned int ic_role, int *vsiloss);
int platform_fg_ops_get_iaver(unsigned int ic_role, int *iaver);
int platform_fg_ops_get_cc_coulomb_integral(unsigned int ic_role, int *val);
int platform_fg_ops_get_batt_qmax(unsigned int ic_role, int *qmax);
int platform_fg_ops_get_fcc_default(unsigned int ic_role, bool *fcc_default);
int platform_fg_ops_get_fg_vendor(unsigned int ic_role, int *fg_vendor);
int platform_fg_ops_get_csd_flag(unsigned int ic_role, int *csd_flag);
int platform_fg_ops_get_fast_mode_times(unsigned int ic_role, int *fast_mode_times);
int platform_fg_ops_set_fast_mode_times(unsigned int ic_role, int fast_mode_times);
int platform_fg_ops_get_curr(unsigned int ic_role, int *curr);
int platform_fg_ops_set_verify_digest(unsigned int ic_role, char *buf);
int platform_fg_ops_get_verify_digest(unsigned int ic_role, char *buf);
int platform_fg_ops_set_authentic(unsigned int ic_role, int value);
int platform_fg_ops_get_authentic(unsigned int ic_role, int *value);
int platform_fg_ops_get_error_state(unsigned int ic_role, bool *error);
int platform_fg_ops_get_volt(unsigned int ic_role, int *volt);
int platform_fg_ops_set_temp(unsigned int ic_role, int temp);
int platform_fg_ops_get_temp(unsigned int ic_role, int *temp);
int platform_fg_ops_get_original_temp(unsigned int ic_role, int *original_temp);
int platform_fg_ops_set_iterm(unsigned int ic_role, int curr);
int platform_fg_ops_get_charge_status(unsigned int ic_role);
int platform_fg_ops_get_rm(unsigned int ic_role, int *rm);
int platform_fg_ops_get_isc_alert_level(unsigned int ic_role, int *level);
int platform_fg_ops_get_soa_alert_level(unsigned int ic_role, int *level);
int platform_fg_ops_get_fastcharge(unsigned int ic_role, int *ffc);
int platform_fg_ops_set_fastcharge(unsigned int ic_role, bool en);
int platform_fg_ops_get_chg_vol(unsigned int ic_role, int *volt);
int platform_fg_ops_get_chip_ok(unsigned int ic_role, int *ok);
int platform_fg_ops_get_cyclecount(unsigned int ic_role, int *cc);
int platform_fg_ops_get_tte(unsigned int ic_role, int *tte);
int platform_fg_ops_get_ttf(unsigned int ic_role, int *ttf);
int platform_fg_ops_get_fcc(unsigned int ic_role, int *fcc);
int platform_fg_ops_get_full_design(unsigned int ic_role, int *dc);
int platform_fg_ops_get_decimal_rate(unsigned int ic_role, int *rate);
int platform_fg_ops_get_decimal(unsigned int ic_role, int *decimal);
int platform_fg_ops_get_soh(unsigned int ic_role, int *soh);
int platform_fg_ops_get_temp_max(unsigned int ic_role, int *temp_max);
int platform_fg_ops_get_time_ot(unsigned int ic_role, int *time_ot);
int platform_fg_ops_get_batt_cell_info(unsigned int ic_role, const char **name);
int platform_fg_ops_get_cutoff_voltage(unsigned int ic_role, int *volt);
int platform_fg_ops_set_cutoff_voltage(unsigned int ic_role, int value);
int platform_fg_ops_get_dod_count(unsigned int ic_role);
int platform_fg_ops_get_count_level1(unsigned int ic_role, int *count);
int platform_fg_ops_get_count_level2(unsigned int ic_role, int *count);
int platform_fg_ops_get_count_level3(unsigned int ic_role, int *count);
int platform_fg_ops_get_count_lowtemp(unsigned int ic_role, int *count);
int platform_fg_ops_set_clear_count_data(unsigned int ic_role);
int platform_fg_ops_get_adapt_power(unsigned int ic_role, int *adapt_power);
int platform_fg_ops_get_aged_flag(unsigned int ic_role, int *flag);
int platform_fg_ops_get_raw_soc(unsigned int ic_role, int *raw_soc);
int platform_fg_ops_get_real_supplement_energy(unsigned int ic_role, int *supplement_energy);
int platform_fg_ops_get_calibration_ffc_iterm(unsigned int ic_role, int *iterm);
int platform_fg_ops_get_calibration_charge_energy(unsigned int ic_role, int *charge_energy);
void platform_fg_ops_fl4p0_enable_check(unsigned int ic_role);
void platform_fg_ops_update_fw(unsigned int ic_role);
int platform_fg_ops_get_device_name(unsigned int ic_role, const char **name);
int platform_fg_ops_get_temp_min(unsigned int ic_role, int *temp_min);
void platform_fg_ops_set_force_report_full(unsigned int ic_role);
int platform_fg_ops_get_fc(unsigned int ic_role, bool *fc);
int platform_fg_ops_set_co(unsigned int ic_role, bool value);
void platform_fg_ops_get_ui_soh(unsigned int ic_role, int *ui_soh);
void platform_fg_ops_qbg_send_chg_data(unsigned int ic_role, void *chg_data);
void platform_fg_ops_get_pack_vendor(unsigned int ic_role, int *pack_vendor_id);
unsigned long platform_fg_ops_get_calc_rvalue(unsigned int ic_role);
void platform_fg_ops_get_average_current(unsigned int ic_role, int *avg_curr);
void platform_fg_ops_get_ota_update_flag(unsigned int ic_role, unsigned int *flag);
void platform_fg_ops_ota_update_check(unsigned int ic_role);
int platform_fg_ops_get_manufacturing_date(unsigned int ic_role, unsigned char *manufacturing_date);
void platform_fg_ops_set_first_usage_date(unsigned int ic_role, const char *first_usage_date);
int platform_fg_ops_get_first_usage_date(unsigned int ic_role, unsigned char *first_usage_date);


_Static_assert(sizeof(struct fuelguage_ic_ops) == 632, "fuelguage_ic_ops size");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_probe_ok) == 0, "fg_ic_probe_ok");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_batt_info) == 8, "fg_ic_get_batt_info");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_soc) == 16, "fg_ic_get_soc");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_rsoc) == 24, "fg_ic_get_rsoc");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_curr) == 32, "fg_ic_get_curr");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_volt) == 40, "fg_ic_get_volt");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_temp) == 48, "fg_ic_set_temp");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_temp) == 56, "fg_ic_get_temp");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_original_temp) == 64, "fg_ic_get_original_temp");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_iterm) == 72, "fg_ic_set_iterm");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_charge_status) == 80, "fg_ic_get_charge_status");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_rm) == 88, "fg_ic_get_rm");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_fastcharge) == 96, "fg_ic_get_fastcharge");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_fastcharge) == 104, "fg_ic_set_fastcharge");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_chg_vol) == 112, "fg_ic_get_chg_vol");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_chip_ok) == 120, "fg_ic_get_chip_ok");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_cyclecount) == 128, "fg_ic_get_cyclecount");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_capacity_level) == 136, "fg_ic_get_capacity_level");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_tte) == 144, "fg_ic_get_tte");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_ttf) == 152, "fg_ic_get_ttf");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_fcc) == 160, "fg_ic_get_fcc");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_fcc_default) == 168, "fg_ic_get_fcc_default");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_full_design) == 176, "fg_ic_get_full_design");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_decimal_rate) == 184, "fg_ic_get_decimal_rate");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_decimal) == 192, "fg_ic_get_decimal");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_soh) == 200, "fg_ic_get_soh");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_temp_max) == 208, "fg_ic_get_temp_max");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_time_ot) == 216, "fg_ic_get_time_ot");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_batt_cell_info) == 224, "fg_ic_get_batt_cell_info");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_verify_digest) == 232, "fg_ic_set_verify_digest");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_verify_digest) == 240, "fg_ic_get_verify_digest");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_authentic) == 248, "fg_ic_set_authentic");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_authentic) == 256, "fg_ic_get_authentic");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_error_state) == 264, "fg_ic_get_error_state");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_cutoff_voltage) == 272, "fg_ic_get_cutoff_voltage");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_cutoff_voltage) == 280, "fg_ic_set_cutoff_voltage");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_dod_count) == 288, "fg_ic_get_dod_count");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_count_level1) == 296, "fg_ic_get_count_level1");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_count_level2) == 304, "fg_ic_get_count_level2");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_count_level3) == 312, "fg_ic_get_count_level3");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_vsiloss) == 320, "fg_ic_get_vsiloss");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_iaver) == 328, "fg_ic_get_iaver");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_count_lowtemp) == 336, "fg_ic_get_count_lowtemp");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_clear_count_data) == 344, "fg_ic_set_clear_count_data");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_adapt_power) == 352, "fg_ic_get_adapt_power");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_aged_flag) == 360, "fg_ic_get_aged_flag");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_isc_alert_level) == 368, "fg_ic_get_isc_alert_level");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_soa_alert_level) == 376, "fg_ic_get_soa_alert_level");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_raw_soc) == 384, "fg_ic_get_raw_soc");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_update_fw) == 392, "fg_ic_update_fw");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_device_name) == 400, "fg_ic_get_device_name");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_temp_min) == 408, "fg_ic_get_temp_min");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_force_report_full) == 416, "fg_ic_set_force_report_full");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_fc) == 424, "fg_ic_get_fc");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_co) == 432, "fg_ic_set_co");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_calibration_ffc_iterm) == 440, "fg_ic_get_calibration_ffc_iterm");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_real_supplement_energy) == 448, "fg_ic_get_real_supplement_energy");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_calibration_charge_energy) == 456, "fg_ic_get_calibration_charge_energy");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_fl4p0_enable_check) == 464, "fg_ic_fl4p0_enable_check");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_ui_soh) == 472, "fg_ic_get_ui_soh");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_qbg_send_chg_data) == 480, "fg_ic_qbg_send_chg_data");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_pack_vendor) == 488, "fg_ic_get_pack_vendor");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_calc_rvalue) == 496, "fg_ic_get_calc_rvalue");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_ota_update_flag) == 504, "fg_ic_get_ota_update_flag");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_ota_update_check) == 512, "fg_ic_ota_update_check");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_average_current) == 520, "fg_ic_get_average_current");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_batt_abnormal_info) == 528, "fg_ic_get_batt_abnormal_info");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_manufacturing_date) == 536, "fg_ic_get_manufacturing_date");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_first_usage_date) == 544, "fg_ic_set_first_usage_date");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_first_usage_date) == 552, "fg_ic_get_first_usage_date");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_read_one_co) == 560, "fg_ic_read_one_co");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_fast_mode_times) == 568, "fg_ic_get_fast_mode_times");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_fast_mode_times) == 576, "fg_ic_set_fast_mode_times");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_fg_vendor) == 584, "fg_ic_get_fg_vendor");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_csd_flag) == 592, "fg_ic_get_csd_flag");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_batt_qmax) == 600, "fg_ic_get_batt_qmax");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_cc_coulomb_integral) == 608, "fg_ic_get_cc_coulomb_integral");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_get_batt_sn) == 616, "fg_ic_get_batt_sn");
_Static_assert(offsetof(struct fuelguage_ic_ops, fg_ic_set_dynamic_fv) == 624, "fg_ic_set_dynamic_fv");

#endif
