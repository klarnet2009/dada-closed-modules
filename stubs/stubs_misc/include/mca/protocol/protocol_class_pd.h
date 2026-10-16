/* SPDX-License-Identifier: GPL-2.0 */
/*
 * protocol_class_pd.h - interface of the Xiaomi MCA pd protocol class (mca_protocol_pd_class.ko).
 * Reconstructed from the BTF of the stock vendor_dlkm/mca_protocol_pd_class.ko (struct protocol_class_pd_ops with all
 * 50 slots) plus the Xiaomi MCA reference sources; field names that BTF could not give (hashed strings) are taken
 * from the reference sources (min_volt/max_volt, digest, max_power, nums/cap).
 */
#ifndef __MCA_PROTOCOL_CLASS_PD_H__
#define __MCA_PROTOCOL_CLASS_PD_H__

#include <linux/types.h>

#define USBPD_UVDM_SS_LEN	4

enum uvdm_state {
	USBPD_UVDM_DISCONNECT,
	USBPD_UVDM_CHARGER_VERSION,
	USBPD_UVDM_CHARGER_VOLTAGE,
	USBPD_UVDM_CHARGER_TEMP,
	USBPD_UVDM_SESSION_SEED,
	USBPD_UVDM_AUTHENTICATION,
	USBPD_UVDM_VERIFIED,
	USBPD_UVDM_REMOVE_COMPENSATION,
	USBPD_UVDM_REVERSE_AUTHEN,
	USBPD_UVDM_CONNECT,
	USBPD_UVDM_NAN_ACK,
	USBPD_UVDM_CMD_INIT = 50,
	USBPD_UVDM_CMD_NAK = 100,
	USBPD_UVDM_REQUEST_PD_DR = 200,
	USBPD_UVDM_RESET_VSAFE0V = 0xFF,
};

struct usbpd_vdm_data {
	int ta_version;
	int ta_temp;
	int ta_voltage;
	int reauth;
	unsigned long s_secert[USBPD_UVDM_SS_LEN];
	unsigned long digest[USBPD_UVDM_SS_LEN];
};

struct pd_pdo {
	int min_volt;
	int max_volt;
	int max_current;
};

struct adapter_power_cap {
	int min_current;
	int max_current;
	int min_voltage;
	int max_voltage;
	int max_power;
};

struct protocol_class_pd_ops {
	int (*protocol_pd_pps_get_max_power)(unsigned int *, void *);
	int (*protocol_pd_pps_pdo_select)(unsigned int, unsigned int, void *);
	int (*protocol_pd_get_pps_ptf)(int *, void *);
	int (*protocol_pd_fixed_pdo_set_vol)(unsigned int, void *);
	int (*protocol_pd_set_gear_shift)(int, void *);
	int (*protocol_pd_set_pps_max_cur)(unsigned int, void *);
	int (*protocol_pd_get_pps_max_cur)(unsigned int *, void *);
	int (*protocol_pd_get_pps_status)(unsigned int *, unsigned int *, void *);
	int (*protocol_pd_set_pd_active)(int, void *);
	int (*protocol_pd_get_pd_active)(int *, void *);
	int (*protocol_pd_set_pps_min_volt)(unsigned int, void *);
	int (*protocol_pd_get_pps_min_volt)(unsigned int *, void *);
	int (*protocol_pd_set_pps_max_volt)(unsigned int, void *);
	int (*protocol_pd_get_pps_max_volt)(unsigned int *, void *);
	int (*protocol_pd_set_pps_apdo_max)(unsigned int, void *);
	int (*protocol_pd_get_pps_apdo_max)(unsigned int *, void *);
	int (*protocol_pd_get_pd_type)(int *, void *);
	int (*protocol_pd_set_typec_mode)(int, void *);
	int (*protocol_pd_get_typec_mode)(int *, void *);
	int (*protocol_pd_get_typec_cc_orientation)(int *, void *);
	int (*protocol_pd_set_typec_cc_orientation)(int, void *);
	int (*protocol_pd_set_in_hard_reset)(int, void *);
	int (*protocol_pd_get_in_hard_reset)(int *, void *);
	int (*protocol_pd_set_usb_suspend_supported)(int, void *);
	int (*protocol_pd_get_usb_suspend_supported)(int *, void *);
	int (*protocol_pd_set_pd_typec_accessory_mode)(int, void *);
	int (*protocol_pd_get_pd_typec_accessory_mode)(int *, void *);
	int (*protocol_pd_get_cap)(int, struct adapter_power_cap *, void *);
	int (*protocol_pd_get_adapter_id)(unsigned int *, void *);
	int (*protocol_pd_get_adapter_svid)(unsigned int *, void *);
	int (*protocol_pd_get_has_dp)(bool *, void *);
	int (*protocol_pd_request_vdm_cmd)(enum uvdm_state, unsigned int *, unsigned int, void *);
	int (*protocol_pd_get_vdm_cmd)(int *, struct usbpd_vdm_data *, void *);
	int (*protocol_pd_get_power_role)(unsigned char *, void *);
	int (*protocol_pd_get_data_role)(unsigned char *, void *);
	int (*protocol_pd_get_current_state)(char *, int, void *);
	int (*protocol_pd_get_pdos)(struct pd_pdo *, int, void *);
	int (*protocol_pd_set_verify_process)(int, void *);
	int (*protocol_pd_get_verify_process)(int *, void *);
	int (*protocol_pd_set_pd_verifed)(int, void *);
	int (*protocol_pd_get_pd_verifed)(int *, void *);
	int (*protocol_pd_get_cid_status)(bool *, void *);
	int (*protocol_pd_get_otg_plugin_status)(bool *, void *);
	int (*protocol_pd_set_cc_toggle)(bool, void *);
	int (*protocol_pd_get_cc_toggle)(bool *, void *);
	int (*protocol_pd_get_snk_src_mode)(int *, void *);
	int (*protocol_pd_get_cc_status)(bool *, void *);
	int (*protocol_pd_get_cc_short_vbus)(int *, void *);
	int (*protocol_pd_get_suspend_support_status)(bool *, void *);
	int (*protocol_pd_get_zimi_cypress_flag)(int *, void *);
};

int protocol_class_pd_register_ops(unsigned int port_num, struct protocol_class_pd_ops *ops, void *data);

#endif /* __MCA_PROTOCOL_CLASS_PD_H__ */
