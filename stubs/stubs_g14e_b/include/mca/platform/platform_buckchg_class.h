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
	AUX_BUCK_CHARGER,
	MAX_BUCK_CHARGER,
};

/* operations of one buck charger driver (reconstructed from the BTF of the stock mca_platform_buckchg_class.ko, size 664 in OS4) */
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
	int (*set_cp_vendor_id)(void *data, int *val);	/* OS4: new op (+656, struct size 664) */
};

int platform_class_buckchg_ops_register(unsigned int role, void *data, struct platform_class_buckchg_ops *ops);
int platform_class_buckchg_ops_enable_hvdcp(unsigned int role, int en);
int platform_class_buckchg_ops_get_online(unsigned int role, int *online);
int platform_class_buckchg_ops_get_cid_status(unsigned int role, bool *cid_status);
int platform_class_buckchg_ops_is_charge_done(unsigned int role, bool *charge_done);
int platform_class_buckchg_ops_get_hiz_status(unsigned int role, int *hz);
int platform_class_buckchg_ops_get_input_volt_lmt(unsigned int role, int *mV);
int platform_class_buckchg_ops_get_input_curr_lmt(unsigned int role, int *mA);
int platform_class_buckchg_ops_get_bus_curr(unsigned int role, int *bus_curr);
int platform_class_buckchg_ops_get_bus_volt(unsigned int role, int *bus_volt);
int platform_class_buckchg_ops_get_usb_sns_volt(unsigned int role, int *bus_volt);
int platform_class_buckchg_ops_get_ac_volt(unsigned int role, int *mV);
int platform_class_buckchg_ops_get_batt_volt_sns(unsigned int role, int *sns);
int platform_class_buckchg_ops_get_batt_volt(unsigned int role, int *mV);
int platform_class_buckchg_ops_get_batt_curr(unsigned int role, int *mA);
int platform_class_buckchg_ops_get_sys_volt(unsigned int role, int *vsys_min);
int platform_class_buckchg_ops_get_bus_tsns(unsigned int role, int *sns);
int platform_class_buckchg_ops_get_batt_tsns(unsigned int role, int *sns);
int platform_class_buckchg_ops_get_die_temp(unsigned int role, int *temp);
int platform_class_buckchg_ops_get_batt_id(unsigned int role, int *id);
int platform_class_buckchg_ops_get_chg_status(unsigned int role, int *chg_status);
int platform_class_buckchg_ops_get_chg_type(unsigned int role, int *chg_type);
int platform_class_buckchg_ops_get_term_curr(unsigned int role, int *term_curr);
int platform_class_buckchg_ops_get_term_volt(unsigned int role, int *term_volt);
int platform_class_buckchg_ops_get_wls_curr(unsigned int role, int *wls_curr);
int platform_class_buckchg_ops_set_hiz(unsigned int role, bool en);
int platform_class_buckchg_ops_set_wls_hiz(unsigned int role, bool en);
int platform_class_buckchg_ops_set_input_curr_lmt(unsigned int role, int ma);
int platform_class_buckchg_ops_set_wls_input_curr_lmt(unsigned int role, int ma);
int platform_class_buckchg_ops_set_input_volt_lmt(unsigned int role, int mv);
int platform_class_buckchg_ops_set_ichg(unsigned int role, int ma);
int platform_class_buckchg_ops_set_chg(unsigned int role, bool en);
int platform_class_buckchg_ops_set_buck_fsw(unsigned int role, int mv);
int platform_class_buckchg_ops_set_otg_curr(unsigned int role, int ma);
int platform_class_buckchg_ops_set_otg_volt(unsigned int role, int mv);
int platform_class_buckchg_ops_set_term(unsigned int role, bool en);
int platform_class_buckchg_ops_set_term_curr(unsigned int role, int ma);
int platform_class_buckchg_ops_set_term_volt(unsigned int role, int mv);
int platform_class_buckchg_ops_adc_enable(unsigned int role, bool en);
int platform_class_buckchg_ops_set_prechg_volt(unsigned int role, int mv);
int platform_class_buckchg_ops_set_prechg_curr(unsigned int role, int ma);
int platform_class_buckchg_ops_force_dpdm(unsigned int role, int en);
int platform_class_buckchg_ops_request_dpdm(unsigned int role, bool en);
int platform_class_buckchg_ops_set_wd_timeout(unsigned int role, int ms);
int platform_class_buckchg_ops_kick_wd(unsigned int role);
int platform_class_buckchg_ops_set_qc_volt(unsigned int role, int mv);
int platform_class_buckchg_ops_set_usb_aicl_cont_thd(unsigned int role, int mv);
int platform_class_buckchg_ops_get_usb_aicl_cont_thd(unsigned int role, int *mv);
int platform_class_buckchg_ops_set_opt_fws(unsigned int role, int mv);
int platform_class_buckchg_ops_usb_adapter_allow_override(unsigned int role, bool en);
int platform_class_buckchg_ops_set_qc3_volt(unsigned int role, int mv);
int platform_class_buckchg_ops_get_otg_boost_src(unsigned int role, int *otg_boost_src);
int platform_class_buckchg_ops_get_otg_boost_enable_status(unsigned int role, int *otg_boost_enable_sts);
int platform_class_buckchg_ops_get_otg_gate_enable_status(unsigned int role, int *otg_gate_enable_sts);
int platform_class_buckchg_ops_set_boost_enable(unsigned int role, int src_enable);
int platform_class_buckchg_ops_set_boost_voltage(unsigned int role, int src_value);
int platform_class_buckchg_ops_set_aicl_enable(unsigned int role, bool en);
int platform_class_buckchg_ops_set_rerun_aicl(unsigned int role, bool en);
int platform_class_buckchg_ops_is_support_cid(unsigned int role, bool *en);
int platform_class_buckchg_ops_set_ship_mode(unsigned int role, bool en);
int platform_class_buckchg_ops_get_ship_mode(unsigned int role, bool *en);
int platform_class_buckchg_ops_set_wls_vdd_flag(unsigned int role, bool en);
int platform_class_buckchg_ops_get_lpd_enable(unsigned int role, int *lpd_en);
int platform_class_buckchg_ops_get_lpd_status(unsigned int role, int *lpd_status);
int platform_class_buckchg_ops_get_lpd_sbu1(unsigned int role, int *lpd_sbu1);
int platform_class_buckchg_ops_get_lpd_sbu2(unsigned int role, int *lpd_sbu2);
int platform_class_buckchg_ops_get_lpd_cc1(unsigned int role, int *lpd_cc1);
int platform_class_buckchg_ops_get_lpd_cc2(unsigned int role, int *lpd_cc2);
int platform_class_buckchg_ops_get_lpd_dp(unsigned int role, int *lpd_dp);
int platform_class_buckchg_ops_get_lpd_dm(unsigned int role, int *lpd_dm);
int platform_class_buckchg_ops_set_lpd_sbu1(unsigned int role, int lpd_sbu1);
int platform_class_buckchg_ops_set_lpd_control(unsigned int role, int lpd_control);
int platform_class_buckchg_ops_get_lpd_control(unsigned int role, int *lpd_control);
int platform_class_buckchg_ops_set_lpd_uart_control(unsigned int role, int lpd_uart_control);
int platform_class_buckchg_ops_get_lpd_uart_control(unsigned int role, int *lpd_uart_control);
int platform_class_buckchg_ops_get_pack_vbat(unsigned int role, int *pvbat);
int platform_class_buckchg_ops_set_eu_model(unsigned int role, bool en);
int platform_class_buckchg_ops_is_init_ok(unsigned int role);
int platform_class_buckchg_ops_set_otg(unsigned int role, bool otg_en);
int platform_class_buckchg_ops_get_pack_ibat(unsigned int role, int *pibat);
int platform_class_buckchg_ops_get_pack_tbat(unsigned int role, int *ptbat);
int platform_class_buckchg_ops_get_aicl_status(unsigned int role, int *aicl_status);
int platform_class_buckchg_ops_set_restart_aicl(unsigned int role, bool en);
int platform_class_buckchg_ops_set_too_hot_limit(unsigned int role, int limit);



#endif /* __PLATFORM_BUCKCHG_CLASS_H__ */
