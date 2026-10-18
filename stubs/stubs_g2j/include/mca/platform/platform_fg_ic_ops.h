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

struct fuelguage_ic_ops {
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
	void (*fg_ic_qbg_send_chg_data)(void *data, int, int);
	void (*fg_ic_get_pack_vendor)(void *data, int *);
	unsigned long (*fg_ic_get_calc_rvalue)(void *data);
	void (*fg_ic_get_ota_update_flag)(void *data, unsigned int *);
	void (*fg_ic_ota_update_check)(void *data);
	void (*fg_ic_get_average_current)(void *data, int *);
	void (*fg_ic_get_batt_abnormal_info)(void *data, void *);
	int (*fg_ic_get_manufacturing_date)(void *data, unsigned char *);
	void (*fg_ic_set_first_usage_date)(void *data, const char *);
	int (*fg_ic_get_first_usage_date)(void *data, unsigned char *);
};

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
void platform_fg_ops_qbg_send_chg_data(unsigned int ic_role, int fcc, int final_fcc);
void platform_fg_ops_get_pack_vendor(unsigned int ic_role, int *pack_vendor_id);
unsigned long platform_fg_ops_get_calc_rvalue(unsigned int ic_role);
void platform_fg_ops_get_average_current(unsigned int ic_role, int *avg_curr);
void platform_fg_ops_get_ota_update_flag(unsigned int ic_role, unsigned int *flag);
void platform_fg_ops_ota_update_check(unsigned int ic_role);
int platform_fg_ops_get_manufacturing_date(unsigned int ic_role, unsigned char *manufacturing_date);
void platform_fg_ops_set_first_usage_date(unsigned int ic_role, const char *first_usage_date);
int platform_fg_ops_get_first_usage_date(unsigned int ic_role, unsigned char *first_usage_date);

#endif
