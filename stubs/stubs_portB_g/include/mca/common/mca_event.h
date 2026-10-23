/* SPDX-License-Identifier: GPL-2.0 */
/* G13d WARNING: several enumerator NAMES in this header do not match the OS4 stock BTF (the VALUES used by the built modules are the stock immediates, so the code is right, but e.g. MCA_EVENT_CP_VBUS_OVP here = stock MCA_EVENT_CP_VUSB_OVP). Authoritative: stubs_g13d_g5/include/mca/common/mca_event.h + mca_charge_mievent.h. See work/a17/review/charging_enum_audit.md */
/*
 * mca_event.h - reconstructed from the stock MCA modules (dada).  Enumerator VALUES are proven by the stock code
 * (jump tables / immediates); the NAMES are taken from the XRING sources where the semantics match, else inferred.
 */
#ifndef __MCA_EVENT_H__
#define __MCA_EVENT_H__

#include <linux/notifier.h>

#define MCA_EVENT_NOTIFY_SIZE	128

struct mca_event_notify_data {
	const char *event;
	int event_len;
};

enum mca_event_type {
	MCA_EVENT_TYPE_CHARGER_CONNECT = 0,
	MCA_EVENT_TYPE_CHARGE_TYPE = 1,
	MCA_EVENT_TYPE_BATTERY_INFO = 2,
	MCA_EVENT_TYPE_CP_INFO = 3,
	MCA_EVENT_TYPE_HW_INFO = 4,
	MCA_EVENT_TYPE_CHARGE_STATUS = 5,
	MCA_EVENT_TYPE_THERMAL_TEMP = 6,
	MCA_EVENT_TYPE_PANEL = 7,
	MCA_EVENT_TYPE_DEBUG = 9,
};

enum mca_event_panel {
	MCA_EVENT_PANEL_SCREEN_STATE_CHANGE = 0x40,
	MCA_EVENT_PANEL_HBM_STATE_CHANGE = 0x41,
};

enum mca_event_ele {
	MCA_EVENT_USB_DISCONNECT = 0,
	MCA_EVENT_USB_CONNECT = 1,
	MCA_EVENT_WIRELESS_DISCONNECT = 2,
	MCA_EVENT_WIRELESS_CONNECT = 3,
	MCA_EVENT_WIRELESS_REVCHG = 4,
	MCA_EVENT_CHARGE_TYPE_CHANGE = 5,
	MCA_EVENT_CHARGE_CAP_CHANGE = 6,
	MCA_EVENT_CHARGE_VERIFY_PROCESS_END = 7,
	/* charge pump events: values and names proven by the stock sc8581 (log message next to each notify) */
	MCA_EVENT_CP_VUSB_INSERT = 0x15,
	MCA_EVENT_CP_VUSB_OUT = 0x16,
	MCA_EVENT_CP_VUSB_OVP = 0x17,
	MCA_EVENT_CP_VBAT_OVP = 0x18,
	MCA_EVENT_CP_VBUS_OVP = 0x19,
	MCA_EVENT_CP_VWPC_OVP = 0x1a,
	MCA_EVENT_CP_PMID2OUT_OVP = 0x1b,
	MCA_EVENT_CP_PMID2OUT_UVP = 0x1c,
	MCA_EVENT_CP_IBAT_OCP = 0x1d,
	MCA_EVENT_CP_IBUS_OCP = 0x1e,
	MCA_EVENT_CP_IBUS_UCP = 0x1f,
	MCA_EVENT_CP_CBOOT_FAIL = 0x20,
	MCA_EVENT_CP_VOUT_UVLO = 0x21,
	MCA_EVENT_CP_POR_FLAG = 0x22,
	MCA_EVENT_CP_IIC_ERROR = 0x23,
	MCA_EVENT_CP_TSHUT_FLAG = 0x24,
	MCA_EVENT_BATTERY_STS_CHANGE = 0x11,
	MCA_EVENT_BATTERY_HEALTH_CHANGE = 0x13,
	MCA_EVENT_CONN_ANTIBURN_CHANGE = 0x26,
	MCA_EVENT_BATT_BTB_CHANGE = 0x27,
	MCA_EVENT_BATT_AUTH_PASS = 0x28,
	MCA_EVENT_LPD_STATUS_CHANGE = 0x29,
	MCA_EVENT_PMIC_INIT_DONE = 0x2a,
	MCA_EVENT_CC_SHORT_VBUS = 0x2b,
	MCA_EVENT_VBAT_OVP_CHANGE = 0x2c,
	MCA_EVENT_IBAT_OCP_CHANGE = 0x2d,
	MCA_EVENT_CP_REVERT_CHANGE = 0x2e,
	MCA_EVENT_USB_STS_CHANGE = 0x31,
	MCA_EVENT_CHARGE_ABNORMAL = 0x32,
	MCA_EVENT_CHARGE_RESTORE = 0x33,
	MCA_EVENT_SOC_LIMIT = 0x34,
	MCA_EVENT_BATTERY_DTPT = 0x35,
	MCA_EVENT_CSD_SEND_PULSE = 0x36,
	/* sent by the strategies, not through the notifier chains */
	MCA_EVENT_IS_EU_MODEL = 0x38,
	MCA_EVENT_PLATE_SHOCK = 0x39,
	MCA_EVENT_START_QUICK_REVCHG = 0x3a,
	MCA_EVENT_SET_REVCHG_BCL = 0x3b,
	MCA_EVENT_HANDLE_STATE = 0x3c,
	MCA_EVENT_SINK_PWR_SUSPEND_CHANGE = 0x45,
	MCA_EVENT_USB_SUSPEND = 0x46,
	MCA_EVENT_FG_OTA_PROCESS = 0x49,
	MCA_EVENT_DEBUG_DOUBLE85 = 0x4c,
	MCA_EVENT_DEBUG_REMOVE_TEMP_LIMIT = 0x4d,
	MCA_EVENT_DEBUG_MEMORY_TEST = 0x4e,
	MCA_EVENT_DEBUG_SOC_LIMIT = 0x4f,
	/* GLOBAL OS4.0.0.7 (BTF enum mca_event_notify_list: ..., BATT_SAFE_LVL = 84, BATT_EMERG = 85, MAX = 86) */
	MCA_EVENT_BATT_SAFE_LVL = 0x54,
	MCA_EVENT_BATT_EMERG = 0x55,
};

int mca_event_block_notify_register(unsigned int type, struct notifier_block *nb);
int mca_event_block_notify_unregister(unsigned int type, struct notifier_block *nb);
void mca_event_block_notify(unsigned int type, unsigned long event, void *data);
void mca_event_report_uevent(const struct mca_event_notify_data *n_data);

#endif /* __MCA_EVENT_H__ */
