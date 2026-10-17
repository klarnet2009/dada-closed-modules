/* SPDX-License-Identifier: GPL-2.0 */
/* mca_charge_mievent.h - reconstructed; ids are the values proven by stock code, names inferred from the XRING sources */
#ifndef __MCA_CHARGE_MIEVENT_H__
#define __MCA_CHARGE_MIEVENT_H__

enum charge_mievent_state_ele {
	MIEVENT_STATE_PLUG,
	MIEVENT_STATE_END,
	MIEVENT_STATE_MAX,
};

enum charge_dfx_ele {
	CHARGE_DFX_NOT_STANDARD_ADAPTER = 2,
	CHARGE_DFX_RP_SHORT_VBUS_DETECTED = 3,
	CHARGE_DFX_LPD_DETECTED = 4,
	CHARGE_DFX_CP_IBUS_OCP = 5,
	CHARGE_DFX_CP_VAC_OVP = 6,
	CHARGE_DFX_CP_VBAT_OVP = 7,
	CHARGE_DFX_CP_IBAT_OCP = 8,
	CHARGE_DFX_CP_VBUS_OVP = 9,
	CHARGE_DFX_CP_TDIE_HOT = 0xf,
	CHARGE_DFX_SOC_NOT_FULL = 0xb,
	CHARGE_DFX_BATTERY_AUTH_FAIL = 0x16,
	CHARGE_DFX_BATTERY_AUTH_FAIL_PARALLEL = 0x17,
	CHARGE_DFX_XMPPS_CHARGE_SLOW = 0x25,
	CHARGE_DFX_NONXMPPS_CHARGE = 0x26,
	CHARGE_DFX_BATTERY_CYCLECOUNT = 0x2a,
	CHARGE_DFX_FG_IIC_ERR = 0x2b,
	CHARGE_DFX_BATTERY_TEMP_HOT = 0x2e,
	CHARGE_DFX_BATTERY_TEMP_COLD = 0x2f,
	CHARGE_DFX_BATT_VBAT_DIFF = 0x30,
	CHARGE_DFX_BATTERY_OCD_COUNT = 0x27,
	CHARGE_DFX_BATTERY_CUV_COUNT = 0x28,
	CHARGE_DFX_BATTERY_HSCD_COUNT = 0x29,
	CHARGE_DFX_CP_ABSENT = 0x2c,
	CHARGE_DFX_WLS_TRX_IIC_ERR = 0x31,	/* values unverified */
	CHARGE_DFX_WLS_TRX_UVLO = 0x32,
	CHARGE_DFX_WLS_TRX_OCP = 0x33,
	CHARGE_DFX_WLS_TRX_FOD = 0x34,
};

void mca_charge_mievent_report(int event_index, void *data, int size);
void mca_charge_mievent_set_state(enum charge_mievent_state_ele state, int value);

#endif
