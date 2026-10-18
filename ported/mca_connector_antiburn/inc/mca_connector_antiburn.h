/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_connector_antiburn.h - connector antiburn driver, OS4 (dada) layout.
 * Member order / offsets from the BTF of the OS4 stock module (struct size 400); names inferred from the XRING sources.
 * SAFETY: community-only charging source, never flash it, never put it into a ROM build.
 */
#ifndef __CONNECTOR_ANTIBURN_H__
#define __CONNECTOR_ANTIBURN_H__

#include <linux/notifier.h>
#include <linux/workqueue.h>

enum connector_antiburn_attr_list {
	CONNECTOR_PROP_TEMP_1,
	CONNECTOR_PROP_TEMP_2,
	CONNECTOR_PROP_TEMP_MAX,
	CONNECTOR_PROP_RESET_VSAFE0V,
	CONNECTOR_PROP_NTC_ALARM,
	CONNECTOR_PROP_MOS_CTRL,
};

struct connector_antiburn {
	struct device *dev;			/* +0 */
	struct thermal_zone_device *tzd_conn;	/* +8 */
	struct thermal_zone_device *tzd_conn2;	/* +16 */
	struct notifier_block thermal_board_nb;	/* +24 */
	struct notifier_block debug_nb;		/* +48 */
	struct notifier_block connect_nb;	/* +72 */
	struct notifier_block hw_nb;		/* +96 */
	int triggered;				/* +120 */
	int is_reset_vsafe0V;
	int ntc_alarm;
	int temperature[CONNECTOR_PROP_TEMP_MAX];	/* +132 */
	int temp_increase_rate[CONNECTOR_PROP_TEMP_MAX];	/* +140 */
	int fake_connector_temp[CONNECTOR_PROP_TEMP_MAX];	/* +148 */
	int thermal_board_temp;			/* +156 */
	int max_thermal_board_temp;
	int comb_sensorboard_con_trigger_temp;
	int comb_rate_conn_trigger_temp;
	int trigger_temp;
	int recharge_temp;
	int mos_ctrl_gpio;			/* +180 */
	int otg_detect_en;
	int support_soft_antiburn;
	int support_hw_antiburn;
	int use_double_ntc;			/* +196 */
	int monitor_interval;			/* +200 */
	int max_temp_increase_rate;
	const char *thermal_zone_name;		/* +208 */
	const char *thermal_zone_name2;
	bool otg_plugin_status;			/* +224 */
	int usb_online;				/* +228 */
	bool cid_status;			/* +232 */
	int otg_boost_src;			/* +236 */
	int en_src;
	int support_base_flip;
	int disable_antiburn;			/* +248 */
	int support_elaboration_anti_strategy;	/* +252 */
	struct wakeup_source *anti_wake_lock;	/* +256 */
	struct delayed_work monitor_work;	/* +264 */
};

_Static_assert(sizeof(struct connector_antiburn) == 400, "connector_antiburn size");
_Static_assert(offsetof(struct connector_antiburn, monitor_work) == 264, "monitor_work");
_Static_assert(offsetof(struct connector_antiburn, mos_ctrl_gpio) == 180, "mos_ctrl_gpio");
_Static_assert(offsetof(struct connector_antiburn, support_elaboration_anti_strategy) == 252, "elaboration");

#endif /* __CONNECTOR_ANTIBURN_H__ */
