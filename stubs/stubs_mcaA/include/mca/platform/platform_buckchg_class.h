/* SPDX-License-Identifier: GPL-2.0 */
/*
 * platform_buckchg_class.h - buck charger platform class API (provider: mca_platform_buckchg_class.ko).
 * Only the part needed by the reconstructed modules so far.
 */
#ifndef __PLATFORM_BUCKCHG_CLASS_H__
#define __PLATFORM_BUCKCHG_CLASS_H__

#include <linux/types.h>

enum buck_charger_role {
	MAIN_BUCK_CHARGER = 0,
};

/* operations of one buck charger driver (reconstructed from the BTF of the stock mca_platform_buckchg_class.ko, size 656) */
struct platform_class_buckchg_ops {
	int (*is_init_ok)(void *data);
	int (*enable_hvdcp)(void *data, int val);
	int (*get_online)(void *data, int *val);
	int (*is_charge_done)(void *data, bool *val);
	int (*get_hiz_status)(void *data, int *val);
	int (*get_input_volt_lmt)(void *data, int *val);
	int (*get_input_curr_lmt)(void *data, int *val);
	int (*get_bus_curr)(void *data, int *val);
	int (*get_bus_volt)(void *data, int *val);
	int (*get_usb_sns_volt)(void *data, int *val);
	int (*get_ac_volt)(void *data, int *val);
	int (*get_batt_volt_sns)(void *data, int *val);
	int (*get_batt_volt)(void *data, int *val);
	int (*get_batt_curr)(void *data, int *val);
	int (*get_sys_volt)(void *data, int *val);
	int (*get_bus_tsns)(void *data, int *val);
	int (*get_batt_tsns)(void *data, int *val);
	int (*get_die_temp)(void *data, int *val);
	int (*get_batt_id)(void *data, int *val);
	int (*get_chg_status)(void *data, int *val);
	int (*get_chg_type)(void *data, int *val);
	int (*get_term_curr)(void *data, int *val);
	int (*get_term_volt)(void *data, int *val);
	int (*get_wls_curr)(void *data, int *val);
	int (*set_hiz)(void *data, bool val);
	int (*set_wls_hiz)(void *data, bool val);
	int (*set_input_curr_lmt)(void *data, int val);
	int (*set_wls_input_curr_lmt)(void *data, int val);
	int (*set_input_volt_lmt)(void *data, int val);
	int (*set_ichg)(void *data, int val);
	int (*set_chg)(void *data, bool val);
	int (*set_buck_fsw)(void *data, int val);
	int (*set_otg)(void *data, bool val);
	int (*set_otg_curr)(void *data, int val);
	int (*set_otg_volt)(void *data, int val);
	int (*set_term)(void *data, bool val);
	int (*set_term_curr)(void *data, int val);
	int (*set_term_volt)(void *data, int val);
	int (*adc_enable)(void *data, bool val);
	int (*set_prechg_volt)(void *data, int val);
	int (*set_prechg_curr)(void *data, int val);
	int (*force_dpdm)(void *data, int val);
	int (*request_dpdm)(void *data, bool val);
	int (*set_wd_timeout)(void *data, int val);
	int (*kick_wd)(void *data);
	int (*set_qc_volt)(void *data, int val);
	int (*set_usb_aicl_cont_thd)(void *data, int val);
	int (*get_usb_aicl_cont_thd)(void *data, int *val);
	int (*set_opt_fws)(void *data, int val);
	int (*usb_adapter_allow_override)(void *data, bool val);
	int (*set_qc3_volt)(void *data, int val);
	int (*get_otg_boost_src)(void *data, int *val);
	int (*get_otg_gate_enable_status)(void *data, int *val);
	int (*get_otg_boost_enable_status)(void *data, int *val);
	int (*set_boost_enable)(void *data, int val);
	int (*set_boost_voltage)(void *data, int val);
	int (*set_aicl_enable)(void *data, bool val);
	int (*set_rerun_aicl)(void *data, bool val);
	int (*set_restart_aicl)(void *data, bool val);
	int (*is_support_cid)(void *data, bool *val);
	int (*set_ship_mode)(void *data, bool val);
	int (*get_ship_mode)(void *data, bool *val);
	int (*set_wls_vdd_flag)(void *data, bool val);
	int (*get_lpd_enable)(void *data, int *val);
	int (*get_lpd_status)(void *data, int *val);
	int (*get_lpd_sbu1)(void *data, int *val);
	int (*get_lpd_sbu2)(void *data, int *val);
	int (*get_lpd_cc1)(void *data, int *val);
	int (*get_lpd_cc2)(void *data, int *val);
	int (*get_lpd_dp)(void *data, int *val);
	int (*get_lpd_dm)(void *data, int *val);
	int (*set_lpd_sbu1)(void *data, int val);
	int (*set_lpd_control)(void *data, int val);
	int (*get_lpd_control)(void *data, int *val);
	int (*set_lpd_uart_control)(void *data, int val);
	int (*get_lpd_uart_control)(void *data, int *val);
	int (*get_pack_vbat)(void *data, int *val);
	int (*get_pack_ibat)(void *data, int *val);
	int (*set_eu_model)(void *data, bool val);
	int (*get_aicl_status)(void *data, int *val);
	int (*set_too_hot_limit)(void *data, int val);
	int (*get_pack_tbat)(void *data, int *val);
};

int platform_class_buckchg_ops_register(unsigned int role, void *data, struct platform_class_buckchg_ops *ops);
int platform_class_buckchg_ops_get_chg_type(unsigned int role, int *chg_type);
int platform_class_buckchg_ops_is_support_cid(unsigned int role, bool *en);
int platform_class_buckchg_ops_get_pack_ibat(unsigned int role, int *pibat);
int platform_class_buckchg_ops_get_ship_mode(unsigned int role, bool *en);
int platform_class_buckchg_ops_set_ship_mode(unsigned int role, bool en);

#endif /* __PLATFORM_BUCKCHG_CLASS_H__ */
