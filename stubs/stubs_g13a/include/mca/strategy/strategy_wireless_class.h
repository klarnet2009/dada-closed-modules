/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __STRATEGY_WIRELESS_CLASS_H__
#define __STRATEGY_WIRELESS_CLASS_H__

#include <linux/types.h>

enum firmware_update_state {
	FIRMWARE_NO_UPDATE = 0,
	FIRMWARE_UPDATING,
	FIRMWARE_UPDATE_ERROR,
	FIRMWARE_UPDATE_FINISH,
};

enum rev_boost_src {
	PMIC_REV_BOOST = 0,
	PMIC_HBOOST,
	EXTERNAL_BOOST,
	CHARGER_ADAPTER,
	BOOST_SRC_MAX,
};

/* g13a: values from the OS4 BTF of mca_basic_wireless (REV_EN_BOOST is 2; the older stubs said 0) */
enum en_src_cfg {
	OTG_EN_BOOST = 0,
	WIRELESS_EN_BOOST = 1,
	REV_EN_BOOST = 2,
};
typedef enum en_src_cfg EN_SRC;

int mca_wireless_rev_get_fw_update(bool *fw_update);
int mca_wireless_rev_set_usb_plugin(bool wls_sleep_usb_insert);

/* g13a: OS4 additions found in the module-local BTF of mca_basic_wireless / mca_quick_wireless (prototypes of mca_basic_wireless' own definitions;
 * return types of the int ones are the BTF `int`, `rx_iout_limit` returns void; pointer-ness of the T32 parameters is inferred from the call sites) */
struct wls_adapter_power_cap {
	int max_fcc;		/* +0 */
	int max_power;		/* +4, name GUESS (BTF ?str622979; the XRING template calls it max_power) */
};

int strategy_class_wireless_ops_get_adapter_power(struct wls_adapter_power_cap *adapter_power);
int strategy_class_wireless_ops_get_wls_type(int *wls_type);
int strategy_class_wireless_ops_set_parallel_charge(bool parallel_charge_flag);
int strategy_class_wireless_ops_get_adapter_charger_mode(int *cp_charger_mode);
void strategy_class_wireless_op_get_rx_iout_limit(int *rx_iout_limit_ma);
/* OS4: new export of mca_basic_wireless (enables/disables the vbus errorhi/errorlo CP irqs); the XRING platform_class_wireless_notify_cp_status() call of the template does NOT exist in the OS4 stock. Prototype GUESSED (BTF: base type id 78344) */
void strategy_wireless_enable_cp_error_irq(bool enable);

#endif
