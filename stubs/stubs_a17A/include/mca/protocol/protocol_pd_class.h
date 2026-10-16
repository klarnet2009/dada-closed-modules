/* SPDX-License-Identifier: GPL-2.0 */
/*
 * protocol_pd_class.h - reconstructed API of mca_protocol_pd_class.ko.
 * Prototypes: XRING sources (mca_protocol_pd_class.c) + CRC check against the stock exporter;
 * types: stubs_misc/protocol_class_pd.h (BTF of the stock provider).
 */
#ifndef __MCA_PROTOCOL_PROTOCOL_PD_CLASS_H__
#define __MCA_PROTOCOL_PROTOCOL_PD_CLASS_H__

#include <linux/types.h>
#include <mca/protocol/protocol_class.h>

#define PROTOCOL_PD_MAX_STRING_LEN	64
#define PROTOCOL_PD_MAX_PDO_NUMS	7
#define USBPD_UVDM_SS_LEN		4

enum typec_port_num {
	TYPEC_PORT_0 = 0,
	TYPEC_PORT_1 = 1,
	TYPEC_PORT_MAX,
};

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

enum xm_request_pd_dr {
	XM_REQUEST_PD_DR_UFP = 1,
	XM_REQUEST_PD_DR_DFP = 2,
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


int protocol_class_pd_get_port_num(void);
int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status);
int protocol_class_pd_get_otg_plugin_status(unsigned int port_num, bool *status);
int protocol_class_pd_get_cc_toggle(unsigned int port_num, bool *en);
int protocol_class_pd_set_cc_toggle(unsigned int port_num, bool en);
int protocol_class_pd_get_has_dp(unsigned int port_num, bool *has_dp);
int protocol_class_pd_get_pd_active(unsigned int port_num, int *pd_active);
int protocol_class_pd_set_pd_active(unsigned int port_num, int pd_active);
int protocol_class_pd_get_adapter_id(unsigned int port_num, unsigned int *adapter_id);
int protocol_class_pd_get_adapter_svid(unsigned int port_num, unsigned int *adapter_svid);
int protocol_class_pd_request_vdm_cmd(unsigned int port_num, enum uvdm_state cmd,
	unsigned int *data, unsigned int data_len);
int protocol_class_pd_get_vdm_cmd(unsigned int port_num, int *cmd, struct usbpd_vdm_data *vdm_data);
int protocol_class_pd_get_power_role(unsigned int port_num, unsigned char *pr);
int protocol_class_pd_get_data_role(unsigned int port_num, unsigned char *pr);
int protocol_class_pd_get_current_state(unsigned int port_num, char *current_state, int len);
int protocol_class_pd_get_pdos(unsigned int port_num, struct pd_pdo *pdos, int count);
int protocol_class_pd_set_verify_process(unsigned int port_num, int verify_process);
int protocol_class_pd_get_verify_process(unsigned int port_num, int *verify_process);
int protocol_class_pd_set_pd_verifed(unsigned int port_num, int pd_verifed);
int protocol_class_pd_get_pd_verifed(unsigned int port_num, int *pd_verifed);

#endif /* __MCA_PROTOCOL_PROTOCOL_PD_CLASS_H__ */
