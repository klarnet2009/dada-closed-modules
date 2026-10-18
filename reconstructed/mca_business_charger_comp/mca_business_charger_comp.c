// SPDX-License-Identifier: GPL-2.0
/*
 * mca_business_charger_comp - Xiaomi MCA charger business driver (Xiaomi 15 "dada", Qualcomm platform variant)
 *
 * Reconstructed by reverse engineering from the stock mca_business_charger_comp.ko; the XRING sources of the same vendor
 * code base (mca_business_charger.c / mca_charger_usb_psy.c / mca_charger_wireless_psy.c) were used as the template.
 * The three original source files are merged into this one translation unit (the stock module is an LTO link of them);
 * "#line" directives reproduce the __LINE__ values baked into the log calls of the original files.
 */
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_voter.h>
#include <mca/protocol/protocol_pd_class.h>

/* provider: fsa4480-i2c.ko (audio switch); the stock OS4 enum fsa_function has values 4 and 5 that the GKI header lacks */
int fsa4480_switch_event(struct device_node *node, int event);
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/strategy/strategy_wireless_class.h>
#include <mca/hardware/hw_connector_antiburn.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/hardware/hw_lpd_detect.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/shared_memory/charger_partition_class.h>

/*
 * The stock module was linked with the LTO module.lds, which merges all .bss.* input sections into one .bss.  The zero-sized
 * lock_class_key of init_waitqueue_head() in probe() therefore shares its address (and, being listed first in the symbol table,
 * its name) with the next static, business_charger_process_online_change.active_flag.  Without that merge the key and
 * active_flag stay in separate sections, so put the key into active_flag's (-fdata-sections) section: same codegen, same
 * relocation target names.
 */
#undef init_waitqueue_head
#define init_waitqueue_head(wq_head)						\
do {										\
	static struct lock_class_key __key					\
		__attribute__((section(".bss.business_charger_process_online_change.active_flag")));	\
	__init_waitqueue_head((wq_head), #wq_head, &__key);			\
} while (0)

/* ---------------------------------------------------------------- mca_charger_usb_psy.h / mca_charger_wireless_psy.h */
struct mca_votable;
struct usb_psy_info {
	struct device *dev;
	struct power_supply_desc *usb_psy_desc;
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	struct mca_votable *input_suppend_voter;
	int present;
	int online;
	int last_allow_charge;
};

struct wireless_psy_info {
	struct device *dev;
	struct power_supply_desc *wireless_psy_desc;
	struct power_supply *wireless_psy;
	struct power_supply *batt_psy;
	int present;
	int online;
	int ibat;
	int vbat;
};

struct usb_psy_info *business_charger_usb_psy_init(struct device *dev);
noinline void business_charger_usb_psy_deinit(struct usb_psy_info *info);
noinline void business_charger_update_usb_psy(int real_type, struct usb_psy_info *info);
noinline void business_usb_psy_event_process(struct usb_psy_info *info);

struct wireless_psy_info *business_charger_wireless_psy_init(struct device *dev);
noinline void business_charger_wireless_psy_deinit(struct wireless_psy_info *info);
noinline void business_charger_update_wireless_psy(struct wireless_psy_info *info);

/* ---------------------------------------------------------------- mca_business_charger.h */
enum business_charger_attr_list {
	BUSINESS_CHARGER_DEBUG_PROP_DAM_TEST = 0,
};

struct chg_dt_props {
	int test;
};

struct charger_event_lis_node {
	struct list_head lnode;
	unsigned int event;
	void *data;
};

union charger_abnormal_info {
	u32 asuint32;
	struct {
		u32 batt_auth_failed	: 1;
		u32 batt_missing	: 1;
		u32 batt_cold		: 1;
		u32 batt_overhot	: 1;
		u32 reserved		: 26;
	};
};

struct business_charger {
	struct device *dev;
	struct chg_dt_props dt;
	struct usb_psy_info *usb_psy_info;
	struct wireless_psy_info *wls_psy_info;
	struct notifier_block connect_nb;
	struct notifier_block type_change_nb;
	struct notifier_block hw_info_nb;
	struct notifier_block chg_sts_nb;
	struct notifier_block cp_info_nb;
	struct notifier_block debug_nb;
	struct list_head header;
	spinlock_t list_lock;
	wait_queue_head_t wait_que;
	struct wakeup_source *online_wake_lock;
	struct delayed_work delay_report_status_work;
	struct delayed_work delay_enable_rx_work;
	struct delayed_work reset_rx_work;
	struct delayed_work report_quick_charge_type_work;
	struct delayed_work delay_rerun_usb_sns_work;
	struct delayed_work pmic_init_done_notify_work;
	union charger_abnormal_info abnormal_info;
	int wired_qucik_charge_type;
	int wired_power_max;
	int bc12_active;
	int pd_active;
	int bc12_type;
	int pd_type;
	int real_type;
	int thread_active;
	int wls_active;
	int wls_adapter_type;
	int otg_present;
	int dam_test_flag;
	int is_eu_model;
	int plate_shock;
	int start_quick_revchg;
	int revchg_bcl;
	int handle_state;
	int stop_handle_charge;
	int support_wls;
	int cp_type;
	int verify_process_end;
	ktime_t disconnect_time;
	bool enable_broken_charge_check;
	bool keep_report_max_power;
};

/* charger_partition_class.h (as far as used here) */
enum { CHARGER_PARTITION_HOST_KERNEL = 4, CHARGER_PARTITION_INFO_2 = 2 };
typedef struct {
	u32 eu_mode;
	u32 reserved0[5];
	u32 test;
	u32 reserved;
} charger_partition_info_2;

enum mca_charger_partition_type {
	MCA_CHARGER_PARTITION_TEST,
	MCA_CHARGER_PARTITION_POWEROFFMODE,
	MCA_CHARGER_PARTITION_EU_MODE,
};

#define BUSINESS_CHARGER_THREAD_ACTIVE	1

#define QUICK_CHG_TYPE_FLASH_CHG_POWER	20
#define QUICK_CHG_TYPE_TURBO_CHG_POWER	30
#define QUICK_CHG_TYPE_SUPER_CHG_POWER	50

enum adp_icon_type {
	ADP_ICON_TYPE_NORMAL = 0,
	ADP_ICON_TYPE_FAST = 1,
	ADP_ICON_TYPE_FLASH = 2,
	ADP_ICON_TYPE_TURBO = 3,
	ADP_ICON_TYPE_SUPER = 4,
};

/* G13d: MCA_EVENT_SBU_LPD_CHANGE (83 = 0x53) and MCA_EVENT_BOOST_STS (47 = 0x2f) are stock BTF enumerators (stubs_g13d_g5) */

bool mca_strategy_is_support_pps_100w(void);

#undef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_business_charger"

static void business_charger_report_wired_quick_charge_type(struct business_charger *charger, int icon_type);

/* ---------------------------------------------------------------- mca_business_charger.c */
static void update_usb_pd_type(struct business_charger *charger)
{
	static int last_type;
	int cur_type;

	protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12, &charger->bc12_type);
	protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &charger->pd_type);
#line 105
	mca_log_info("last_bc12_type = %d, bc12_type = %d, pd_type = %d\n", last_type, charger->bc12_type, charger->pd_type);
	if (!charger->bc12_active) {
		cur_type = XM_CHARGER_TYPE_UNKNOW;
	} else {
		if (charger->pd_type)
			cur_type = charger->pd_type;
		else
			cur_type = charger->bc12_type;
	}

	if (last_type != cur_type) {
#line 116
		mca_log_err("real_type changed: %d => %d\n", last_type, cur_type);
		charger->real_type = cur_type;
		business_charger_update_usb_psy(charger->real_type, charger->usb_psy_info);
		if (charger->real_type == XM_CHARGER_TYPE_FLOAT)
			mca_charge_mievent_report(CHARGE_DFX_NOT_STANDARD_ADAPTER, NULL, 0);
		last_type = cur_type;
	}
}

static int business_charger_parse_dt(struct business_charger *charger)
{
	struct device_node *node = charger->dev->of_node;
	int ret, i;
	struct {
		const char *name;
		int *val;
	} chg_data[] = {
		{"business,charger-core-test", &charger->dt.test},
	};

	for (i = 0; i < ARRAY_SIZE(chg_data); i++) {
		ret = of_property_read_u32(node, chg_data[i].name, chg_data[i].val);
		if (ret < 0) {
#line 139
			mca_log_err("not find property %s\n", chg_data[i].name);
			return ret;
		}

#line 143
		mca_log_info("%s: %d\n", chg_data[i].name, *chg_data[i].val);
	}

	(void)mca_parse_dts_u32(node, "support-wls", &charger->support_wls, 0);
#line 146
	mca_log_info("support-wls %d\n", charger->support_wls);
	(void)mca_parse_dts_u32(node, "cp_type", &charger->cp_type, 0);
#line 148
	mca_log_err("cp_type %d\n", charger->cp_type);

	return 0;
}

static void business_charger_add_event_node(unsigned long event, void *data, int size,
	struct business_charger *charger)
{
	struct charger_event_lis_node *event_node = NULL;

	event_node = kmalloc(sizeof(*event_node), GFP_ATOMIC);
	if (!event_node)
		return;

	event_node->event = event;
	event_node->data = NULL;
	if (data) {
		event_node->data = kmalloc(size, GFP_ATOMIC);
		memcpy(event_node->data, data, size);
	}
	spin_lock(&charger->list_lock);
	list_add_tail(&event_node->lnode, &charger->header);
	spin_unlock(&charger->list_lock);
	charger->thread_active = BUSINESS_CHARGER_THREAD_ACTIVE;
	wake_up_interruptible(&charger->wait_que);
}

static int business_charger_notifier_connect_cb(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct business_charger *charger = container_of(nb, struct business_charger, connect_nb);
	int size = 0;

	switch (event) {
	case MCA_EVENT_WIRELESS_REVCHG:
	case MCA_EVENT_USB_SUSPEND:
		size = sizeof(bool);
		break;
	default:
		break;
	}
#line 190
	mca_log_info("event %lu\n", event);
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_type_cb(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct business_charger *charger = container_of(nb, struct business_charger, type_change_nb);
	int size = 0;

	switch (event) {
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
	case MCA_EVENT_CHARGE_VERIFY_PROCESS_END:
		size = sizeof(int);
		break;
	case MCA_EVENT_SINK_PWR_SUSPEND_CHANGE:
		size = sizeof(bool);
		break;
	default:
		break;
	}

#line 214
	mca_log_info("event %lu\n", event);
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_hw_cb(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct business_charger *charger = container_of(nb, struct business_charger, hw_info_nb);
	int size = 0;

	switch (event) {
	case MCA_EVENT_BATT_BTB_CHANGE:
		size = sizeof(bool);
		break;
	case MCA_EVENT_LPD_STATUS_CHANGE:
	case MCA_EVENT_CC_SHORT_VBUS:
	case MCA_EVENT_VBAT_OVP_CHANGE:
	case MCA_EVENT_IBAT_OCP_CHANGE:
	case MCA_EVENT_QUICK_REVCHG_CHANGE:
	case MCA_EVENT_BOOST_STS:	/* 0x2f */
	case MCA_EVENT_SBU_LPD_CHANGE:	/* OS4: sbu_lpd status from mca_qcom_subpmic_proxy */
		size = sizeof(int);
		break;
	default:
		break;
	}
#line 242
	mca_log_info("event %lu\n", event);
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_chg_sts_cb(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct business_charger *charger = container_of(nb, struct business_charger, chg_sts_nb);
	int size = 0;

#line 254
	mca_log_info("event %lu\n", event);
	switch (event) {
	case MCA_EVENT_SOC_LIMIT:
	case MCA_EVENT_BATTERY_DTPT:
	case MCA_EVENT_CSD_SEND_PULSE:
	case MCA_EVENT_FG_OTA_PROCESS:
		size = sizeof(int);
		break;
	default:
		break;
	}
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_cp_info_cb(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct business_charger *charger = container_of(nb, struct business_charger, cp_info_nb);
	int size = 0;

#line 276
	mca_log_info("event %lu\n", event);
	switch (event) {
	case MCA_EVENT_CP_TSHUT_FLAG:
		size = sizeof(int) * 2;
		break;
	case MCA_EVENT_CP_CBOOT_FAIL:
		size = sizeof(int);
		break;
	default:
		break;
	}
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_debug_cb(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct business_charger *charger = container_of(nb, struct business_charger, debug_nb);
	int size = 0;

#line 298
	mca_log_info("debug event %lu\n", event);
	switch (event) {
	case MCA_EVENT_DEBUG_CTRL_DOUBLE85:
	case MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT:
	case MCA_EVENT_DEBUG_CTRL_MEMORY_TEST:
	case MCA_EVENT_DEBUG_CTRL_SOC_LIMIT:
		size = sizeof(int);
		break;
	default:
		break;
	}
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static void business_charger_wireless_report_status_work(struct work_struct *work)
{
	struct business_charger *charger = container_of(work,
		struct business_charger, delay_report_status_work.work);
	int len = 0, ret = 0;
	int usb_present = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data;
	union power_supply_propval prop;

#line 324
	mca_log_info("wireless delay report status work\n");

	prop.intval = charger->wls_active;
	ret = power_supply_set_property(charger->wls_psy_info->wireless_psy,
		POWER_SUPPLY_PROP_ONLINE, &prop);
	if (ret < 0)
#line 330
		mca_log_err("failed to set wls_online");
	business_charger_update_wireless_psy(charger->wls_psy_info);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_TX_ADAPTER=0");
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_WIRELESS_DISCONNECT, charger->wls_active);
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE, STRATEGY_STATUS_TYPE_ONLINE, &usb_present);
	if (usb_present)
		return;

	__pm_relax(charger->online_wake_lock);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_QUICK_CHARGE_TYPE=0");
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static void business_charger_wireless_delay_enable_rx_work(struct work_struct *work)
{
	int usb_present = 0;

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE, STRATEGY_STATUS_TYPE_ONLINE, &usb_present);
	if (!usb_present)
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, true);

#line 365
	mca_log_info("delay_enable_rx when usb out\n");
}

static void business_charger_wireless_reset_rx_work(struct work_struct *work)
{
	int usb_present = 0;
	int ret;

	ret = mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE, STRATEGY_STATUS_TYPE_ONLINE, &usb_present);
	if (ret) {
#line 377
		mca_log_err("get usb_present failed, ret=%d\n", ret);
		return;
	}
#line 380
	mca_log_info("get usb_present %d\n", usb_present);
	if (!usb_present) {
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, false);
		usleep_range(125000, 150000);
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, true);
	}
}

static void business_charger_process_pmic_init_done(struct business_charger *charger,
	unsigned int event)
{
#line 391
	mca_log_err("pmic init done\n");
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, 1);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, 1);
	if (charger->support_wls)
		schedule_delayed_work(&charger->reset_rx_work, msecs_to_jiffies(0));
}

static void business_charger_pmic_init_done_notify_work(struct work_struct *work)
{
	struct business_charger *charger = container_of(work,
		struct business_charger, pmic_init_done_notify_work.work);
	int buckchg_init_ok = 0;
	int wls_basic_init_ok = 0;
	int ret;

#line 404
	mca_log_info("pmic init done notify work\n");
	if (charger->support_wls)
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_INIT_OK, &wls_basic_init_ok);
	else
		wls_basic_init_ok = 1;
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
		STRATEGY_STATUS_TYPE_INIT_OK, &buckchg_init_ok);

	if (!wls_basic_init_ok || !buckchg_init_ok) {
#line 413
		mca_log_err("wls_basic or buckchg not init done, rerun work\n");
		schedule_delayed_work(&charger->pmic_init_done_notify_work, msecs_to_jiffies(1000));
		return;
	}

	ret = platform_class_buckchg_ops_is_init_ok(MAIN_BUCK_CHARGER);
	if (ret <= 0) {
#line 419
		mca_log_err("pmic not init done, return & wait event\n");
		return;
	}
#line 423
	mca_log_err("pmic init done, process event\n");
	business_charger_process_pmic_init_done(charger, MCA_EVENT_PMIC_INIT_DONE);
}

static void business_charger_process_cap_change(struct business_charger *charger,
	unsigned int event)
{
	if (charger->real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		cancel_delayed_work_sync(&charger->report_quick_charge_type_work);
		schedule_delayed_work(&charger->report_quick_charge_type_work, 0);
	} else {
		cancel_delayed_work_sync(&charger->report_quick_charge_type_work);
		schedule_delayed_work(&charger->report_quick_charge_type_work, msecs_to_jiffies(1000));
	}

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event, 0);
}

static void business_charger_process_snk_power_suspend(struct business_charger *charger,
	unsigned int event, void *data)
{
	bool bsinkpowersuspend = *((bool *)data);

#line 448
	mca_log_info("bsinkpowersuspend: %d\n", bsinkpowersuspend);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, bsinkpowersuspend);
}

static void business_charger_report_wired_quick_charge_type(struct business_charger *charger, int icon_type)
{
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data;
	int len;

	charger->wired_qucik_charge_type = icon_type;

	if (charger->abnormal_info.asuint32) {
		icon_type = ADP_ICON_TYPE_NORMAL;
#line 462
		mca_log_info("force normal icon 0x%x", charger->abnormal_info.asuint32);
	}

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_QUICK_CHARGE_TYPE=%d", icon_type);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static void business_charger_report_quick_charge_type_work(struct work_struct *work)
{
	struct business_charger *charger = container_of(work,
		struct business_charger, report_quick_charge_type_work.work);
	int icon_type;
	int power_max = 0;

	if (charger->real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS, (unsigned int *)(&power_max));
		if (charger->keep_report_max_power) {
			power_max = max(charger->wired_power_max, power_max);
			charger->wired_power_max = power_max;
		} else {
			charger->wired_power_max = power_max;
		}
		if (power_max >= QUICK_CHG_TYPE_SUPER_CHG_POWER)
			icon_type = ADP_ICON_TYPE_SUPER;
		else if (power_max >= QUICK_CHG_TYPE_TURBO_CHG_POWER)
			icon_type = ADP_ICON_TYPE_TURBO;
		else if (power_max >= QUICK_CHG_TYPE_FLASH_CHG_POWER)
			icon_type = ADP_ICON_TYPE_FLASH;
		else
			icon_type = ADP_ICON_TYPE_FAST;

#line 495
		mca_log_info("keep_report_max: %d, power_max: %d, icon_type: %d\n", charger->keep_report_max_power, power_max, icon_type);
		business_charger_report_wired_quick_charge_type(charger, icon_type);
	} else if (charger->real_type == XM_CHARGER_TYPE_PPS) {
		if (charger->verify_process_end && mca_strategy_is_support_pps_100w()) {
			protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS, (unsigned int *)(&power_max));
			charger->wired_power_max = max(charger->wired_power_max, power_max);
			icon_type = power_max > 29 ? ADP_ICON_TYPE_TURBO : ADP_ICON_TYPE_FAST;
#line 507
			mca_log_info("real_type: %d, power_max: %d, icon_type: %d\n", charger->real_type, power_max, icon_type);
			business_charger_report_wired_quick_charge_type(charger, icon_type);
		}
	}
}

static void business_charger_update_wired_quick_charge_type(struct business_charger *charger)
{
	union power_supply_propval pval = {0,};
	int icon_type = 0;
	int power_max = 0;

	if (!charger->bc12_active)
		return;

	if (charger->usb_psy_info != NULL && charger->usb_psy_info->batt_psy != NULL)
		power_supply_get_property(charger->usb_psy_info->batt_psy, POWER_SUPPLY_PROP_HEALTH, &pval);
	if (pval.intval == POWER_SUPPLY_HEALTH_COLD)
		charger->abnormal_info.batt_cold = 1;
	else if (pval.intval == POWER_SUPPLY_HEALTH_OVERHEAT)
		charger->abnormal_info.batt_overhot = 1;
	else {
		charger->abnormal_info.batt_cold = 0;
		charger->abnormal_info.batt_overhot = 0;
	}

	switch (charger->real_type) {
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
		icon_type = ADP_ICON_TYPE_FAST;
		break;
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		icon_type = ADP_ICON_TYPE_FLASH;
		break;
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS, (unsigned int *)(&power_max));
		charger->wired_power_max = power_max;
		icon_type = ADP_ICON_TYPE_FAST;
		break;
	default:
		break;
	}

	business_charger_report_wired_quick_charge_type(charger, icon_type);
}

/* OS4: event 0x2f (boost status) */
static void business_charger_process_boost_status(struct business_charger *charger,
	unsigned int event, void *data)
{
	int boost_sts;

	if (!data) {
#line 972
		mca_log_err("boost status data is NULL!\n");
		return;
	}
	boost_sts = *((int *)data);
#line 978
	mca_log_info("handle boost_sts changed: %#x\n", boost_sts);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, boost_sts);
}

/* OS4: event 0x53 (sbu_lpd, sent by mca_qcom_subpmic_proxy): switch the FSA4480 SBU path */
static void business_charger_process_sbu_lpd_status_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	int status = *((int *)data);
	int sbu_event;
	struct device_node *node = of_parse_phandle(charger->dev->of_node, "fsa4480-i2c-handle", 0);

	if (!node) {
#line 1098
		mca_log_err("no switch node found\n");
		return;
	}
#line 1102
	mca_log_info("handle sbu lpd changed: %#x\n", status);
	if (status == 1)
		sbu_event = 4;
	else if (status == 2)
		sbu_event = 5;
	else
		sbu_event = 3;
#line 1111
	mca_log_err("third sbu event is %d\n", sbu_event);
	fsa4480_switch_event(node, sbu_event);
}

static void business_charger_process_type_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	bool cid_status = false;
	ktime_t now = ktime_get_boottime();

	(void)update_usb_pd_type(charger);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, charger->real_type);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, charger->real_type);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event, charger->real_type);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event, charger->real_type);
	if (charger->real_type != XM_CHARGER_TYPE_PD_VERIFY)
		business_charger_update_wired_quick_charge_type(charger);

	if (!charger->real_type) {
		protocol_class_pd_get_cid_status(0, &cid_status);
		charger->enable_broken_charge_check = cid_status;
		if (cid_status)
			charger->disconnect_time = now;
	} else if (charger->enable_broken_charge_check) {
		if (ktime_sub(now, charger->disconnect_time) < NSEC_PER_SEC)
			mca_charge_mievent_report(CHARGE_DFX_CHARGER_BROKEN, NULL, 0);
	}
}

static void business_charger_process_wireless_online_change(struct business_charger *charger,
	unsigned int event)
{
	static int wls_active_flag;
	int ret = 0;
	union power_supply_propval prop = {0,};

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_ONLINE, &charger->wls_active);
#line 606
	mca_log_info("wireless online %d\n", charger->wls_active);
	if (wls_active_flag == charger->wls_active)
		return;

	wls_active_flag = charger->wls_active;
	if (!wls_active_flag) {
		charger->wls_adapter_type = 0;
		event = MCA_EVENT_WIRELESS_DISCONNECT;
#line 615
		mca_log_info("relax wake lock\n");
		mca_charge_mievent_set_state(MIEVENT_STATE_PLUG, 0);
	} else if (!charger->dam_test_flag) {
#line 618
		mca_log_info("stay wake lock\n");
		event = MCA_EVENT_WIRELESS_CONNECT;
		__pm_stay_awake(charger->online_wake_lock);
	}

	if (wls_active_flag) {
		prop.intval = charger->wls_active;
		ret = power_supply_set_property(charger->wls_psy_info->wireless_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		if (ret < 0)
#line 628
			mca_log_err("failed to set wls_online");
		cancel_delayed_work_sync(&charger->delay_report_status_work);
		business_charger_update_wireless_psy(charger->wls_psy_info);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, wls_active_flag);
	} else
		schedule_delayed_work(&charger->delay_report_status_work, msecs_to_jiffies(2000));

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_JEITA, event, wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event, wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_SMARTCHG, event, wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event, wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BMD, event, wls_active_flag);
}

static void business_charger_process_usb_sns_func(struct business_charger *charger,
	unsigned int event, void *data)
{
	int usb_present = *((int *)data);
	bool fw_update = false;

	mca_wireless_rev_get_fw_update(&fw_update);
#line 652
	mca_log_err("usb_resent: %d, fw_update: %d\n", usb_present, fw_update);

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, usb_present);

	if (usb_present) {
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, false);
	} else if (!fw_update) {
		schedule_delayed_work(&charger->delay_enable_rx_work, msecs_to_jiffies(1500));
	}
	mca_wireless_rev_set_usb_plugin(usb_present ? true : false);
}

static void business_charger_process_online_change(struct business_charger *charger,
	unsigned int event)
{
	static int active_flag;
	struct mca_event_notify_data n_data = { 0 };
	union power_supply_propval pval = {0,};

	power_supply_get_property(charger->wls_psy_info->wireless_psy,
		POWER_SUPPLY_PROP_ONLINE, &pval);

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_STATUS_TYPE_ONLINE, &charger->bc12_active);
#line 678
	mca_log_info("online %d\n", charger->bc12_active);

	active_flag = charger->bc12_active;
	if (!active_flag) {
		charger->pd_type = 0;
		charger->real_type = 0;
		charger->dam_test_flag = 0;
		charger->wired_qucik_charge_type = 0;
		charger->wired_power_max = 0;
		charger->verify_process_end = 0;
		event = MCA_EVENT_USB_DISCONNECT;
		__pm_relax(charger->online_wake_lock);
		mca_charge_mievent_set_state(MIEVENT_STATE_PLUG, 0);
#line 691
		mca_log_info("relax wake lock\n");
	} else if (!charger->dam_test_flag) {
#line 693
		mca_log_info("stay wake lock\n");
		event = MCA_EVENT_USB_CONNECT;
		__pm_stay_awake(charger->online_wake_lock);
	}
	(void)update_usb_pd_type(charger);
	business_charger_process_usb_sns_func(charger, event, &active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_JEITA, event, active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event, active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_SMARTCHG, event, active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event, active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BMD, event, active_flag);
	if (active_flag) {
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			MCA_EVENT_CHARGE_TYPE_CHANGE, charger->real_type);
	} else if (!pval.intval) {
		n_data.event = "POWER_SUPPLY_QUICK_CHARGE_TYPE=0";
		n_data.event_len = 32; /* length of POWER_SUPPLY_QUICK_CHARGE_TYPE=0 */
		mca_event_report_uevent(&n_data);
	}
}

static void business_charger_wireless_rerun_usb_sns_work(struct work_struct *work)
{
	struct business_charger *charger = container_of(work,
		struct business_charger, delay_rerun_usb_sns_work.work);
	int usb_present = 0;

#line 912
	mca_log_info("wireless rerun cp vusb work\n");
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE, STRATEGY_STATUS_TYPE_ONLINE, &usb_present);
	business_charger_process_usb_sns_func(charger,
		usb_present ? MCA_EVENT_USB_CONNECT : MCA_EVENT_USB_DISCONNECT, &usb_present);
}

static void business_charger_process_usb_suspend_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	bool usb_suspend = *((bool *)data);

	if (usb_suspend) {
		__pm_relax(charger->online_wake_lock);
#line 723
		mca_log_info("relax wake lock\n");
	} else {
		if (charger->bc12_active) {
			__pm_stay_awake(charger->online_wake_lock);
#line 727
			mca_log_info("stay wake lock\n");
		}
	}

	return;
}

static void business_charger_process_antiburn_change(struct business_charger *charger,
	unsigned int event)
{
	int antiburn = connector_antiburn_is_triggered();

#line 739
	mca_log_info("antiburn: %d\n", antiburn);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, antiburn);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, antiburn);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, antiburn);
}

static void business_charger_process_is_eu_model(struct business_charger *charger,
	int value)
{
#line 748
	mca_log_info("is_eu_model: %d\n", value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, MCA_EVENT_IS_EU_MODEL, value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, MCA_EVENT_IS_EU_MODEL, value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_IS_EU_MODEL, value);
}

static void business_charger_process_plate_shock(struct business_charger *charger,
	int value)
{
#line 757
	mca_log_info("plate_shock: %d\n", value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, MCA_EVENT_PLATE_SHOCK, value == 1 ? 1 : 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_PLATE_SHOCK, value == 1 ? 1 : 0);
}

static void business_charger_process_start_quick_revchg(struct business_charger *charger,
	int value)
{
#line 765
	mca_log_info("start_quick_revchg: %d\n", value);
	if (value) {
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_START_QUICK_REVCHG, value);
	} else {
#line 769
		mca_log_info("user cancel start_quick_revchg, do nothing\n");
	}
}

static void business_charger_process_set_revchg_bcl(struct business_charger *charger,
	int value)
{
#line 775
	mca_log_info("set_revchg_bcl: %d\n", value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_REVCHG_BCL, value);
}

/* stop_handle_charge / handle_state state machine written by the sysfs nodes of the same name */
static void business_charger_process_handle_logic(struct business_charger *charger)
{
	static int last_handle_state;
	static int last_stop_handle_charge;
	int allow_charge;
	int tmp;
	bool ok;
	struct mca_event_notify_data n_data = { 0 };

	if (last_stop_handle_charge && !charger->stop_handle_charge && charger->handle_state)
		tmp = 0;
	else
		tmp = (last_stop_handle_charge != charger->stop_handle_charge && (charger->stop_handle_charge)) ? 1 : -1;
	ok = (last_handle_state == charger->handle_state) | (!charger->handle_state) | (charger->stop_handle_charge != 0);
	allow_charge = ok ? ((!charger->handle_state && last_handle_state) ? 1 : tmp) : 0;

#line 797
	mca_log_info("last_allow_charge = %d, last_handle_state = %d, last_stop_handle_charge = %d, allow_charge = %d, handle_state = %d, stop_handle_charge = %d\n", charger->usb_psy_info->last_allow_charge, last_handle_state, last_stop_handle_charge, allow_charge, charger->handle_state, charger->stop_handle_charge);
	last_handle_state = charger->handle_state;
	last_stop_handle_charge = charger->stop_handle_charge;

	if (allow_charge == -1 || allow_charge == charger->usb_psy_info->last_allow_charge)
		return;

	charger->usb_psy_info->last_allow_charge = allow_charge;
	business_usb_psy_event_process(charger->usb_psy_info);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_HANDLE_ALLOW_CHARGE, allow_charge);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, MCA_EVENT_HANDLE_ALLOW_CHARGE, allow_charge);
	if (allow_charge) {
		if (charger->real_type != XM_CHARGER_TYPE_PD_VERIFY)
			business_charger_update_wired_quick_charge_type(charger);
		if (charger->bc12_active) {
#line 815
			mca_log_info("stay wake lock\n");
			__pm_stay_awake(charger->online_wake_lock);
			return;
		}
	} else {
		if (charger->real_type) {
			n_data.event = "POWER_SUPPLY_QUICK_CHARGE_TYPE=0";
			n_data.event_len = 32; /* length of POWER_SUPPLY_QUICK_CHARGE_TYPE=0 */
			mca_event_report_uevent(&n_data);
		}
	}
#line 818
	mca_log_info("relax wake lock\n");
	__pm_relax(charger->online_wake_lock);
}

static void business_charger_process_soc_limit_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	int smartchg_pmic = *((int *)data);

#line 829
	mca_log_info("smartchg_pmic: %d\n", smartchg_pmic);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, smartchg_pmic);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, smartchg_pmic);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, smartchg_pmic);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, smartchg_pmic);
}

static void business_charger_process_dtpt_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	int dtpt_status = *((int *)data);

#line 841
	mca_log_info("dtpt_status: %d\n", dtpt_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_SMARTCHG, event, dtpt_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_JEITA, event, dtpt_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, dtpt_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, dtpt_status);
}

static void business_charger_process_fg_ota(struct business_charger *charger,
	unsigned int event, void *data)
{
	int fg_ota_process = *((int *)data);

#line 853
	mca_log_info("fg_ota_process: %d\n", fg_ota_process);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, fg_ota_process);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, fg_ota_process);
}

static void business_charger_process_batt_health_change(struct business_charger *charger,
	unsigned int event)
{
	if (charger->real_type == XM_CHARGER_TYPE_PPS || charger->real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		cancel_delayed_work_sync(&charger->report_quick_charge_type_work);
		schedule_delayed_work(&charger->report_quick_charge_type_work, 0);
	} else {
		business_charger_update_wired_quick_charge_type(charger);
	}
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, 0);
}

static void business_charger_process_batt_btb_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	bool batt_missing = *((bool *)data);

#line 875
	mca_log_info("batt_missing: %d\n", batt_missing);
	charger->abnormal_info.batt_missing = (int)batt_missing;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, batt_missing);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, batt_missing);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, batt_missing);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, batt_missing);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, batt_missing);
}

static void business_charger_process_batt_auth_pass(struct business_charger *charger,
	unsigned int event)
{
#line 887
	mca_log_err("reviced batt_auth pass\n");
	charger->abnormal_info.batt_auth_failed = 0;
	business_charger_update_wired_quick_charge_type(charger);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, true);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, true);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, true);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, true);
}

static void business_charger_process_chg_sts_change(struct business_charger *charger,
	unsigned int event)
{
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, 0);
}

static void business_charger_process_lpd_status_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	int force_5v_charging = 0;
	int is_liquited = *((int *)data);
	int is_charginglimit = lpd_is_charging_limit();

	force_5v_charging = is_liquited & is_charginglimit;

#line 930
	mca_log_info("%s: occur liquited need drop charging power: %d\n", __func__, force_5v_charging);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, force_5v_charging);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, force_5v_charging);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, is_liquited);
	mca_charge_mievent_report(CHARGE_DFX_LPD_DETECTED, &is_liquited, 1);
}

static void business_charger_process_verify_process_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	charger->verify_process_end = *((int *)data);

#line 942
	mca_log_info("pd verify process done: %d\n", charger->verify_process_end);
	cancel_delayed_work_sync(&charger->report_quick_charge_type_work);
	schedule_delayed_work(&charger->report_quick_charge_type_work, msecs_to_jiffies(4000));
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, charger->verify_process_end);
}

static void business_charger_process_cp_cboot_short(struct business_charger *charger,
	unsigned int event, void *data)
{
	int cp_cboot_short = *((int *)data);

#line 953
	mca_log_info("occur cp cboot short: %d\n", cp_cboot_short);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, cp_cboot_short);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, cp_cboot_short);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, cp_cboot_short);
}

static void business_charger_process_quick_revchg(struct business_charger *charger,
	unsigned int event, void *data)
{
	int auth_pos = *((int *)data);

#line 964
	mca_log_info("handle quick revchg changed: %#x\n", auth_pos);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, auth_pos);
}

static void business_charger_process_rp_short_vbus_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	int cc_short_vbus = *((int *)data);

#line 987
	mca_log_info("occur cc short vbus: %d\n", cc_short_vbus);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, cc_short_vbus);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, cc_short_vbus);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, cc_short_vbus);
	if (cc_short_vbus)
		mca_charge_mievent_report(CHARGE_DFX_RP_SHORT_VBUS_DETECTED, NULL, 0);
}

static void business_charger_process_vbat_ovp_status_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	int vbat_ovp_status = *((int *)data);

#line 1000
	mca_log_info("occur vbat_ovp_status: %d\n", vbat_ovp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, vbat_ovp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, vbat_ovp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, vbat_ovp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, vbat_ovp_status);
}

static void business_charger_process_ibat_ocp_status_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	int ibat_ocp_status = *((int *)data);

#line 1012
	mca_log_info("occur ibat_ocp_status: %d\n", ibat_ocp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, ibat_ocp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, ibat_ocp_status);
}

static void business_charger_process_csd_pulse_change(struct business_charger *charger,
	unsigned int event, void *data)
{
	int csd_pulse = *((int *)data);

	if (charger->real_type == XM_CHARGER_TYPE_PD_VERIFY) {
#line 1023
		mca_log_info("receive csd pulse request: %d\n", csd_pulse);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, csd_pulse);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, csd_pulse);
	} else
#line 1027
		mca_log_info("not standard pd verify, ignore csd pulse event\n");

}

static void business_charger_mievent_report_cp_vbat_ovp(void)
{
	int event_data[2] = { 0 };
	int vcell, vpack;

	(void)strategy_class_fg_ops_get_voltage(&vcell);
	(void)platform_class_cp_get_battery_voltage(CP_ROLE_MASTER, &vpack);
	event_data[0] = vcell;
	event_data[1] = vpack;
	mca_charge_mievent_report(CHARGE_DFX_CP_VBAT_OVP, event_data, 2);
}

/* sysfs "debug_ctrl": name, number of parameters, last parameters written by the debug event handlers */
struct debug_ctrl_info {
	char name[20];
	int arg_num;
	int args[5];
};

enum debug_ctrl_idx {
	DEBUG_CTRL_DOUBLE85,
	DEBUG_CTRL_REMOVE_TEMP_LIMIT,
	DEBUG_CTRL_SOC_LIMIT,
	DEBUG_CTRL_MEMORY_TEST,
	DEBUG_CTRL_MAX,
};

static struct debug_ctrl_info g_debug_ctrl[DEBUG_CTRL_MAX] = {
	[DEBUG_CTRL_DOUBLE85] = { "double85", 1 },
	[DEBUG_CTRL_REMOVE_TEMP_LIMIT] = { "remove_temp_limit", 1 },
	[DEBUG_CTRL_SOC_LIMIT] = { "soc_limit", 2 },
	[DEBUG_CTRL_MEMORY_TEST] = { "memory_test", 1 },
};

static void business_charger_process_debug_ctrl_soc_limit(struct business_charger *charger,
	unsigned int event, void *data)
{
	int soc_limit = *((int *)data);
	int soc_high = soc_limit >> 8;
	int soc_low = soc_limit & 0xff;

	if (soc_high < 0 || soc_high > 100 || soc_low > 100) {
#line 1079
		mca_log_err("soc_limit value is invalid: %d\n", soc_limit);
		return;
	}

	if (soc_high == g_debug_ctrl[DEBUG_CTRL_SOC_LIMIT].args[0] &&
	    soc_low == g_debug_ctrl[DEBUG_CTRL_SOC_LIMIT].args[1])
		return;

	g_debug_ctrl[DEBUG_CTRL_SOC_LIMIT].args[0] = soc_high;
	g_debug_ctrl[DEBUG_CTRL_SOC_LIMIT].args[1] = soc_low;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, soc_limit);
}

/* end of part */

static void business_charger_process_debug_ctrl_double85(struct business_charger *charger,
	unsigned int event, void *data)
{
	int value = *((int *)data);

	if (value == g_debug_ctrl[DEBUG_CTRL_DOUBLE85].args[0])
		return;
	g_debug_ctrl[DEBUG_CTRL_DOUBLE85].args[0] = value;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event, value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event, value);
	platform_class_buckchg_ops_set_too_hot_limit(MAIN_BUCK_CHARGER, value == 0);
}

static void business_charger_process_debug_ctrl_remove_temp_limit(struct business_charger *charger,
	unsigned int event, void *data)
{
	int value = *((int *)data);

	if (value == g_debug_ctrl[DEBUG_CTRL_REMOVE_TEMP_LIMIT].args[0])
		return;
	g_debug_ctrl[DEBUG_CTRL_REMOVE_TEMP_LIMIT].args[0] = value;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event, value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event, value);
	platform_class_buckchg_ops_set_too_hot_limit(MAIN_BUCK_CHARGER, value == 0);
}

static void business_charger_process_debug_ctrl_memory_test(struct business_charger *charger,
	unsigned int event, void *data)
{
	int value = *((int *)data);

	if (value == g_debug_ctrl[DEBUG_CTRL_MEMORY_TEST].args[0])
		return;
	g_debug_ctrl[DEBUG_CTRL_MEMORY_TEST].args[0] = value;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event, value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event, value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, value);
	platform_class_buckchg_ops_set_too_hot_limit(MAIN_BUCK_CHARGER, value == 0);
}

static void business_charger_process_event(struct business_charger *charger)
{
	struct charger_event_lis_node *event_node, *temp_node;
	int data[2] = {0};

	while (!list_empty(&charger->header)) {
		spin_lock(&charger->list_lock);
		list_for_each_entry_safe(event_node, temp_node, &charger->header, lnode) {
			list_del(&event_node->lnode);
			spin_unlock(&charger->list_lock);

#line 1141
			mca_log_info("event_node->event: %d\n", event_node->event);
			switch (event_node->event) {
			case MCA_EVENT_USB_DISCONNECT:
			case MCA_EVENT_USB_CONNECT:
				business_charger_process_online_change(charger, event_node->event);
				break;
			case MCA_EVENT_WIRELESS_CONNECT:
			case MCA_EVENT_WIRELESS_DISCONNECT:
				business_charger_process_wireless_online_change(charger, event_node->event);
				break;
			case MCA_EVENT_CHARGE_ABNORMAL:
			case MCA_EVENT_CHARGE_RESTORE:
				business_charger_process_chg_sts_change(charger, event_node->event);
				break;
			case MCA_EVENT_WIRELESS_REVCHG:
				mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event_node->event,
					*((bool *)event_node->data));
				break;
			case MCA_EVENT_CHARGE_TYPE_CHANGE:
				business_charger_process_type_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_CHARGE_CAP_CHANGE:
				business_charger_process_cap_change(charger, event_node->event);
				break;
			case MCA_EVENT_CHARGE_VERIFY_PROCESS_END:
#line 1195
				mca_log_info("receive verify_process_end notify: %d\n", *((int *)event_node->data));
				business_charger_process_verify_process_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_BATTERY_HEALTH_CHANGE:
				business_charger_process_batt_health_change(charger, event_node->event);
				break;
			case MCA_EVENT_CP_VUSB_OVP:
				mca_charge_mievent_report(CHARGE_DFX_CP_VAC_OVP, NULL, 0);
				break;
			case MCA_EVENT_CP_VBAT_OVP:
				business_charger_mievent_report_cp_vbat_ovp();
				break;
			case MCA_EVENT_CP_VBUS_OVP:
				mca_charge_mievent_report(CHARGE_DFX_CP_VBUS_OVP, NULL, 0);
				break;
			case MCA_EVENT_CP_IBAT_OCP:
				mca_charge_mievent_report(CHARGE_DFX_CP_IBAT_OCP, NULL, 0);
				break;
			case MCA_EVENT_CP_IBUS_OCP:
				mca_charge_mievent_report(CHARGE_DFX_CP_IBUS_OCP, NULL, 0);
				break;
			case MCA_EVENT_CP_CBOOT_FAIL:
				business_charger_process_cp_cboot_short(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_CP_IIC_ERROR:
				mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event_node->event, 0);
				mca_charge_mievent_report(CHARGE_DFX_CP_ABSENT /* 0x39 */, data, 2);
				break;
			case MCA_EVENT_CP_TSHUT_FLAG:
				if (charger->cp_type == 1) {
					int *tdie = event_node->data;

					platform_class_cp_get_tdie(CP_ROLE_MASTER, &tdie[0]);
					platform_class_cp_get_tdie(CP_ROLE_SLAVE, &tdie[1]);
				}
				mca_charge_mievent_report(CHARGE_DFX_CP_TDIE_HOT, event_node->data, 2);
				break;
			case MCA_EVENT_CONN_ANTIBURN_CHANGE:
				business_charger_process_antiburn_change(charger, event_node->event);
				break;
			case MCA_EVENT_BATT_BTB_CHANGE:
				business_charger_process_batt_btb_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_BATT_AUTH_PASS:
				business_charger_process_batt_auth_pass(charger, event_node->event);
				break;
			case MCA_EVENT_LPD_STATUS_CHANGE:
				business_charger_process_lpd_status_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_PMIC_INIT_DONE:
				business_charger_process_pmic_init_done(charger, event_node->event);
				break;
			case MCA_EVENT_CC_SHORT_VBUS:
				business_charger_process_rp_short_vbus_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_VBAT_OVP_CHANGE:
				business_charger_process_vbat_ovp_status_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_IBAT_OCP_CHANGE:
				business_charger_process_ibat_ocp_status_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_QUICK_REVCHG_CHANGE:
				business_charger_process_quick_revchg(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_BOOST_STS:
				business_charger_process_boost_status(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_USB_STS_CHANGE:
				business_usb_psy_event_process(charger->usb_psy_info);
				break;
			case MCA_EVENT_SOC_LIMIT:
				business_charger_process_soc_limit_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_BATTERY_DTPT:
				business_charger_process_dtpt_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_CSD_SEND_PULSE:
				business_charger_process_csd_pulse_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_SINK_PWR_SUSPEND_CHANGE:
				business_charger_process_snk_power_suspend(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_USB_SUSPEND:
				business_charger_process_usb_suspend_change(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_DEBUG_CTRL_DOUBLE85:
				business_charger_process_debug_ctrl_double85(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT:
				business_charger_process_debug_ctrl_remove_temp_limit(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_DEBUG_CTRL_MEMORY_TEST:
				business_charger_process_debug_ctrl_memory_test(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_DEBUG_CTRL_SOC_LIMIT:
				business_charger_process_debug_ctrl_soc_limit(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_FG_OTA_PROCESS:
				business_charger_process_fg_ota(charger, event_node->event, event_node->data);
				break;
			case MCA_EVENT_SBU_LPD_CHANGE:
				business_charger_process_sbu_lpd_status_change(charger, event_node->event, event_node->data);
				break;
			}
			spin_lock(&charger->list_lock);
			kfree(event_node->data);
			kfree(event_node);
		}
		spin_unlock(&charger->list_lock);
	};
}

/* Standard usb_type definitions similar to power_supply_sysfs.c */
static const char * const power_supply_usb_type_text[] = {
	"Unknown", "SDP", "CDP", "DCP", "USB_FLOAT", "HVDCP",
	"HVDCP_3", "HVDCP_3_B", "HVDCP_3P5", "C", "PD", "PD_PPS",
	"PD_PPS", "Unknown", "ACA", "DCP",
};

static const char *get_usb_type_name(u32 usb_type)
{
	if (usb_type < ARRAY_SIZE(power_supply_usb_type_text))
		return power_supply_usb_type_text[usb_type];

	return "Unknown";
}

enum bussiness_charger_attr_list {
	BUSSINESS_CHARGER_PROP_REAL_TYPE,
	BUSSINESS_CHARGER_PROP_POWER_MAX,
	BUSSINESS_CHARGER_PROP_QUICK_CHARGE_TYPE,
	BUSSINESS_CHARGER_PROP_IS_EU_MODEL,
	BUSSINESS_CHARGER_PROP_PLATE_SHOCK,
	BUSSINESS_CHARGER_PROP_START_QUICK_REVCHG,
	BUSSINESS_CHARGER_PROP_REVCHG_BCL,
	BUSSINESS_CHARGER_PROP_HANDLE_STATE,
	BUSSINESS_CHARGER_PROP_STOP_HANDLE_CHARGE,
	BUSSINESS_CHARGER_PROP_DEBUG_CTRL,
};

static ssize_t bussiness_charger_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t bussiness_charger_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info bussiness_charger_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(bussiness_charger_sysfs, 0444, BUSSINESS_CHARGER_PROP_REAL_TYPE, real_type),
	mca_sysfs_attr_ro(bussiness_charger_sysfs, 0444, BUSSINESS_CHARGER_PROP_POWER_MAX, power_max),
	mca_sysfs_attr_ro(bussiness_charger_sysfs, 0444, BUSSINESS_CHARGER_PROP_QUICK_CHARGE_TYPE, quick_charge_type),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664, BUSSINESS_CHARGER_PROP_IS_EU_MODEL, is_eu_model),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664, BUSSINESS_CHARGER_PROP_PLATE_SHOCK, plate_shock),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664, BUSSINESS_CHARGER_PROP_START_QUICK_REVCHG, reverse_quick_charge),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664, BUSSINESS_CHARGER_PROP_REVCHG_BCL, revchg_bcl),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664, BUSSINESS_CHARGER_PROP_HANDLE_STATE, handle_state),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664, BUSSINESS_CHARGER_PROP_STOP_HANDLE_CHARGE, stop_handle_charge),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664, BUSSINESS_CHARGER_PROP_DEBUG_CTRL, debug_ctrl),
};

#define BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE   ARRAY_SIZE(bussiness_charger_sysfs_field_tbl)

static struct attribute *bussiness_charger_sysfs_attrs[BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group bussiness_charger_sysfs_attr_group = {
	.attrs = bussiness_charger_sysfs_attrs,
};

static int business_charger_get_power_max(struct business_charger *charger, int *pdata)
{
	union power_supply_propval pval = {0,};

	if (charger->bc12_active) {
		*pdata = charger->wired_power_max;
		return 0;
	}

	power_supply_get_property(charger->wls_psy_info->wireless_psy,
		POWER_SUPPLY_PROP_ONLINE, &pval);
	if (pval.intval)
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_POWER_MAX, pdata);
	return 0;
}

static int  business_charger_get_quick_charge_type(struct business_charger *charger, int *pdata)
{
	int online = 0;

	if (charger->bc12_active) {
		*pdata = charger->wired_qucik_charge_type;
		return 0;
	}

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
		STRATEGY_STATUS_TYPE_ONLINE, &online);
	if (online)
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_QC_TYPE, pdata);
	return 0;
}

static int business_charger_partition_set_status(int mca_charger_partition_type, int val)
{
	int ret = 0;
	charger_partition_info_2 info_2 = {.eu_mode = 0, .test = val, .reserved = 0};

	switch (mca_charger_partition_type) {
	case MCA_CHARGER_PARTITION_TEST:
		break;
	case MCA_CHARGER_PARTITION_POWEROFFMODE:
		break;
	case MCA_CHARGER_PARTITION_EU_MODE:
		ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
		if(ret < 0) {
#line 1382
			mca_log_err("failed to alloc\n");
			return -1;
		}

		info_2.eu_mode = val;
		info_2.test = 0x34567890;
		info_2.reserved = 0;
		ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, (void *)&info_2, sizeof(charger_partition_info_2));
		if(ret < 0) {
#line 1391
			mca_log_err("failed to write\n");
			ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
			if(ret < 0) {
#line 1394
				mca_log_err("failed to dealloc\n");
				return -1;
			}
			return -1;
		}
#line 1399
		mca_log_info("ret: %d, info_2.eu_mode: %u\n", ret, info_2.eu_mode);

		ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
		if(ret < 0) {
#line 1403
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		break;
	default:
		break;
	}

	return ret;
}

static void business_charger_process_debug_ctrl_input(const char *buf)
{
	char name[20] = {0};
	int value = 0, value2 = 0;
	int i, num;

#line 1424
	mca_log_err("debug_ctrl input: %s\n", buf);

	for (i = 0; i < DEBUG_CTRL_MAX; i++) {
		if (!strncmp(buf, g_debug_ctrl[i].name, strlen(g_debug_ctrl[i].name)))
			break;
	}
	if (i == DEBUG_CTRL_MAX) {
#line 1433
		mca_log_err("invalid name\n");
		return;
	}

	switch (i) {
	case DEBUG_CTRL_DOUBLE85:
		num = sscanf(buf, "%s %d", name, &value);
		if (num == 2) {
			if (value != g_debug_ctrl[i].args[0]) {
				mca_event_block_notify(MCA_EVENT_TYPE_DEBUG, MCA_EVENT_DEBUG_CTRL_DOUBLE85, &value);
				charger_partition_write_double85(value);
			}
		} else {
#line 1446
			mca_log_err("invalid input\n");
		}
		break;
	case DEBUG_CTRL_REMOVE_TEMP_LIMIT:
		num = sscanf(buf, "%s %d", name, &value);
		if (num == 2) {
			if (value != g_debug_ctrl[i].args[0]) {
				mca_event_block_notify(MCA_EVENT_TYPE_DEBUG, MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT, &value);
				charger_partition_write_remove_temp_limit(value);
			}
		} else {
#line 1456
			mca_log_err("invalid input\n");
		}
		break;
	case DEBUG_CTRL_MEMORY_TEST:
		num = sscanf(buf, "%s %d", name, &value);
		if (num == 2) {
			if (value != g_debug_ctrl[i].args[0]) {
				mca_event_block_notify(MCA_EVENT_TYPE_DEBUG, MCA_EVENT_DEBUG_CTRL_MEMORY_TEST, &value);
				charger_partition_write_memory_test(value);
			}
		} else {
#line 1466
			mca_log_err("invalid input\n");
		}
		break;
	case DEBUG_CTRL_SOC_LIMIT:
		num = sscanf(buf, "%s %d %d", name, &value, &value2);
		if (num == 3) {
			if (value < 0 || value > 100 || value2 < 0 || value2 > 100) {
#line 1472
				mca_log_err("invalid soc value: %d %d\n", value, value2);
			} else {
				if (value != g_debug_ctrl[i].args[0] || value2 != g_debug_ctrl[i].args[1]) {
					int soc_limit = (value << 8) | value2;

					mca_event_block_notify(MCA_EVENT_TYPE_DEBUG, MCA_EVENT_DEBUG_CTRL_SOC_LIMIT, &soc_limit);
					charger_partition_write_soc_limit(soc_limit);
				}
			}
		} else {
#line 1479
			mca_log_err("invalid input\n");
		}
		break;
	default:
		break;
	}
}

static ssize_t bussiness_charger_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info = NULL;
	struct business_charger *charger = (struct business_charger *)dev_get_drvdata(dev);
	int ret = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		bussiness_charger_sysfs_field_tbl, BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case BUSSINESS_CHARGER_PROP_IS_EU_MODEL:
		(void)sscanf(buf, "%d", &charger->is_eu_model);
#line 1520
		mca_log_info("is_eu_model set: %d\n", charger->is_eu_model);
		ret = business_charger_partition_set_status(MCA_CHARGER_PARTITION_EU_MODE,charger->is_eu_model);
		business_charger_process_is_eu_model(charger, charger->is_eu_model);
		break;
	case BUSSINESS_CHARGER_PROP_PLATE_SHOCK:
		(void)sscanf(buf, "%d", &charger->plate_shock);
		business_charger_process_plate_shock(charger, charger->plate_shock);
		break;
	case BUSSINESS_CHARGER_PROP_START_QUICK_REVCHG:
		(void)sscanf(buf, "%d", &charger->start_quick_revchg);
		business_charger_process_start_quick_revchg(charger, charger->start_quick_revchg);
		break;
	case BUSSINESS_CHARGER_PROP_REVCHG_BCL:
		(void)sscanf(buf, "%d", &charger->revchg_bcl);
#line 1534
		mca_log_info("revchg_bcl set: %d\n", charger->revchg_bcl);
		business_charger_process_set_revchg_bcl(charger, charger->revchg_bcl);
		break;
	case BUSSINESS_CHARGER_PROP_HANDLE_STATE:
		(void)sscanf(buf, "%d", &charger->handle_state);
		business_charger_process_handle_logic(charger);
		break;
	case BUSSINESS_CHARGER_PROP_STOP_HANDLE_CHARGE:
		(void)sscanf(buf, "%d", &charger->stop_handle_charge);
		business_charger_process_handle_logic(charger);
		break;
	case BUSSINESS_CHARGER_PROP_DEBUG_CTRL:
		business_charger_process_debug_ctrl_input(buf);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t bussiness_charger_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	struct business_charger *charger = (struct business_charger *)dev_get_drvdata(dev);
	ssize_t count = 0;
	int value = 0;
	int len = 0;
	int i, j;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		bussiness_charger_sysfs_field_tbl, BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case BUSSINESS_CHARGER_PROP_REAL_TYPE:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%s\n", get_usb_type_name(charger->real_type));
		break;
	case BUSSINESS_CHARGER_PROP_POWER_MAX:
		business_charger_get_power_max(charger, &value);
#line 1575
		mca_log_info("power_max: %d\n", value);
		count = snprintf(buf, PAGE_SIZE, "%d\n", value);
		break;
	case BUSSINESS_CHARGER_PROP_QUICK_CHARGE_TYPE:
		business_charger_get_quick_charge_type(charger, &value);
		count = snprintf(buf, PAGE_SIZE, "%d\n", value);
		break;
	case BUSSINESS_CHARGER_PROP_IS_EU_MODEL:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%d\n", charger->is_eu_model);
#line 1585
		mca_log_info("get eu_model: %d\n", buf);
		break;
	case BUSSINESS_CHARGER_PROP_PLATE_SHOCK:
		if (charger) {
			count = snprintf(buf, PAGE_SIZE, "%d\n", charger->plate_shock);
#line 1590
			mca_log_info("plate_shock: %d\n", charger->plate_shock);
		}
		break;
	case BUSSINESS_CHARGER_PROP_START_QUICK_REVCHG:
		if (charger) {
			count = snprintf(buf, PAGE_SIZE, "%d\n", charger->start_quick_revchg);
#line 1596
			mca_log_info("start_quick_revchg: %d\n", charger->start_quick_revchg);
		}
		break;
	case BUSSINESS_CHARGER_PROP_REVCHG_BCL:
		if (charger) {
			count = snprintf(buf, PAGE_SIZE, "%d\n", charger->revchg_bcl);
#line 1602
			mca_log_info("revchg_bcl: %d\n", charger->revchg_bcl);
		}
		break;
	case BUSSINESS_CHARGER_PROP_HANDLE_STATE:
		if (charger) {
			count = snprintf(buf, PAGE_SIZE, "%d\n", charger->handle_state);
#line 1608
			mca_log_info("handle_state: %d\n", charger->handle_state);
		}
		break;
	case BUSSINESS_CHARGER_PROP_STOP_HANDLE_CHARGE:
		if (charger) {
			count = snprintf(buf, PAGE_SIZE, "%d\n", charger->stop_handle_charge);
#line 1614
			mca_log_info("stop_handle_charge: %d\n", charger->stop_handle_charge);
		}
		break;
	case BUSSINESS_CHARGER_PROP_DEBUG_CTRL:
		if (charger) {
			for (i = 0; i < DEBUG_CTRL_MAX; i++) {
				len += sprintf(buf + len, "%s", g_debug_ctrl[i].name);
				for (j = 0; j < g_debug_ctrl[i].arg_num; j++)
					len += sprintf(buf + len, " %d", g_debug_ctrl[i].args[j]);
				len += sprintf(buf + len, "\n");
			}
			count = len;
		}
		break;
	default:
		break;
	}
	return count;
}

static int business_charger_sysfs_create_group(struct business_charger *charger)
{
	mca_sysfs_init_attrs(bussiness_charger_sysfs_attrs, bussiness_charger_sysfs_field_tbl,
		BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_1, "charger_common",
		charger->dev, &bussiness_charger_sysfs_attr_group);
}

static void business_charger_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group(SYSFS_DEV_1, "charge_common", dev,
		&bussiness_charger_sysfs_attr_group);
}

static int business_charger_event_thread(void *args)
{
	struct business_charger *charger = args;

	business_charger_process_online_change(charger, MCA_EVENT_USB_CONNECT);
	business_charger_process_wireless_online_change(charger, MCA_EVENT_WIRELESS_CONNECT);

	while (1) {
		wait_event_interruptible(charger->wait_que,
			(charger->thread_active == BUSINESS_CHARGER_THREAD_ACTIVE));
		charger->thread_active = 0;
		business_charger_process_event(charger);
	};
	return 0;
}

static ssize_t business_debugfs_show(void *priv_data, char *buf)
{
	struct mca_debugfs_attr_data *attr_data = (struct mca_debugfs_attr_data *)priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct business_charger *charger = (struct business_charger *)attr_data->private;
	size_t count = 0;

	if (!charger || !attr_info) {
#line 1666
		mca_log_err("null pointer store\n");
		return count;
	}

	switch (attr_info->debugfs_attr_name) {
	case BUSINESS_CHARGER_DEBUG_PROP_DAM_TEST:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", charger->dam_test_flag);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t business_debugfs_store(void *priv_data, const char *buf, size_t count)
{
	struct mca_debugfs_attr_data *attr_data = (struct mca_debugfs_attr_data *)priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct business_charger *charger = (struct business_charger *)attr_data->private;
	int val;

	if (kstrtoint(buf, 16, &val))
		return -EINVAL;

	switch (attr_info->debugfs_attr_name) {
	case BUSINESS_CHARGER_DEBUG_PROP_DAM_TEST:
		charger->dam_test_flag = val;
		if (charger->dam_test_flag) {
#line 1695
			mca_log_info("relax wake_lock\n");
			__pm_relax(charger->online_wake_lock);
		} else if (charger->bc12_active) {
			__pm_stay_awake(charger->online_wake_lock);
#line 1699
			mca_log_info("stay wake_lock\n");
		}
		break;
	default:
		break;
	}

	return count;
}

static struct mca_debugfs_attr_info g_business_charge_debugfs_field_tbl[] = {
	mca_debugfs_attr(business_debugfs, 0664, BUSINESS_CHARGER_DEBUG_PROP_DAM_TEST, dam_test),
};
#define BUSINESS_CHARGER_DEBUGFS_ATTRS_SIZE	ARRAY_SIZE(g_business_charge_debugfs_field_tbl)

static int business_charge_dump_log_head(void *data, char *buf, int size)
{
	return snprintf(buf, size, "Online ChargeType ChargeEnable ChargeMode Vbus   Ibus   BTB_Status Batt_Auth ");
}

static int business_charge_dump_log_context(void *data, char *buf, int size)
{
	struct business_charger *charger = (struct business_charger *)data;
	int online = 0;
	int enable = 0;
	int chg_type = 0;
	int ibus = 0;
	int vbus = 0;
	int mode = 0;
	int temp = 0;

	if (!data)
		return snprintf(buf, size, "%-7d%-11d0x%-11x%-11d%-7d%-7d%-11d%-10d",
			-1, -1, -1, -1, -1, -1, -1, -1);

	if (charger->bc12_active) {
		online = 1;
		chg_type = charger->real_type;
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			STRATEGY_STATUS_TYPE_MODE, &mode);
	}
	else if (charger->wls_active) {
		online = 2;
		chg_type = charger->wls_adapter_type;
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
			STRATEGY_STATUS_TYPE_MODE, &mode);
	}

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
		STRATEGY_STATUS_TYPE_ENABLE, &temp);
	enable |= temp;
	temp = 0;
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
		STRATEGY_STATUS_TYPE_ENABLE, &temp);
	enable |= temp << 4;
	temp = 0;
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
		STRATEGY_STATUS_TYPE_ENABLE, &temp);
	enable |= temp << 14;

	if (mode) {
		if (charger->bc12_active) {
			(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				STRATEGY_STATUS_TYPE_VBUS, &vbus);
			(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				STRATEGY_STATUS_TYPE_IBUS, &ibus);
		} else {
			(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
				STRATEGY_STATUS_TYPE_VBUS, &vbus);
			(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
				STRATEGY_STATUS_TYPE_IBUS, &ibus);
		}
	} else {
		(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
		(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER, &ibus);
		vbus /= 1000;
		ibus /= 1000;
	}

	return snprintf(buf, size, "%-7d%-11d0x%-11x%-11d%-7d%-7d%-11d%-10d",
		online, chg_type, enable, mode, vbus, ibus,
		charger->abnormal_info.batt_missing, !charger->abnormal_info.batt_auth_failed);
}

static struct mca_log_charge_log_ops g_business_charger_log_ops = {
	.dump_log_head = business_charge_dump_log_head,
	.dump_log_context = business_charge_dump_log_context,
};

static int business_charger_probe(struct platform_device *pdev)
{
	struct business_charger *charger;
	static int probe_cnt;
	int rc = 0;

#line 1794
	mca_log_err("probe begin cnt %d\n", ++probe_cnt);
	charger = devm_kzalloc(&pdev->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	charger->dev = &pdev->dev;
	platform_set_drvdata(pdev, charger);
	rc = business_charger_parse_dt(charger);
	if (rc < 0) {
#line 1803
		mca_log_err(" Couldn't parse device tree rc=%d\n", rc);
		return rc;
	}

	charger->keep_report_max_power = of_property_read_bool(pdev->dev.of_node, "keep-report-max-power");
	charger->usb_psy_info = business_charger_usb_psy_init(charger->dev);
	if (!charger->usb_psy_info) {
#line 1809
		mca_log_err("Couldn't init usb psy rc=%d\n", rc);
		return -EINVAL;
	}

	charger->wls_psy_info = business_charger_wireless_psy_init(charger->dev);
	if (!charger->wls_psy_info) {
#line 1815
		mca_log_err("Couldn't init wireless psy rc=%d\n", rc);
		return -EINVAL;
	}

	INIT_LIST_HEAD(&charger->header);
	spin_lock_init(&charger->list_lock);
	init_waitqueue_head(&charger->wait_que);
	charger->usb_psy_info->last_allow_charge = 1;
	charger->abnormal_info.asuint32 = 0;
	charger->abnormal_info.batt_auth_failed = 1;
	INIT_DELAYED_WORK(&charger->delay_report_status_work, business_charger_wireless_report_status_work);
	INIT_DELAYED_WORK(&charger->delay_enable_rx_work, business_charger_wireless_delay_enable_rx_work);
	INIT_DELAYED_WORK(&charger->reset_rx_work, business_charger_wireless_reset_rx_work);
	INIT_DELAYED_WORK(&charger->report_quick_charge_type_work, business_charger_report_quick_charge_type_work);
	INIT_DELAYED_WORK(&charger->delay_rerun_usb_sns_work, business_charger_wireless_rerun_usb_sns_work);
	INIT_DELAYED_WORK(&charger->pmic_init_done_notify_work, business_charger_pmic_init_done_notify_work);

	charger->connect_nb.notifier_call = business_charger_notifier_connect_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
		&charger->connect_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail2;
	}

	charger->type_change_nb.notifier_call = business_charger_notifier_type_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGE_TYPE,
		&charger->type_change_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail1;
	}

	charger->hw_info_nb.notifier_call = business_charger_notifier_hw_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_HW_INFO,
		&charger->hw_info_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail3;
	}

	charger->chg_sts_nb.notifier_call = business_charger_notifier_chg_sts_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGE_STATUS,
		&charger->chg_sts_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail4;
	}

	charger->cp_info_nb.notifier_call = business_charger_notifier_cp_info_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_CP_INFO,
		&charger->cp_info_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail5;
	}

	charger->debug_nb.notifier_call = business_charger_notifier_debug_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_DEBUG,
		&charger->debug_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail6;
	}

	schedule_delayed_work(&charger->delay_rerun_usb_sns_work, msecs_to_jiffies(10000));
	schedule_delayed_work(&charger->pmic_init_done_notify_work, msecs_to_jiffies(0));

	charger->online_wake_lock = wakeup_source_register(charger->dev, "charger_wakelock");
	if (!charger->online_wake_lock)
#line 1884
		mca_log_err("reg wakelock failed\n");
	kthread_run(business_charger_event_thread, charger, "charger_event_thread");
	if (business_charger_sysfs_create_group(charger))
#line 1887
		mca_log_err("create sysfs failed\n");
#ifdef CONFIG_DEBUG_FS
	mca_debugfs_create_group("business_charger", g_business_charge_debugfs_field_tbl,
		BUSINESS_CHARGER_DEBUGFS_ATTRS_SIZE, charger);
#endif /* CONFIG_DEBUG_FS */
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_BUSINESS_CHG,
		&g_business_charger_log_ops, charger);
#line 1894
	mca_log_err("probe success\n");
	return 0;

reg_notify_fail6:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_DEBUG,
		&charger->debug_nb);
reg_notify_fail5:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CP_INFO,
		&charger->cp_info_nb);
reg_notify_fail4:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_HW_INFO,
		&charger->hw_info_nb);
reg_notify_fail3:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGE_TYPE,
		&charger->connect_nb);
reg_notify_fail1:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
		&charger->connect_nb);
reg_notify_fail2:
	business_charger_usb_psy_deinit(charger->usb_psy_info);
#line 1914
	mca_log_info("%s !!\n",
		rc == -EPROBE_DEFER ? "Over probe cnt max" : "OK");

	return rc;
}

static int business_charger_remove(struct platform_device *pdev)
{
	business_charger_sysfs_remove_group(&pdev->dev);
	return 0;
}

static void business_charger_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,business_charger"},
	{},
};

static struct platform_driver business_charger_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "business_charger",
		.of_match_table = match_table,
	},
	.probe = business_charger_probe,
	.remove = business_charger_remove,
	.shutdown = business_charger_shutdown,
};

/* ---------------------------------------------------------------- mca_charger_usb_psy.c */
#undef MCA_LOG_TAG
#define MCA_LOG_TAG "charger_usb_psy"

static enum power_supply_property usb_psy_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TYPE,
};

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int rc = 0;
	int bus_vol, bus_curr = 0;
	struct usb_psy_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_STATUS_TYPE_ONLINE, &info->present);
		val->intval = info->present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (!info->input_suppend_voter)
			info->input_suppend_voter = mca_find_votable("input_suspend");
		if (info->input_suppend_voter && mca_get_client_vote(info->input_suppend_voter, "micharge")) {
			val->intval = 0;
			break;
		}
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_STATUS_TYPE_ONLINE, &info->online);
		if (info->last_allow_charge)
			val->intval = info->online;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX: {
		int pd_status = 3;
		int max_val = 5000;

		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE, 2, &pd_status);
		(void)mca_strategy_func_get_status(pd_status == 1, 0x1e, &max_val);
		val->intval = max_val * 1000;
		break;
	}
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &bus_vol);
		val->intval = bus_vol;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER, &bus_curr);
		val->intval = bus_curr;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX: {
		int pd_status = 3;
		int max_val = 500;

		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE, 2, &pd_status);
		(void)mca_strategy_func_get_status(pd_status == 1, 0x1f, &max_val);
		val->intval = max_val * 1000;
		break;
	}
	case POWER_SUPPLY_PROP_TYPE:
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		break;
	default:
#line 135
		mca_log_err("Get prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
#line 141
		mca_log_err("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int usb_psy_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *pval)
{
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		mca_strategy_func_set_config(STRATEGY_FUNC_TYPE_BUCK_CHARGE, STRATEGY_CONFIG_INPUT_CURRENT_LIMIT,  pval->intval);
		break;
	default:
#line 159
		mca_log_err("set prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;

	}

	if (rc < 0) {
#line 166
		mca_log_err("Couldn't set prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int usb_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static struct power_supply_desc _usb_psy_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = usb_psy_props,
	.num_properties = ARRAY_SIZE(usb_psy_props),
	.get_property = usb_psy_get_prop,
	.set_property = usb_psy_set_prop,
	.property_is_writeable	= usb_psy_prop_is_writeable,
};

void business_charger_update_usb_psy(int real_type, struct usb_psy_info *info)
{
	switch (real_type) {
	case XM_CHARGER_TYPE_SDP:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB;
		break;
	case XM_CHARGER_TYPE_CDP:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case XM_CHARGER_TYPE_FLOAT:
	case XM_CHARGER_TYPE_DCP:
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
	case XM_CHARGER_TYPE_OCP:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case XM_CHARGER_TYPE_TYPEC:
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB_PD;
		break;
	case XM_CHARGER_TYPE_ACA:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB_ACA;
		break;
	default:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	}

	power_supply_changed(info->usb_psy);
	if (!info->batt_psy)
		info->batt_psy = power_supply_get_by_name("battery");

	if (info->batt_psy)
		power_supply_changed(info->batt_psy);
}

void business_usb_psy_event_process(struct usb_psy_info *info)
{
	if (!info)
		return;

	power_supply_changed(info->usb_psy);
}

static int charger_usb_psy_init(struct usb_psy_info *info)
{
	struct power_supply_config usb_cfg = {};
	int rc = 0;

	usb_cfg.drv_data = info;
	usb_cfg.of_node = info->dev->of_node;
	info->usb_psy = devm_power_supply_register(
			info->dev, &_usb_psy_desc, &usb_cfg);
	if (IS_ERR(info->usb_psy)) {
#line 254
		mca_log_err("Couldn't register usb power supply\n");
		return PTR_ERR(info->usb_psy);
	}
	info->usb_psy_desc = &_usb_psy_desc;

	return rc;
}

struct usb_psy_info *business_charger_usb_psy_init(struct device *dev)
{
	struct usb_psy_info *info;
	int rc = 0;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	info->dev = dev;
	rc = charger_usb_psy_init(info);
	if (rc < 0) {
		pr_err("Couldn't init usb psy rc=%d\n", rc);
		return NULL;
	}

	info->batt_psy = power_supply_get_by_name("battery");
	if (!info->batt_psy)
#line 280
		mca_log_err("get batt psy fail\n");

#line 282
	mca_log_err("usb psy success\n");
	return info;
}

void business_charger_usb_psy_deinit(struct usb_psy_info *info)
{
	if (!info || !info->usb_psy)
		return;

	power_supply_unregister(info->usb_psy);
}

/* ---------------------------------------------------------------- mca_charger_wireless_psy.c */
#undef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_charger_wireless_psy"

static bool is_batt_available(struct wireless_psy_info *info)
{
	if (!info->batt_psy)
		info->batt_psy = power_supply_get_by_name("battery");

	if (!info->batt_psy)
		return false;

	return true;
}

static enum power_supply_property wireless_psy_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
};

static int wireless_psy_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int rc = 0;
	struct wireless_psy_info *info = power_supply_get_drvdata(psy);

	if (!is_batt_available(info)) {
#line 71
		mca_log_err("get batt psy fail\n");
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_ONLINE, &info->present);
		val->intval = info->present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		strategy_class_fg_ops_get_voltage(&info->vbat);
		val->intval = info->vbat;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		strategy_class_fg_ops_get_current(&info->ibat);
		val->intval = info->ibat;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		break;
	case POWER_SUPPLY_PROP_TYPE:
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		break;
	default:
#line 109
		mca_log_err("Get prop %d is not supported in wireless\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
#line 115
		mca_log_err("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int wireless_psy_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int rc = 0;
	struct wireless_psy_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		info->online = val->intval;
		break;
	default:
#line 134
		mca_log_err("Set prop %d is not supported in wireless psy\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
#line 140
		mca_log_err("Couldn't set prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int wireless_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return 1;
	default:
		break;
	}

	return 0;
}

static struct power_supply_desc _wireless_psy_desc = {
	.name = "wireless",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = wireless_psy_props,
	.num_properties = ARRAY_SIZE(wireless_psy_props),
	.get_property = wireless_psy_get_prop,
	.set_property = wireless_psy_set_prop,
	.property_is_writeable = wireless_psy_prop_is_writeable,
};

void business_charger_update_wireless_psy(struct wireless_psy_info *info)
{
	power_supply_changed(info->wireless_psy);
	if (!info->batt_psy)
		info->batt_psy = power_supply_get_by_name("battery");

	if (info->batt_psy)
		power_supply_changed(info->batt_psy);

}

static int charger_wireless_psy_init(struct wireless_psy_info *info)
{
	struct power_supply_config wireless_cfg = {};
	int rc = 0;

	wireless_cfg.drv_data = info;
	wireless_cfg.of_node = info->dev->of_node;
	info->wireless_psy = devm_power_supply_register(
			info->dev, &_wireless_psy_desc, &wireless_cfg);
	if (IS_ERR(info->wireless_psy)) {
#line 191
		mca_log_err("Couldn't register wireless power supply\n");
		return PTR_ERR(info->wireless_psy);
	}
	info->wireless_psy_desc = &_wireless_psy_desc;
#line 195
	mca_log_err("wls psy success\n");

	return rc;
}

struct wireless_psy_info *business_charger_wireless_psy_init(struct device *dev)
{
	struct wireless_psy_info *info;
	int rc = 0;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	info->dev = dev;
	rc = charger_wireless_psy_init(info);
	if (rc < 0) {
#line 212
		mca_log_err("Couldn't init wireless psy rc=%d\n", rc);
		return NULL;
	}

	info->batt_psy = power_supply_get_by_name("battery");
	if (!info->batt_psy)
#line 218
		mca_log_err("get batt psy fail\n");

	return info;
}

void business_charger_wireless_psy_deinit(struct wireless_psy_info *info)
{
	if (!info || !info->wireless_psy)
		return;

	power_supply_unregister(info->wireless_psy);
}

/* ---------------------------------------------------------------- module */
static int __init business_charger_init(void)
{
	return platform_driver_register(&business_charger_driver);
}
module_init(business_charger_init);

static void __exit business_charger_exit(void)
{
	platform_driver_unregister(&business_charger_driver);
}
module_exit(business_charger_exit);

MODULE_DESCRIPTION("business charger core");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_LICENSE("GPL v2");
