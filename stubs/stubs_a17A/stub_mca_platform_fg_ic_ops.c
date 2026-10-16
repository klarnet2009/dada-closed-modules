// SPDX-License-Identifier: GPL-2.0
/* generated stub provider: only the export CRCs matter */
#include <linux/module.h>
#include <mca/platform/platform_fg_ic_ops.h>

int platform_fg_ic_ops_register(unsigned int ic_role, void *data, struct fuelguage_ic_ops *platform_fg_ops) { return 0; }
EXPORT_SYMBOL(platform_fg_ic_ops_register);
int platform_fg_ops_probe_ok(unsigned int ic_role, bool *ok) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_probe_ok);
int platform_fg_ops_get_batt_info(unsigned int ic_role, void *info) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_batt_info);
void platform_fg_ops_get_batt_abnormal_info(unsigned int ic_role, void *info) { }
EXPORT_SYMBOL(platform_fg_ops_get_batt_abnormal_info);
int platform_fg_ops_get_soc(unsigned int ic_role) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_soc);
int platform_fg_ops_get_rsoc(unsigned int ic_role, int *rsoc) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_rsoc);
int platform_fg_ops_get_curr(unsigned int ic_role, int *curr) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_curr);
int platform_fg_ops_set_verify_digest(unsigned int ic_role, char *buf) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_set_verify_digest);
int platform_fg_ops_get_verify_digest(unsigned int ic_role, char *buf) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_verify_digest);
int platform_fg_ops_set_authentic(unsigned int ic_role, int value) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_set_authentic);
int platform_fg_ops_get_authentic(unsigned int ic_role, int *value) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_authentic);
int platform_fg_ops_get_error_state(unsigned int ic_role, bool *error) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_error_state);
int platform_fg_ops_get_volt(unsigned int ic_role, int *volt) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_volt);
int platform_fg_ops_set_temp(unsigned int ic_role, int temp) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_set_temp);
int platform_fg_ops_get_temp(unsigned int ic_role, int *temp) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_temp);
int platform_fg_ops_get_original_temp(unsigned int ic_role, int *original_temp) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_original_temp);
int platform_fg_ops_set_iterm(unsigned int ic_role, int curr) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_set_iterm);
int platform_fg_ops_get_charge_status(unsigned int ic_role) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_charge_status);
int platform_fg_ops_get_rm(unsigned int ic_role, int *rm) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_rm);
int platform_fg_ops_get_isc_alert_level(unsigned int ic_role, int *level) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_isc_alert_level);
int platform_fg_ops_get_soa_alert_level(unsigned int ic_role, int *level) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_soa_alert_level);
int platform_fg_ops_get_fastcharge(unsigned int ic_role, int *ffc) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_fastcharge);
int platform_fg_ops_set_fastcharge(unsigned int ic_role, bool en) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_set_fastcharge);
int platform_fg_ops_get_chg_vol(unsigned int ic_role, int *volt) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_chg_vol);
int platform_fg_ops_get_chip_ok(unsigned int ic_role, int *ok) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_chip_ok);
int platform_fg_ops_get_cyclecount(unsigned int ic_role, int *cc) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_cyclecount);
int platform_fg_ops_get_tte(unsigned int ic_role, int *tte) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_tte);
int platform_fg_ops_get_ttf(unsigned int ic_role, int *ttf) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_ttf);
int platform_fg_ops_get_fcc(unsigned int ic_role, int *fcc) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_fcc);
int platform_fg_ops_get_full_design(unsigned int ic_role, int *dc) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_full_design);
int platform_fg_ops_get_decimal_rate(unsigned int ic_role, int *rate) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_decimal_rate);
int platform_fg_ops_get_decimal(unsigned int ic_role, int *decimal) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_decimal);
int platform_fg_ops_get_soh(unsigned int ic_role, int *soh) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_soh);
int platform_fg_ops_get_temp_max(unsigned int ic_role, int *temp_max) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_temp_max);
int platform_fg_ops_get_time_ot(unsigned int ic_role, int *time_ot) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_time_ot);
int platform_fg_ops_get_batt_cell_info(unsigned int ic_role, const char **name) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_batt_cell_info);
int platform_fg_ops_get_cutoff_voltage(unsigned int ic_role, int *volt) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_cutoff_voltage);
int platform_fg_ops_set_cutoff_voltage(unsigned int ic_role, int value) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_set_cutoff_voltage);
int platform_fg_ops_get_dod_count(unsigned int ic_role) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_dod_count);
int platform_fg_ops_get_count_level1(unsigned int ic_role, int *count) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_count_level1);
int platform_fg_ops_get_count_level2(unsigned int ic_role, int *count) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_count_level2);
int platform_fg_ops_get_count_level3(unsigned int ic_role, int *count) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_count_level3);
int platform_fg_ops_get_count_lowtemp(unsigned int ic_role, int *count) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_count_lowtemp);
int platform_fg_ops_set_clear_count_data(unsigned int ic_role) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_set_clear_count_data);
int platform_fg_ops_get_adapt_power(unsigned int ic_role, int *adapt_power) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_adapt_power);
int platform_fg_ops_get_aged_flag(unsigned int ic_role, int *flag) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_aged_flag);
int platform_fg_ops_get_raw_soc(unsigned int ic_role, int *raw_soc) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_raw_soc);
int platform_fg_ops_get_real_supplement_energy(unsigned int ic_role, int *supplement_energy) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_real_supplement_energy);
int platform_fg_ops_get_calibration_ffc_iterm(unsigned int ic_role, int *iterm) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_calibration_ffc_iterm);
int platform_fg_ops_get_calibration_charge_energy(unsigned int ic_role, int *charge_energy) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_calibration_charge_energy);
void platform_fg_ops_fl4p0_enable_check(unsigned int ic_role) { }
EXPORT_SYMBOL(platform_fg_ops_fl4p0_enable_check);
void platform_fg_ops_update_fw(unsigned int ic_role) { }
EXPORT_SYMBOL(platform_fg_ops_update_fw);
int platform_fg_ops_get_device_name(unsigned int ic_role, const char **name) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_device_name);
int platform_fg_ops_get_temp_min(unsigned int ic_role, int *temp_min) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_temp_min);
void platform_fg_ops_set_force_report_full(unsigned int ic_role) { }
EXPORT_SYMBOL(platform_fg_ops_set_force_report_full);
int platform_fg_ops_get_fc(unsigned int ic_role, bool *fc) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_fc);
int platform_fg_ops_set_co(unsigned int ic_role, bool value) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_set_co);
void platform_fg_ops_get_ui_soh(unsigned int ic_role, int *ui_soh) { }
EXPORT_SYMBOL(platform_fg_ops_get_ui_soh);
void platform_fg_ops_qbg_send_chg_data(unsigned int ic_role, int fcc, int final_fcc) { }
EXPORT_SYMBOL(platform_fg_ops_qbg_send_chg_data);
void platform_fg_ops_get_pack_vendor(unsigned int ic_role, int *pack_vendor_id) { }
EXPORT_SYMBOL(platform_fg_ops_get_pack_vendor);
unsigned long platform_fg_ops_get_calc_rvalue(unsigned int ic_role) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_calc_rvalue);
void platform_fg_ops_get_average_current(unsigned int ic_role, int *avg_curr) { }
EXPORT_SYMBOL(platform_fg_ops_get_average_current);
void platform_fg_ops_get_ota_update_flag(unsigned int ic_role, unsigned int *flag) { }
EXPORT_SYMBOL(platform_fg_ops_get_ota_update_flag);
void platform_fg_ops_ota_update_check(unsigned int ic_role) { }
EXPORT_SYMBOL(platform_fg_ops_ota_update_check);
int platform_fg_ops_get_manufacturing_date(unsigned int ic_role, unsigned char *manufacturing_date) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_manufacturing_date);
void platform_fg_ops_set_first_usage_date(unsigned int ic_role, const char *first_usage_date) { }
EXPORT_SYMBOL(platform_fg_ops_set_first_usage_date);
int platform_fg_ops_get_first_usage_date(unsigned int ic_role, unsigned char *first_usage_date) { return 0; }
EXPORT_SYMBOL(platform_fg_ops_get_first_usage_date);

MODULE_LICENSE("GPL");
