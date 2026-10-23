/* SPDX-License-Identifier: GPL-2.0 */
/* platform_fg_ic_ops.h - reconstructed API of mca_platform_fg_ic_ops.ko (XRING prototypes, CRC-checked against the stock exporter) */
#ifndef __PLATFORM_FG_IC_OPS_H__
#define __PLATFORM_FG_IC_OPS_H__

#include <linux/types.h>

enum fg_ic_role {
	FG_IC_MASTER = 0,
	FG_IC_SLAVE = 1,
	FG_IC_MAX,
};

void platform_fg_ops_fl4p0_enable_check(unsigned int ic_role);
int platform_fg_ops_get_adapt_power(unsigned int ic_role, int *adapt_power);
int platform_fg_ops_get_authentic(unsigned int ic_role, int *value);
void platform_fg_ops_get_average_current(unsigned int ic_role, int *avg_curr);
void platform_fg_ops_get_batt_abnormal_info(unsigned int ic_role, void *info);
int platform_fg_ops_get_batt_info(unsigned int ic_role, void *info);
unsigned long platform_fg_ops_get_calc_rvalue(unsigned int ic_role);
int platform_fg_ops_get_calibration_charge_energy(unsigned int ic_role, int *charge_energy);
int platform_fg_ops_get_calibration_ffc_iterm(unsigned int ic_role, int *iterm);
int platform_fg_ops_get_count_level1(unsigned int ic_role, int *count);
int platform_fg_ops_get_count_level2(unsigned int ic_role, int *count);
int platform_fg_ops_get_count_level3(unsigned int ic_role, int *count);
int platform_fg_ops_get_count_lowtemp(unsigned int ic_role, int *count);
int platform_fg_ops_get_curr(unsigned int ic_role, int *curr);
int platform_fg_ops_get_cutoff_voltage(unsigned int ic_role, int *volt);
int platform_fg_ops_get_cyclecount(unsigned int ic_role, int *cc);
int platform_fg_ops_get_device_name(unsigned int ic_role, const char **name);
int platform_fg_ops_get_dod_count(unsigned int ic_role);
int platform_fg_ops_get_error_state(unsigned int ic_role, bool *error);
int platform_fg_ops_get_fastcharge(unsigned int ic_role, int *ffc);
int platform_fg_ops_get_fc(unsigned int ic_role, bool *fc);
int platform_fg_ops_get_fcc(unsigned int ic_role, int *fcc);
int platform_fg_ops_get_first_usage_date(unsigned int ic_role, unsigned char *first_usage_date);
int platform_fg_ops_get_full_design(unsigned int ic_role, int *dc);
int platform_fg_ops_get_isc_alert_level(unsigned int ic_role, int *level);
int platform_fg_ops_get_manufacturing_date(unsigned int ic_role, unsigned char *manufacturing_date);
int platform_fg_ops_get_original_temp(unsigned int ic_role, int *original_temp);
void platform_fg_ops_get_ota_update_flag(unsigned int ic_role, unsigned int *flag);
void platform_fg_ops_get_pack_vendor(unsigned int ic_role, int *pack_vendor_id);
int platform_fg_ops_get_real_supplement_energy(unsigned int ic_role, int *supplement_energy);
int platform_fg_ops_get_rm(unsigned int ic_role, int *rm);
int platform_fg_ops_get_rsoc(unsigned int ic_role, int *rsoc);
int platform_fg_ops_get_soa_alert_level(unsigned int ic_role, int *level);
int platform_fg_ops_get_soh(unsigned int ic_role, int *soh);
int platform_fg_ops_get_temp(unsigned int ic_role, int *temp);
int platform_fg_ops_get_temp_max(unsigned int ic_role, int *temp_max);
int platform_fg_ops_get_temp_min(unsigned int ic_role, int *temp_min);
void platform_fg_ops_get_ui_soh(unsigned int ic_role, int *ui_soh);
int platform_fg_ops_get_verify_digest(unsigned int ic_role, char *buf);
int platform_fg_ops_get_volt(unsigned int ic_role, int *volt);
void platform_fg_ops_ota_update_check(unsigned int ic_role);
int platform_fg_ops_probe_ok(unsigned int ic_role, bool *ok);
void platform_fg_ops_qbg_send_chg_data(unsigned int ic_role, int fcc, int final_fcc);
int platform_fg_ops_set_authentic(unsigned int ic_role, int value);
int platform_fg_ops_set_clear_count_data(unsigned int ic_role);
int platform_fg_ops_set_co(unsigned int ic_role, bool value);
int platform_fg_ops_set_cutoff_voltage(unsigned int ic_role, int value);
int platform_fg_ops_set_fastcharge(unsigned int ic_role, bool en);
void platform_fg_ops_set_first_usage_date(unsigned int ic_role, const char *first_usage_date);
void platform_fg_ops_set_force_report_full(unsigned int ic_role);
int platform_fg_ops_set_temp(unsigned int ic_role, int temp);
int platform_fg_ops_set_verify_digest(unsigned int ic_role, char *buf);
void platform_fg_ops_update_fw(unsigned int ic_role);

#endif
