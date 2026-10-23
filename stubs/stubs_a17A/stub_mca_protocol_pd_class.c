// SPDX-License-Identifier: GPL-2.0
/* generated stub provider: only the export CRCs matter */
#include <linux/module.h>
#include <mca/protocol/protocol_pd_class.h>

int protocol_class_pd_get_port_num(void) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_port_num);
int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_cid_status);
int protocol_class_pd_get_otg_plugin_status(unsigned int port_num, bool *status) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_otg_plugin_status);
int protocol_class_pd_get_cc_toggle(unsigned int port_num, bool *en) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_cc_toggle);
int protocol_class_pd_set_cc_toggle(unsigned int port_num, bool en) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_set_cc_toggle);
int protocol_class_pd_get_has_dp(unsigned int port_num, bool *has_dp) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_has_dp);
int protocol_class_pd_get_pd_active(unsigned int port_num, int *pd_active) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_pd_active);
int protocol_class_pd_set_pd_active(unsigned int port_num, int pd_active) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_set_pd_active);
int protocol_class_pd_get_adapter_id(unsigned int port_num, unsigned int *adapter_id) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_adapter_id);
int protocol_class_pd_get_adapter_svid(unsigned int port_num, unsigned int *adapter_svid) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_adapter_svid);
int protocol_class_pd_request_vdm_cmd(unsigned int port_num, enum uvdm_state cmd, unsigned int *data, unsigned int data_len) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_request_vdm_cmd);
int protocol_class_pd_get_vdm_cmd(unsigned int port_num, int *cmd, struct usbpd_vdm_data *vdm_data) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_vdm_cmd);
int protocol_class_pd_get_power_role(unsigned int port_num, unsigned char *pr) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_power_role);
int protocol_class_pd_get_data_role(unsigned int port_num, unsigned char *pr) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_data_role);
int protocol_class_pd_get_current_state(unsigned int port_num, char *current_state, int len) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_current_state);
int protocol_class_pd_get_pdos(unsigned int port_num, struct pd_pdo *pdos, int count) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_pdos);
int protocol_class_pd_set_verify_process(unsigned int port_num, int verify_process) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_set_verify_process);
int protocol_class_pd_get_verify_process(unsigned int port_num, int *verify_process) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_verify_process);
int protocol_class_pd_set_pd_verifed(unsigned int port_num, int pd_verifed) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_set_pd_verifed);
int protocol_class_pd_get_pd_verifed(unsigned int port_num, int *pd_verifed) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_pd_verifed);

MODULE_LICENSE("GPL");
