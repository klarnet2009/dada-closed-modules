/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PROTOCOL_CLASS_H__
#define __PROTOCOL_CLASS_H__

/* values proven by the stock business charger: pd = 0, pps = 1, bc12 = 5 */
enum adapter_protocol_type {
	ADAPTER_PROTOCOL_PD = 0,
	ADAPTER_PROTOCOL_PPS = 1,
	ADAPTER_PROTOCOL_BC12 = 5,
};

/* charger types (usb_type text index of the stock business charger) */
enum xm_charger_type {
	XM_CHARGER_TYPE_UNKNOW = 0,
	XM_CHARGER_TYPE_SDP = 1,
	XM_CHARGER_TYPE_CDP = 2,
	XM_CHARGER_TYPE_DCP = 3,
	XM_CHARGER_TYPE_FLOAT = 4,
	XM_CHARGER_TYPE_HVDCP2 = 5,
	XM_CHARGER_TYPE_HVDCP3 = 6,
	XM_CHARGER_TYPE_HVDCP3_B = 7,
	XM_CHARGER_TYPE_HVDCP3P5 = 8,
	XM_CHARGER_TYPE_TYPEC = 9,
	XM_CHARGER_TYPE_PD = 10,
	XM_CHARGER_TYPE_PPS = 11,
	XM_CHARGER_TYPE_PD_VERIFY = 12,
	XM_CHARGER_TYPE_ACA = 14,
	XM_CHARGER_TYPE_OCP = 15,
};

int protocol_class_get_adapter_max_power(unsigned int protocol, unsigned int *max_power);
int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value);

#endif
