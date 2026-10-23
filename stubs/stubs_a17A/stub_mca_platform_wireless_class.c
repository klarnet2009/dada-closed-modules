// SPDX-License-Identifier: GPL-2.0
/* generated stub provider: only the export CRCs matter */
#include <linux/module.h>
#include <mca/platform/platform_wireless_class.h>

int platform_class_wireless_register_ops(unsigned int role, void *data, struct platform_class_wireless_ops *ops) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_register_ops);
int platform_class_wireless_enable_reverse_chg(unsigned int role, bool enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_enable_reverse_chg);
int platform_class_wireless_is_present(unsigned int role, int *present) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_is_present);
int platform_class_wireless_set_vout(unsigned int role, int vout) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_vout);
int platform_class_wireless_get_vout(unsigned int role, int *vout) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_vout);
int platform_class_wireless_get_iout(unsigned int role, int *iout) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_iout);
int platform_class_wireless_get_vrect(unsigned int role, int *vrect) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_vrect);
int platform_class_wireless_get_tx_adapter(unsigned int role, int *adapter) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_adapter);
int platform_class_wireless_get_tx_adapter_by_i2c(unsigned int role, int *adapter) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_adapter_by_i2c);
int platform_class_wireless_get_temp(unsigned int role, int *temp) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_temp);
int platform_class_wireless_set_enable_mode(unsigned int role, bool enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_enable_mode);
int platform_class_wireless_is_car_adapter(unsigned int role, bool *enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_is_car_adapter);
int platform_class_wireless_get_fw_version(unsigned int role, char *buf) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_fw_version);
int platform_class_wireless_get_rx_rtx_mode(unsigned int role, int *mode) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_rx_rtx_mode);
int platform_class_wireless_set_fw_bin(unsigned int role, const char *buf, int count) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_fw_bin);
int platform_class_wireless_set_input_current_limit(unsigned int role, int value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_input_current_limit);
int platform_class_wireless_get_rx_int_flag(unsigned int role, int *int_flag) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_rx_int_flag);
int platform_class_wireless_get_rx_power_mode(unsigned int role, u8 *power_mode) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_rx_power_mode);
int platform_class_wireless_get_tx_max_power(unsigned int role, u8 *max_power) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_max_power);
int platform_class_wireless_get_auth_value(unsigned int role, int *value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_auth_value);
int platform_class_wireless_set_adapter_voltage(unsigned int role, int voltage) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_adapter_voltage);
int platform_class_wireless_get_tx_uuid(unsigned int role, u8 *uuid) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_uuid);
int platform_class_wireless_set_fod_params(unsigned int role, int value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_fod_params);
int platform_class_wireless_get_rx_fastcharge_status(unsigned int role, u8 *fc_flag) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_rx_fastcharge_status);
int platform_class_wireless_receive_transparent_data(unsigned int role, u8 *rcv_value, int buff_len, int *rcv_len) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_receive_transparent_data);
int platform_class_wireless_send_transparent_data(unsigned int role, u8 *send_data, u8 length) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_send_transparent_data);
int platform_class_wireless_get_ss_voltage(unsigned int role, int *ss_voltage) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_ss_voltage);
int platform_class_wireless_do_renego(unsigned int role, u8 max_power) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_do_renego);
int platform_class_wireless_set_parallel_charge(unsigned int role, bool parallel) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_parallel_charge);
int platform_class_wireless_get_vout_setted(unsigned int role, int *vout_setted) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_vout_setted);
int platform_class_wireless_get_poweroff_err_code(unsigned int role, u8 *err_code) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_poweroff_err_code);
int platform_class_wireless_get_rx_err_code(unsigned int role, u8 *err_code, u8 *dfx_code) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_rx_err_code);
int platform_class_wireless_get_rx_brg_status(unsigned int role, u8 *brg_status) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_rx_brg_status);
int platform_class_wireless_get_tx_err_code(unsigned int role, u8 *err_code, u8 *dfx_code) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_err_code);
int platform_class_wireless_get_project_vendor(unsigned int role, int *project_vendor) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_project_vendor);
int platform_class_wireless_check_i2c_is_ok(unsigned int role) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_check_i2c_is_ok);
int platform_class_wireless_enable_rev_fod(unsigned int role, bool enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_enable_rev_fod);
int platform_class_wireless_send_tx_q_value(unsigned int role, u8 value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_send_tx_q_value);
int platform_class_wireless_set_tx_fan_speed(unsigned int role, int value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_tx_fan_speed);
int platform_class_wireless_get_tx_fan_speed(unsigned int role, int *value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_fan_speed);
int platform_class_wireless_set_rx_offset(unsigned int role, int rx_offset) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_rx_offset);
int platform_class_wireless_get_rx_offset(unsigned int role, int *rx_offset) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_rx_offset);
int platform_class_wireless_set_rx_sleep_mode(unsigned int role, int sleep_for_dam) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_rx_sleep_mode);
int platform_class_wireless_download_fw_from_bin(unsigned int role) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_download_fw_from_bin);
int platform_class_wireless_erase_fw(unsigned int role) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_erase_fw);
int platform_class_wireless_get_fw_version_check(unsigned int role, u8 *check_result) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_fw_version_check);
int platform_class_wireless_download_fw(unsigned int role) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_download_fw);
int platform_class_wireless_set_confirm_data(unsigned int role, u8 confirm_data) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_confirm_data);
int platform_class_wireless_receive_test_cmd(unsigned int role, u8 *rev_data, int *length) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_receive_test_cmd);
int platform_class_wireless_process_factory_cmd(unsigned int role, u8 cmd) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_process_factory_cmd);
int platform_class_wireless_get_hall_gpio_status(unsigned int role, bool *status) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_hall_gpio_status);
int platform_class_wireless_set_phone_case_category(unsigned int role, int category) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_phone_case_category);
int platform_class_wireless_get_phone_case_category(unsigned int role, int *category) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_phone_case_category);
int platform_class_wireless_get_magnetic_case_flag(unsigned int role, bool *status) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_magnetic_case_flag);
int platform_class_wireless_check_firmware_state(unsigned int role, bool *update) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_check_firmware_state);
int platform_class_wireless_set_debug_fod(unsigned int role, int *args, int count) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_debug_fod);
int platform_class_wireless_get_debug_fod_type(unsigned int role, WLS_DEBUG_SET_FOD_TYPE *type) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_debug_fod_type);
int platform_class_wireless_set_debug_fod_params(unsigned int role) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_debug_fod_params);
int platform_class_wireless_enable_vsys_ctrl(unsigned int role, bool enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_enable_vsys_ctrl);
int platform_class_wireless_get_trx_isense(unsigned int role, int *isense) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_trx_isense);
int platform_class_wireless_get_trx_vrect(unsigned int role, int *vrect) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_trx_vrect);
int platform_class_wireless_get_fw_upgrade_fail_info(unsigned int role, const char **fail_info) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_fw_upgrade_fail_info);
int platform_class_wireless_get_rsv_eppmode_fail(unsigned int role, int *fail_info) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_rsv_eppmode_fail);
int platform_class_wireless_switch_bridge(unsigned int role, u8 bridge) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_switch_bridge);
int platform_class_wireless_set_external_boost_enable(unsigned int role, bool enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_external_boost_enable);
int platform_class_wireless_get_tx_vout(unsigned int role, int *vout) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_vout);
int platform_class_wireless_get_tx_iout(unsigned int role, int *iout) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_iout);
int platform_class_wireless_get_pen_mac(unsigned int role, u64 *mac) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_mac);
int platform_class_wireless_get_pen_soc(unsigned int role, int *soc) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_soc);
int platform_class_wireless_get_pen_full_flag(unsigned int role, int *pen_full) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_full_flag);
int platform_class_wireless_get_pen_hall3(unsigned int role, int *value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_hall3);
int platform_class_wireless_get_pen_hall4(unsigned int role, int *value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_hall4);
int platform_class_wireless_get_pen_hall3_s(unsigned int role, int *value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_hall3_s);
int platform_class_wireless_get_pen_hall4_s(unsigned int role, int *value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_hall4_s);
int platform_class_wireless_get_pen_hall_ppe_n(unsigned int role, int *value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_hall_ppe_n);
int platform_class_wireless_get_pen_hall_ppe_s(unsigned int role, int *value) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_hall_ppe_s);
int platform_class_wireless_get_pen_place_err(unsigned int role, int *err) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_pen_place_err);
int platform_class_wireless_set_pen_place_err(unsigned int role, int err) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_pen_place_err);
int platform_class_wireless_set_hboost_enable(unsigned int role, int enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_hboost_enable);
int platform_class_wireless_set_charge_type(unsigned int role, u32 charge_type) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_charge_type);
int platform_class_wireless_get_reverse_chg_en(unsigned int role, int *enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_reverse_chg_en);

MODULE_LICENSE("GPL");
