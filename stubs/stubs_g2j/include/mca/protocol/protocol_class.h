/* SPDX-License-Identifier: GPL-2.0 */
/* protocol_class.h - reconstructed public API of mca_protocol_class.ko (layouts from the BTF of the stock module, names from the sources) */
#ifndef __PROTOCOL_CLASS_H__
#define __PROTOCOL_CLASS_H__

#include <linux/types.h>

/* g_protocol_class_data is 96 bytes in the stock module: 6 protocols of 16 bytes */
enum adapter_protocol_type {
	ADAPTER_PROTOCOL_PD = 0,
	ADAPTER_PROTOCOL_PPS = 1,
	ADAPTER_PROTOCOL_UFCS = 2,
	ADAPTER_PROTOCOL_QC = 3,
	ADAPTER_PROTOCOL_QI2 = 4,
	ADAPTER_PROTOCOL_BC12 = 5,
	ADAPTER_PROTOCOL_MAX = 6,
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

#define ADAPTER_POWER_CAP_MAX 7

struct adapter_power_cap {
	int min_current;
	int max_current;
	int min_voltage;
	int max_voltage;
	int max_power;
};

struct adapter_power_cap_info {
	int nums;
	struct adapter_power_cap cap[ADAPTER_POWER_CAP_MAX];
};

/*
 * The stock BTF references the member names of the next two structs only as unresolved offsets into the vmlinux string table,
 * so the real spelling is unknown.  The names below are guesses; the GENKSYMS-ONLY pad members make genksyms reproduce the stock
 * export CRCs (they never reach the compiler: layout and code are unaffected).  See scripts_mcaB/padcrc.py.
 */
struct adapter_vendor_info {
	int vid;
	int pid;
	int xid;
	int svid;
#ifdef __GENKSYMS__
	int fcrcnEmrsLs01J;
#endif
};

struct adapter_power_curve {
	int pwr_curve_num;
	int volt[5];
	int curr[5];
	int power[5];
#ifdef __GENKSYMS__
	int fcrcJZ207qeJFs;
#endif
};

struct adapter_protocol_class_ops {
	int (*adapter_det_en)(void *data, int en);
	int (*set_adapter_verified)(void *data, int verified);
	int (*get_adapter_verified)(void *data, int *verified);
	int (*get_adapter_type)(void *data, int *type);
	int (*get_adapter_max_power)(void *data, unsigned int *max_power);
	int (*get_adapter_pwr_cap)(void *data, struct adapter_power_cap_info *cap);
	int (*set_adapter_volt_and_curr)(void *data, int volt, int curr);
	int (*get_adapter_volt_and_curr)(void *data, int *volt, int *curr);
	int (*get_adapter_pps_ptf)(void *data, int *pps_ptf);
	int (*get_adapter_info)(void *data, struct adapter_vendor_info *info);
	int (*get_adapter_power_curve)(void *data, struct adapter_power_curve *pwr_curve);
	int (*get_adapter_pwr_max_power)(void *data, unsigned int *max_power);
#ifdef __GENKSYMS__
	int fcrczDYNCKSUIV;	/* CRC pad (see above) */
#endif
};

int protocol_class_register_ops(unsigned int protocol, struct adapter_protocol_class_ops *ops, void *data);
int protocol_class_set_adapter_verified(unsigned int protocol, int verified);
int protocol_class_get_adapter_verified(unsigned int protocol, int *verified);
int protocol_class_get_adapter_max_power(unsigned int protocol, unsigned int *max_power);
int protocol_class_get_adapter_pwr_max_power(unsigned int protocol, unsigned int *max_power);
int protocol_class_det_adapter_type(unsigned int protocol, int en);
int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value);
int protocol_class_get_adapter_power_cap(unsigned int protocol, struct adapter_power_cap_info *cap);
int protocol_class_set_adapter_volt_and_curr(unsigned int protocol, int volt, int curr);
int protocol_class_get_adapter_volt_and_curr(unsigned int protocol, int *volt, int *curr);
int protocol_class_get_adapter_pps_ptf(unsigned int protocol, int *pps_ptf);
int protocol_class_get_adapter_info(unsigned int protocol, struct adapter_vendor_info *info);
int protocol_class_get_adapter_power_curve(unsigned int protocol, struct adapter_power_curve *pwr_curve);

#endif
