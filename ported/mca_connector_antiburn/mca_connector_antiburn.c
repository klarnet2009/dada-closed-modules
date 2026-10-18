// SPDX-License-Identifier: GPL-2.0
/*
 * mca_connector_antiburn.c - connector antiburn driver (Xiaomi 15 "dada", Qualcomm variant)
 *
 * G2i rewrite from the OS4 stock disassembly, XRING sources of the same code base used as the template.
 * SAFETY: charging module, community-only source, never flash it, never put it into a ROM build.
 * Status (G2l): every function follows the OS4 stock listing (monitor_workfunc with check_status v1/v2 and
 * get_temp_increase_rate, probe with parse_dt / override / gpio_init rewritten from the disassembly); 6 functions still
 * differ in bytes only (see STATUS.md). Never run on hardware.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/pm_wakeup.h>
#include <linux/ktime.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/common/mca_hwid.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/protocol/protocol_qc_class.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_workqueue.h>
#include <mca/strategy/strategy_class.h>
#include "inc/mca_connector_antiburn.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_connector_antiburn"
#endif

#define CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST 1000
#define CONNECTOR_ANTIBURN_WORK_INTERVAL_NORMAL 5000
#define CONNECTOR_ANTIBURN_TRIGGER_TEMP 65
#define CONNECTOR_ANTIBURN_RECHARGE_TEMP 55
#define CONNECTOR_ANTIBURN_TEMPERATURE_INCREASE_RATE 4
#define NTC_SCALE_TEMP 1000
#define COMBINED_SENSOR_BOARD_CONNECTOR_ANTIBURN_TRIGGER_TEMP 60
#define COMBINED_RATE_CONNECTOR_ANTIBURN_TRIGGER_TEMP 35
#define THERMAL_SENSOR_BOARD_TRIGGER_TEMP 50
#define VBUS_UV_THRESHOLD 4100000
#define VBUS_DEALY_TIME 50

/* OS4 event / mievent numbers (values taken from the stock immediates and the OS4 BTF enums) */
#define OS4_EVENT_BOOST_STS 47
#define OS4_EVENT_CID_STS 48
#define OS4_EVENT_THERMAL_BOARD_TEMP_CHANGE 62
#define OS4_EVENT_DEBUG_FIRST 76
#define OS4_EVENT_DEBUG_LAST 78

/* OS4 mievent ids (stock `mov w0,#0xa` / `#0x18` before mca_charge_mievent_report / set_state) */
#define OS4_DFX_ANTI_BURN_TRIGGERED 10
#define OS4_DFX_ANTIBURN_ERR 24
#define BOOST_SRC_EXTERNAL 2
#define OTG_EN_BOOST 0

static struct connector_antiburn *g_conn;
static bool flag_restore;

int connector_antiburn_is_triggered(void)
{
	/* stock has no NULL check on g_conn (it dereferences it unconditionally); kept like the stock on purpose */
	return g_conn->triggered;
}
EXPORT_SYMBOL(connector_antiburn_is_triggered);

static __always_inline void connector_temp_uevent(int temp)
{
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	int len = 0;
	struct mca_event_notify_data event_data;

#line 84
	mca_log_info("connector temp uevent notify\n");

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_CONNECTOR_TEMP=%d", temp);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static __always_inline void adapter_reset_vsafe0V_uevent(int is_reset_vsafe0V)
{
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	int len = 0;
	struct mca_event_notify_data event_data;

#line 99
	mca_log_info("adapter vbus drop 0V notify\n");

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_ADAPTER_RESET_VSAFE0V=%d", is_reset_vsafe0V);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static __always_inline void connector_ntc_alarm_uevent(int ntc_alarm)
{
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	int len = 0;
	struct mca_event_notify_data event_data;

#line 114
	mca_log_info("ntc alarm notify\n");

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_NTC_ALARM=%d", ntc_alarm);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

/* G2l: rebuilt from the OS4 stock listing (inlined into connector_antiburn_monitor_workfunc +0x58..+0x45c) */
static int connector_antiburn_get_temp_increase_rate(struct connector_antiburn *conn)
{
	static bool first_boot_flag;
	static ktime_t last_time;
	static int last_connector_temp[CONNECTOR_PROP_TEMP_MAX];
	ktime_t current_time;
	s64 time_gap;
	int temp_gap;
	int i;

	if (!first_boot_flag) {
		last_time = ktime_get();
		for (i = 0; i < CONNECTOR_PROP_TEMP_MAX; i++) {
			last_connector_temp[i] = conn->temperature[i];
			conn->temp_increase_rate[i] = 0;
		}
		first_boot_flag = true;
	}

	current_time = ktime_get();
	time_gap = ktime_to_ms(ktime_sub(current_time, last_time));
	if (time_gap <= 0) {
#line 145
		mca_log_err("timestamp is error, time_gap is %ld, current time is %ld, last time is %ld\n", time_gap, current_time, last_time);
		return -1;
	}

	for (i = 0; i < CONNECTOR_PROP_TEMP_MAX; i++) {
		temp_gap = conn->temperature[i] - last_connector_temp[i];
		conn->temp_increase_rate[i] = temp_gap * 1000 / time_gap;
		last_connector_temp[i] = conn->temperature[i];
		/* the first rate after an ADC error recovery is ignored once */
		if (conn->temp_increase_rate[i] > conn->max_temp_increase_rate && flag_restore) {
			conn->temp_increase_rate[i] = 0;
			flag_restore = false;
#line 158
			mca_log_info("temp_gap is %d, current temp is %d, last temp is %d, temp increase rate is %d, index is %d\n", temp_gap, conn->temperature[i], last_connector_temp[i], conn->temp_increase_rate[i], i);
		}
	}
	last_time = current_time;
	return 0;
}

#line 165
static int connector_antiburn_get_temperature(struct connector_antiburn *conn, int index)
{
	static bool adc_err_flag;
	int connector_temp = conn->support_elaboration_anti_strategy ? 25000 : 25;
	struct thermal_zone_device **tzd;
	int ret;

	if (conn->fake_connector_temp[index])
		return conn->fake_connector_temp[index];

	switch (index) {
	case CONNECTOR_PROP_TEMP_1:
		tzd = &conn->tzd_conn;
		break;
	case CONNECTOR_PROP_TEMP_2:
		tzd = &conn->tzd_conn2;
		break;
	default:
#line 189
		mca_log_err("invaid ntc\n");
		goto done;
	}
	ret = thermal_zone_get_temp(*tzd, &connector_temp);
	if (ret) {
#line 192
		mca_log_err("iio get temp error, index is %d, connector_temp is %d, ret is %d", index,
			connector_temp, ret);
		adc_err_flag = true;
		return conn->temperature[index];
	}
done:
	if (adc_err_flag == 1) {
		adc_err_flag = false;
		flag_restore = true;
	}
#line 203
	mca_log_info(" CONNECTOR_PROP_TEMP read %d, index is %d\n", connector_temp, index);
	if (!conn->support_elaboration_anti_strategy)
		connector_temp /= NTC_SCALE_TEMP;

	return connector_temp;
}

static noinline void connector_antiburn_ensure_vbus_sense5V(struct connector_antiburn *conn)
{
	int i;
	int bus_volt = 0;
	int pd_type = 0;
	int bc12_type = 0;
	unsigned int data = 0;
	int real_type;
	int bus_volt_uv;

	for (i = 0; i < 3; i++) {
		protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &pd_type);
		protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12, &bc12_type);
		real_type = pd_type ? pd_type : bc12_type;
#line 226
		mca_log_err("real_type is %d, bus_volt is %d", real_type, bus_volt);
		switch (real_type) {
		case XM_CHARGER_TYPE_HVDCP2:
		case XM_CHARGER_TYPE_HVDCP3:
		case XM_CHARGER_TYPE_HVDCP3_B:
		case XM_CHARGER_TYPE_HVDCP3P5:
			platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &bus_volt);
			bus_volt_uv = bus_volt;
			bus_volt /= 1000;
			if (bus_volt_uv < 6000 * 1000)
				return;
			msleep(200);
			break;
		case XM_CHARGER_TYPE_PD:
		case XM_CHARGER_TYPE_PPS:
		case XM_CHARGER_TYPE_PD_VERIFY:
			protocol_class_pd_request_vdm_cmd(TYPEC_PORT_0, USBPD_UVDM_RESET_VSAFE0V, &data, 0);
			msleep(200);
			platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &bus_volt);
			bus_volt_uv = bus_volt;
			bus_volt /= 1000;
			if (bus_volt_uv < 6000 * 1000) {
				if (bus_volt_uv < 3600 * 1000) {
					conn->is_reset_vsafe0V = 1;
					adapter_reset_vsafe0V_uevent(conn->is_reset_vsafe0V);
				}
				return;
			}
			break;
		default:
			return;
		}
	}
}

/* G2l: v1 strategy (support_elaboration_anti_strategy == 0), stock monitor +0x464..+0x6a8, +0xaf4..+0xedc */
static __always_inline void connector_antiburn_check_status(struct connector_antiburn *conn)
{
	int connector_temp;
	int bus_volt = 0;
	unsigned int data = 0;
	int real_type_pd = 0;
	int real_type_bc = 0;
	int temp_increase_rate;
	int real_type;
	int bus_volt_uv;

	/* rate of the hotter NTC (index 1 only when strictly hotter), stock `cset lt; add x9,x20,w10,uxtw #2; ldr [x9,#0x8c]` */
	connector_temp = max(conn->temperature[0], conn->temperature[1]);
	temp_increase_rate = conn->temp_increase_rate[conn->temperature[0] < conn->temperature[1]];
	connector_temp = max(connector_temp, 25);

	protocol_class_pd_get_otg_plugin_status(TYPEC_PORT_0, &conn->otg_plugin_status);
	platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &conn->usb_online);
	protocol_class_pd_get_cid_status(TYPEC_PORT_0, &conn->cid_status);
#line 302
	mca_log_info("connector_temp:%d triggered:%d, cid_status:%d, otg_plugin_status: %d, usb_online:%d\n", connector_temp, conn->triggered, conn->cid_status, conn->otg_plugin_status, conn->usb_online);

	if ((connector_temp >= conn->trigger_temp ||
	     (connector_temp >= conn->comb_sensorboard_con_trigger_temp &&
	      conn->thermal_board_temp <= conn->max_thermal_board_temp) ||
	     (connector_temp >= conn->comb_rate_conn_trigger_temp &&
	      temp_increase_rate >= conn->max_temp_increase_rate)) &&
	    !conn->disable_antiburn && !conn->triggered && (conn->cid_status || conn->usb_online)) {
#line 308
		mca_log_err("triggering antiburn conn->usb_online is %d, conn->otg_plugin_status is %d, conn->cid_status is %d\n", conn->usb_online, conn->otg_plugin_status, conn->cid_status);
#line 311
		mca_log_err("usb_online: %d, otg_plugin_status: %d, conn_therm: %d/%d, temp_increase_rate: %d, thermal_board_temp: %d\n", conn->usb_online, conn->otg_plugin_status, conn->temperature[0], conn->temperature[1], temp_increase_rate, conn->thermal_board_temp);
		conn->triggered = 1;
		conn->ntc_alarm = 1;
		connector_temp_uevent(connector_temp * 10);
		connector_ntc_alarm_uevent(conn->ntc_alarm);
		protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &real_type_pd);
		protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12, &real_type_bc);
		real_type = real_type_pd ? real_type_pd : real_type_bc;
#line 320
		mca_log_info("real_type:%d, real_type_pd:%d, real_type_bc:%d\n", real_type, real_type_pd, real_type_bc);
		if (real_type == XM_CHARGER_TYPE_PD || real_type == XM_CHARGER_TYPE_PPS ||
		    real_type == XM_CHARGER_TYPE_PD_VERIFY) {
			protocol_class_pd_request_vdm_cmd(TYPEC_PORT_0, USBPD_UVDM_RESET_VSAFE0V, &data, 0);
			msleep(50);
			mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_CONN_ANTIBURN_CHANGE, NULL);
			mca_charge_mievent_report(OS4_DFX_ANTI_BURN_TRIGGERED, &connector_temp, 1);
			msleep(200);
			connector_antiburn_ensure_vbus_sense5V(conn);
			platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &bus_volt);
			bus_volt_uv = bus_volt;
			bus_volt /= 1000;
			if (bus_volt_uv < 3600 * 1000) {
				conn->is_reset_vsafe0V = 1;
				adapter_reset_vsafe0V_uevent(conn->is_reset_vsafe0V);
			}
		} else {
			mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_CONN_ANTIBURN_CHANGE, NULL);
			mca_charge_mievent_report(OS4_DFX_ANTI_BURN_TRIGGERED, &connector_temp, 1);
			msleep(200);
			connector_antiburn_ensure_vbus_sense5V(conn);
		}
		if (conn->otg_plugin_status)
			platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER,
				(conn->en_src << 16) | (conn->otg_boost_src << 8));
		if (conn->support_hw_antiburn) {
			gpiod_direction_output_raw(gpio_to_desc(conn->mos_ctrl_gpio), 1);
#line 348
			mca_log_err("triggering hw anti burn");
			msleep(VBUS_DEALY_TIME);
			platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &bus_volt);
			if (bus_volt >= VBUS_UV_THRESHOLD)
				mca_charge_mievent_report(OS4_DFX_ANTIBURN_ERR, NULL, 0);
		}
	} else if (connector_temp < conn->recharge_temp && temp_increase_rate < conn->max_temp_increase_rate &&
		   conn->triggered && !conn->otg_plugin_status && !conn->cid_status) {
		conn->triggered = 0;
		conn->is_reset_vsafe0V = 0;
#line 357
		mca_log_err("recovery antiburn conn->usb_online is %d, conn->otg_plugin_status is %d, conn->cid_status is %d\n", conn->usb_online, conn->otg_plugin_status, conn->cid_status);
		connector_temp_uevent(connector_temp * 10);
		adapter_reset_vsafe0V_uevent(conn->is_reset_vsafe0V);
		if (conn->support_hw_antiburn) {
			gpiod_direction_output_raw(gpio_to_desc(conn->mos_ctrl_gpio), 0);
#line 362
			mca_log_err("close hw anti burn");
		}
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_CONN_ANTIBURN_CHANGE, NULL);
		mca_charge_mievent_set_state(MIEVENT_STATE_END, OS4_DFX_ANTI_BURN_TRIGGERED);
	}

	if (conn->ntc_alarm == 1 && !conn->cid_status) {
		conn->ntc_alarm = 0;
		connector_temp_uevent(connector_temp * 10);
		connector_ntc_alarm_uevent(conn->ntc_alarm);
	}
	if (conn->otg_plugin_status || conn->usb_online == 1)
		conn->monitor_interval = CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST;
	else
		conn->monitor_interval = CONNECTOR_ANTIBURN_WORK_INTERVAL_NORMAL;
}

/* G2l: v2 strategy (support_elaboration_anti_strategy != 0, milli-degree units), stock monitor +0xec..+0x388, +0x6ac..+0xae4 */
static __always_inline void connector_antiburn_check_status_v2(struct connector_antiburn *conn)
{
	int connector_temp = 25000;
	int bus_volt = 0;
	unsigned int data = 0;
	int real_type_pd = 0;
	int real_type_bc = 0;
	int temp_increase_rate = 0;
	int real_type;
	int bus_volt_uv;
	static int not_attched_count;
	int i;

	for (i = 0; i < CONNECTOR_PROP_TEMP_MAX; i++)
		connector_temp = max(connector_temp, conn->temperature[i]);

	protocol_class_pd_get_otg_plugin_status(TYPEC_PORT_0, &conn->otg_plugin_status);
	platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &conn->usb_online);
	protocol_class_pd_get_cid_status(TYPEC_PORT_0, &conn->cid_status);
#line 408
	mca_log_info("connector_temp:%d:%d, temp_increase_rate:%d:%d, triggered:%d, cid_status:%d, otg_plugin_status: %d, usb_online:%d\n", conn->temperature[0], conn->temperature[1], conn->temp_increase_rate[0], conn->temp_increase_rate[1], conn->triggered, conn->cid_status, conn->otg_plugin_status, conn->usb_online);

	if ((connector_temp >= 75000 ||
	     ((connector_temp >= conn->trigger_temp ||
	       (connector_temp >= conn->comb_sensorboard_con_trigger_temp &&
		conn->thermal_board_temp <= conn->max_thermal_board_temp) ||
	       (conn->temperature[0] >= conn->comb_rate_conn_trigger_temp &&
		conn->temp_increase_rate[0] >= conn->max_temp_increase_rate) ||
	       (conn->temperature[1] >= conn->comb_rate_conn_trigger_temp &&
		conn->temp_increase_rate[1] >= conn->max_temp_increase_rate)) &&
	      (conn->cid_status || conn->usb_online))) &&
	    !conn->disable_antiburn && !conn->triggered) {
#line 417
		mca_log_err("triggering antiburn conn->usb_online is %d, conn->otg_plugin_status is %d, conn->cid_status is %d\n", conn->usb_online, conn->otg_plugin_status, conn->cid_status);
#line 420
		mca_log_err("usb_online: %d, otg_plugin_status: %d, conn_therm: %d/%d, temp_increase_rate: %d, thermal_board_temp: %d\n", conn->usb_online, conn->otg_plugin_status, conn->temperature[0], conn->temperature[1], temp_increase_rate, conn->thermal_board_temp);
		conn->triggered = 1;
		conn->ntc_alarm = 1;
		connector_temp_uevent(connector_temp * 10);
		connector_ntc_alarm_uevent(conn->ntc_alarm);
		protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &real_type_pd);
		protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12, &real_type_bc);
		real_type = real_type_pd ? real_type_pd : real_type_bc;
#line 429
		mca_log_info("real_type:%d, real_type_pd:%d, real_type_bc:%d\n", real_type, real_type_pd, real_type_bc);
		if (real_type == XM_CHARGER_TYPE_PD || real_type == XM_CHARGER_TYPE_PPS ||
		    real_type == XM_CHARGER_TYPE_PD_VERIFY) {
			protocol_class_pd_request_vdm_cmd(TYPEC_PORT_0, USBPD_UVDM_RESET_VSAFE0V, &data, 0);
			msleep(50);
			mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_CONN_ANTIBURN_CHANGE, NULL);
			mca_charge_mievent_report(OS4_DFX_ANTI_BURN_TRIGGERED, &connector_temp, 1);
			msleep(200);
			connector_antiburn_ensure_vbus_sense5V(conn);
			platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &bus_volt);
			bus_volt_uv = bus_volt;
			bus_volt /= 1000;
			if (bus_volt_uv < 3600 * 1000) {
				conn->is_reset_vsafe0V = 1;
				adapter_reset_vsafe0V_uevent(conn->is_reset_vsafe0V);
			}
		} else {
			mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_CONN_ANTIBURN_CHANGE, NULL);
			mca_charge_mievent_report(OS4_DFX_ANTI_BURN_TRIGGERED, &connector_temp, 1);
			msleep(200);
			connector_antiburn_ensure_vbus_sense5V(conn);
		}
		if (conn->otg_plugin_status)
			platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER,
				(conn->en_src << 16) | (conn->otg_boost_src << 8));
		if (conn->support_hw_antiburn) {
			gpiod_direction_output_raw(gpio_to_desc(conn->mos_ctrl_gpio), 1);
#line 457
			mca_log_err("triggering hw anti burn");
			msleep(VBUS_DEALY_TIME);
			platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &bus_volt);
			if (bus_volt >= VBUS_UV_THRESHOLD)
				mca_charge_mievent_report(OS4_DFX_ANTIBURN_ERR, NULL, 0);
		}
	} else if (conn->temperature[0] < conn->recharge_temp &&
		   conn->temp_increase_rate[0] < conn->max_temp_increase_rate &&
		   conn->temperature[1] < conn->recharge_temp &&
		   conn->temp_increase_rate[1] < conn->max_temp_increase_rate &&
		   conn->triggered && !conn->otg_plugin_status && !conn->cid_status) {
		conn->triggered = 0;
		conn->is_reset_vsafe0V = 0;
#line 468
		mca_log_err("recovery antiburn conn->usb_online is %d, conn->otg_plugin_status is %d, conn->cid_status is %d\n", conn->usb_online, conn->otg_plugin_status, conn->cid_status);
		connector_temp_uevent(connector_temp * 10);
		adapter_reset_vsafe0V_uevent(conn->is_reset_vsafe0V);
		if (conn->support_hw_antiburn) {
			gpiod_direction_output_raw(gpio_to_desc(conn->mos_ctrl_gpio), 0);
#line 473
			mca_log_err("close hw anti burn");
		}
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_CONN_ANTIBURN_CHANGE, NULL);
		mca_charge_mievent_set_state(MIEVENT_STATE_END, OS4_DFX_ANTI_BURN_TRIGGERED);
	}

	if (conn->ntc_alarm == 1 && !conn->cid_status) {
		conn->ntc_alarm = 0;
		connector_temp_uevent(connector_temp * 10);
		connector_ntc_alarm_uevent(conn->ntc_alarm);
	}
	/* fast polling while attached or hot, slow polling after 5 quiet rounds */
	if (!conn->otg_plugin_status && conn->usb_online != 1 && !conn->cid_status &&
	    conn->temperature[0] < 55000 && conn->temperature[1] < 55000) {
		if (not_attched_count++ >= 5)
			conn->monitor_interval = CONNECTOR_ANTIBURN_WORK_INTERVAL_NORMAL;
		else
			conn->monitor_interval = CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST;
	} else {
		not_attched_count = 0;
		conn->monitor_interval = CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST;
	}
}

#line 495
static int connector_antiburn_connect_cb(struct notifier_block *nb, unsigned long event, void *data)
{
	struct connector_antiburn *conn = container_of(nb, struct connector_antiburn, connect_nb);

	if (!conn->support_elaboration_anti_strategy)
		return 0;

	if (conn->monitor_interval == CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST) {
#line 871
		mca_log_info("usb connect, monitor_work already quick, return...");
		return 0;
	}
	if (event == MCA_EVENT_USB_CONNECT) {
#line 876
		mca_log_info("usb connect, run monitor_work now");
		cancel_delayed_work_sync(&conn->monitor_work);
		mca_queue_delayed_work(&conn->monitor_work, msecs_to_jiffies(1000));
		conn->monitor_interval = CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST;
	}
	return 0;
}

static int connector_antiburn_hw_cb(struct notifier_block *nb, unsigned long event, void *data)
{
	struct connector_antiburn *conn = container_of(nb, struct connector_antiburn, hw_nb);
	int val;

	if (!conn->support_elaboration_anti_strategy)
		return 0;

	if (conn->monitor_interval == CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST) {
#line 895
		mca_log_info("cid/otg connect, monitor_work already quick, return...");
		return 0;
	}
	switch (event) {
	case OS4_EVENT_BOOST_STS:
		val = *(int *)data;
#line 902
		mca_log_info("boost_sts = %d\n", val);
		if (val != 1)
			return 0;
#line 904
		mca_log_info("otg connect, run monitor_work now");
		break;
	case OS4_EVENT_CID_STS:
		val = *(int *)data;
#line 912
		mca_log_info("cid_sts = %d\n", val);
		if (val != 1)
			return 0;
#line 914
		mca_log_info("cid_sts, run monitor_work now");
		break;
	default:
		return 0;
	}
	cancel_delayed_work_sync(&conn->monitor_work);
	mca_queue_delayed_work(&conn->monitor_work, msecs_to_jiffies(1000));
	conn->monitor_interval = CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST;
	return 0;
}

static int connector_antiburn_debug_notifier_cb(struct notifier_block *nb, unsigned long event, void *data)
{
	struct connector_antiburn *conn = container_of(nb, struct connector_antiburn, debug_nb);

	switch (event) {
	case 76:
	case 77:
	case 78:
		break;
	default:
		return NOTIFY_DONE;
	}

	conn->disable_antiburn = *(int *)data;
#line 853
	mca_log_info("debug[%d] disable_antiburn: %d\n", event, conn->disable_antiburn);
	return NOTIFY_DONE;
}

static void connector_antiburn_monitor_workfunc(struct work_struct *work)
{
	struct connector_antiburn *conn = container_of(work, struct connector_antiburn, monitor_work.work);
	int i;

	for (i = 0; i < CONNECTOR_PROP_TEMP_MAX; i++)
		conn->temperature[i] = connector_antiburn_get_temperature(conn, i);
	connector_antiburn_get_temp_increase_rate(conn);
	if (conn->support_elaboration_anti_strategy)
		connector_antiburn_check_status_v2(conn);
	else
		connector_antiburn_check_status(conn);

	mca_queue_delayed_work(&conn->monitor_work, msecs_to_jiffies(conn->monitor_interval));
}

/* G2l: stock probe +0xd0..+0x32c (log 580); the defaults depend on support_elaboration_anti_strategy (milli-degrees) */
static __always_inline void connector_antiburn_parse_dt(struct connector_antiburn *conn)
{
	struct device_node *np = conn->dev->of_node;
	int elab;

	mca_parse_dts_u32(np, "support_elaboration_anti_strategy", &conn->support_elaboration_anti_strategy, 0);
	elab = conn->support_elaboration_anti_strategy;
	mca_parse_dts_u32(np, "trigger_temp", &conn->trigger_temp, elab ? 65000 : CONNECTOR_ANTIBURN_TRIGGER_TEMP);
	mca_parse_dts_u32(np, "recharge_temp", &conn->recharge_temp, elab ? 55000 : CONNECTOR_ANTIBURN_RECHARGE_TEMP);
	mca_parse_dts_u32(np, "support_soft_antiburn", &conn->support_soft_antiburn, 1);
	mca_parse_dts_u32(np, "support_hw_antiburn", &conn->support_hw_antiburn, 1);
	mca_parse_dts_u32(np, "use_double_ntc", &conn->use_double_ntc, 0);
	mca_parse_dts_u32(np, "antiburn_otg_detect", &conn->otg_detect_en, 1);
	mca_parse_dts_u32(np, "monitor_interval", &conn->monitor_interval, CONNECTOR_ANTIBURN_WORK_INTERVAL_FAST);
	mca_parse_dts_u32(np, "max_temp_increase_rate", &conn->max_temp_increase_rate,
		elab ? 4000 : CONNECTOR_ANTIBURN_TEMPERATURE_INCREASE_RATE);
	mca_parse_dts_string(np, "thermal-zone-name", &conn->thermal_zone_name);
	mca_parse_dts_string(np, "thermal-zone-name2", &conn->thermal_zone_name2);
	mca_parse_dts_u32(np, "comb_sensorboard_con_trigger_temp", &conn->comb_sensorboard_con_trigger_temp,
		elab ? 60000 : COMBINED_SENSOR_BOARD_CONNECTOR_ANTIBURN_TRIGGER_TEMP);
	mca_parse_dts_u32(np, "comb_rate_conn_trigger_temp", &conn->comb_rate_conn_trigger_temp,
		elab ? 35000 : COMBINED_RATE_CONNECTOR_ANTIBURN_TRIGGER_TEMP);
	mca_parse_dts_u32(np, "max_thermal_board_temp", &conn->max_thermal_board_temp, THERMAL_SENSOR_BOARD_TRIGGER_TEMP);
	mca_parse_dts_u32(np, "otg_boost_src", &conn->otg_boost_src, BOOST_SRC_EXTERNAL);
	mca_parse_dts_u32(np, "en_src", &conn->en_src, OTG_EN_BOOST);
	conn->support_base_flip = of_property_read_bool(np, "support-base-flip");
	mca_parse_dts_u32(np, "support_elaboration_anti_strategy", &conn->support_elaboration_anti_strategy, 0);
#line 580
	mca_log_err("%d:%d:%d:%d:%d:%d", conn->support_elaboration_anti_strategy, conn->trigger_temp, conn->recharge_temp, conn->max_temp_increase_rate, conn->comb_sensorboard_con_trigger_temp, conn->comb_rate_conn_trigger_temp);
}

/* G2l: stock probe +0x344..+0x5a4: child node of hwid "yanan", current values are the defaults */
static __always_inline void connector_antiburn_parse_dt_override(struct connector_antiburn *conn)
{
	struct device_node *np = conn->dev->of_node;
	struct device_node *child = of_get_child_by_name(np, "connector_antiburn_override");

	if (!child) {
#line 590
		mca_log_info("child node 'connector_antiburn_override' not found\n");
		return;
	}
#line 594
	mca_log_info("applying connector_antiburn override from child node\n");
	conn->dev->of_node = child;
	mca_parse_dts_u32(child, "support_elaboration_anti_strategy", &conn->support_elaboration_anti_strategy,
		conn->support_elaboration_anti_strategy);
	mca_parse_dts_u32(child, "trigger_temp", &conn->trigger_temp, conn->trigger_temp);
	mca_parse_dts_u32(child, "recharge_temp", &conn->recharge_temp, conn->recharge_temp);
	mca_parse_dts_u32(child, "support_soft_antiburn", &conn->support_soft_antiburn, conn->support_soft_antiburn);
	mca_parse_dts_u32(child, "support_hw_antiburn", &conn->support_hw_antiburn, conn->support_hw_antiburn);
	mca_parse_dts_u32(child, "use_double_ntc", &conn->use_double_ntc, conn->use_double_ntc);
	mca_parse_dts_u32(child, "antiburn_otg_detect", &conn->otg_detect_en, conn->otg_detect_en);
	mca_parse_dts_u32(child, "monitor_interval", &conn->monitor_interval, conn->monitor_interval);
	mca_parse_dts_u32(child, "max_temp_increase_rate", &conn->max_temp_increase_rate, conn->max_temp_increase_rate);
	mca_parse_dts_string(child, "thermal-zone-name", &conn->thermal_zone_name);
	mca_parse_dts_string(child, "thermal-zone-name2", &conn->thermal_zone_name2);
	mca_parse_dts_u32(child, "comb_sensorboard_con_trigger_temp", &conn->comb_sensorboard_con_trigger_temp,
		conn->comb_sensorboard_con_trigger_temp);
	mca_parse_dts_u32(child, "comb_rate_conn_trigger_temp", &conn->comb_rate_conn_trigger_temp,
		conn->comb_rate_conn_trigger_temp);
	mca_parse_dts_u32(child, "max_thermal_board_temp", &conn->max_thermal_board_temp, conn->max_thermal_board_temp);
	mca_parse_dts_u32(child, "otg_boost_src", &conn->otg_boost_src, conn->otg_boost_src);
	mca_parse_dts_u32(child, "en_src", &conn->en_src, conn->en_src);
	conn->support_base_flip = of_property_read_bool(child, "support-base-flip");
	conn->dev->of_node = np;
#line 632
	mca_log_err("%d:%d:%d:%d:%d:%d", conn->support_elaboration_anti_strategy, conn->trigger_temp, conn->recharge_temp, conn->max_temp_increase_rate, conn->comb_sensorboard_con_trigger_temp, conn->comb_rate_conn_trigger_temp);
}

#line 894
static ssize_t antiburn_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t antiburn_sysfs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info antiburn_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_PROP_TEMP_1, connector_temp_1),
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_PROP_TEMP_2, connector_temp_2),
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_PROP_RESET_VSAFE0V, reset_vsafe0V),
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_PROP_NTC_ALARM, ntc_alarm),
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_PROP_MOS_CTRL, mos_ctrl),
};

#define ANTIBURN_SYSFS_ATTRS_SIZE ARRAY_SIZE(antiburn_sysfs_field_tbl)

static struct attribute *antiburn_sysfs_attrs[ANTIBURN_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group antiburn_sysfs_attr_group = {
	.attrs = antiburn_sysfs_attrs,
};

static ssize_t antiburn_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	struct connector_antiburn *conn = dev_get_drvdata(dev);
	int temp;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name, antiburn_sysfs_field_tbl, ANTIBURN_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;
	if (!conn) {
#line 704
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}
	switch (attr_info->sysfs_attr_name) {
	case CONNECTOR_PROP_TEMP_1:
	case CONNECTOR_PROP_TEMP_2:
		temp = connector_antiburn_get_temperature(conn, attr_info->sysfs_attr_name);
		if (!conn->support_elaboration_anti_strategy)
			temp *= 10;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case CONNECTOR_PROP_RESET_VSAFE0V:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", conn->is_reset_vsafe0V);
		break;
	case CONNECTOR_PROP_NTC_ALARM:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", conn->ntc_alarm);
		break;
	case CONNECTOR_PROP_MOS_CTRL:
		if (!conn->support_hw_antiburn) {
#line 731
			mca_log_err("not support_hw_antiburn\n");
			return 0;
		}
		temp = gpiod_get_raw_value(gpio_to_desc(conn->mos_ctrl_gpio));
#line 727
		mca_log_err("show mos_ctrl:%d\n", temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	default:
		break;
	}
	return count;
}

static ssize_t antiburn_sysfs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct connector_antiburn *conn = dev_get_drvdata(dev);
	int val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name, antiburn_sysfs_field_tbl, ANTIBURN_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;
	if (!conn) {
#line 753
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}
	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case CONNECTOR_PROP_TEMP_1:
	case CONNECTOR_PROP_TEMP_2:
		conn->fake_connector_temp[attr_info->sysfs_attr_name] = conn->support_elaboration_anti_strategy ? val : val / 10;
#line 767
		mca_log_err("set the %d ntc = %d\n", attr_info->sysfs_attr_name, val);
		cancel_delayed_work_sync(&conn->monitor_work);
		mca_queue_delayed_work(&conn->monitor_work, msecs_to_jiffies(1000));
		break;
	case CONNECTOR_PROP_MOS_CTRL:
		if (conn->support_hw_antiburn) {
			if ((unsigned int)val <= 1)
				gpiod_direction_output_raw(gpio_to_desc(conn->mos_ctrl_gpio), val);
#line 779
			mca_log_err("set mos_ctrl:%d\n", val);	/* stock logs for val > 1 too (GPIO untouched) */
		} else {
#line 781
			mca_log_err("not support_hw_antiburn\n");
		}
		break;
	default:
		break;
	}
	return count;
}

static int antiburn_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(antiburn_sysfs_attrs, antiburn_sysfs_field_tbl, ANTIBURN_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_5, "connector", dev, &antiburn_sysfs_attr_group);
}

static int connector_antiburn_thermal_notifier_event(struct notifier_block *nb, unsigned long chg_event, void *val)
{
	struct connector_antiburn *conn = container_of(nb, struct connector_antiburn, thermal_board_nb);

	switch (chg_event) {
	case OS4_EVENT_THERMAL_BOARD_TEMP_CHANGE:
		conn->thermal_board_temp = *(int *)val / NTC_SCALE_TEMP;
#line 807
		mca_log_info("get thermal_board_temp: %d\n", conn->thermal_board_temp);
		break;
	default:
#line 810
		mca_log_info("not supported thermal board notifier event: %d\n", chg_event);
		break;
	}
	return NOTIFY_DONE;
}

static int connector_antiburn_dump_log_head(void *data, char *buf, int size)
{
	return snprintf(buf, size, "port_temp port_temp1 shell_temp ");
}

static int connector_antiburn_dump_log_context(void *data, char *buf, int size)
{
	struct connector_antiburn *conn = (struct connector_antiburn *)data;
	int temp1;
	int temp2 = -1;

	if (!data)
		return snprintf(buf, size, "%-10d%-11d%-11d", -1, -1, -1);

	temp1 = connector_antiburn_get_temperature(conn, CONNECTOR_PROP_TEMP_1);
	if (conn->use_double_ntc)
		temp2 = connector_antiburn_get_temperature(conn, CONNECTOR_PROP_TEMP_2);

	return snprintf(buf, size, "%-10d%-11d%-11d", temp1, temp2, conn->thermal_board_temp);
}

static struct mca_log_charge_log_ops g_connector_antiburn_log_ops = {
	.dump_log_head = connector_antiburn_dump_log_head,
	.dump_log_context = connector_antiburn_dump_log_context,
};

/* G2l: stock probe +0x608..+0x6d4 (inlined): a failed gpio_request keeps going like the stock (no early -EINVAL) */
static __always_inline int connector_antiburn_mos_ctrl_gpio_init(struct connector_antiburn *conn)
{
	int ret;

	conn->mos_ctrl_gpio = of_get_named_gpio(conn->dev->of_node, "mos-ctrl-gpio", 0);
	if (conn->mos_ctrl_gpio < 0) {
#line 641
		mca_log_err("failed to parse mos ctrl gpio\n");
	}
	ret = gpio_request(conn->mos_ctrl_gpio, "mos-ctrl-gpio");
	if (ret) {
#line 645
		mca_log_err("unable to request antiburn mos ctrl gpio ret is %d\n", ret);
		return ret;
	}
	ret = gpiod_direction_output_raw(gpio_to_desc(conn->mos_ctrl_gpio), 0);
	if (ret) {
#line 649
		mca_log_err("unable to set direction for pmic gpio\n");
		return ret;
	}
	return 0;
}

static __always_inline int connector_antiburn_gpio_init(struct connector_antiburn *conn)
{
#line 659
	mca_log_info("Hw antiburn init gpio\n");
	if (conn->support_hw_antiburn)
		return connector_antiburn_mos_ctrl_gpio_init(conn);
#line 663
	mca_log_info("No gpio config\n");
	return -1;
}

static int connector_antiburn_probe(struct platform_device *pdev)
{
	struct connector_antiburn *conn;
	static int probe_cnt;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	int ret;

#line 934
	mca_log_info("probe_cnt = %d\n", ++probe_cnt);
	if (!hwid)
		return -ENOMEM;
	if (hwid->platform_version == 1 && hwid->build_version == 0 && hwid->minor_version == 1) {
#line 941
		mca_log_err("Do not support antiburn in %s P%d.%d\n", hwid->product_name, hwid->build_version, hwid->minor_version);
		return 0;
	}

	conn = devm_kzalloc(&pdev->dev, sizeof(*conn), GFP_KERNEL);
	if (!conn) {
#line 946
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}
	conn->dev = &pdev->dev;
	platform_set_drvdata(pdev, conn);

	connector_antiburn_parse_dt(conn);
	/* OS4: no product_name NULL check in the stock (+0x330 ldr x0,[x21,#0x20]; bl strcmp) */
	if (!strcmp(hwid->product_name, "yanan"))
		connector_antiburn_parse_dt_override(conn);
	if (hwid->platform_version == 1 &&
	    (hwid->build_version == 0 || (hwid->build_version == 1 && hwid->minor_version == 0))) {
		conn->otg_boost_src = BOOST_SRC_EXTERNAL;
#line 960
		mca_log_err("start to use external boost in O2\n");
	}

	ret = connector_antiburn_gpio_init(conn);
	if (ret) {
#line 965
		mca_log_err("connector_antiburn_gpio_init failed, err is %d\n", ret);
		goto err;
	}
	conn->tzd_conn = thermal_zone_get_zone_by_name(conn->thermal_zone_name);
	if (IS_ERR(conn->tzd_conn)) {
#line 971
		mca_log_err("thermal zone get conn_therm failed\n");
		goto err;
	}
	if (conn->use_double_ntc) {
		conn->tzd_conn2 = thermal_zone_get_zone_by_name(conn->thermal_zone_name2);
		if (IS_ERR(conn->tzd_conn2)) {
#line 978
			mca_log_err("thermal zone get conn_therm2 failed\n");
			goto err;
		}
	}

	conn->thermal_board_nb.notifier_call = connector_antiburn_thermal_notifier_event;
	mca_event_block_notify_register(MCA_EVENT_TYPE_THERMAL_TEMP, &conn->thermal_board_nb);
	conn->debug_nb.notifier_call = connector_antiburn_debug_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_DEBUG, &conn->debug_nb);
	conn->connect_nb.notifier_call = connector_antiburn_connect_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT, &conn->connect_nb);
	conn->hw_nb.notifier_call = connector_antiburn_hw_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_HW_INFO, &conn->hw_nb);
	INIT_DELAYED_WORK(&conn->monitor_work, connector_antiburn_monitor_workfunc);
	mca_queue_delayed_work(&conn->monitor_work, msecs_to_jiffies(conn->monitor_interval));

	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_USCP, &g_connector_antiburn_log_ops, conn);
	antiburn_sysfs_create_group(conn->dev);
	conn->triggered = 0;
	g_conn = conn;
	conn->disable_antiburn = 0;
#line 1011
	mca_log_err("probe OK");
	return 0;
err:
	gpio_free(conn->mos_ctrl_gpio);
	devm_kfree(&pdev->dev, conn);
	return -EPROBE_DEFER;
}

#line 1018
static const struct of_device_id match_table[] = {
	{.compatible = "xiaomi,connector_antiburn"},
	{},
};

static int connector_antiburn_remove(struct platform_device *pdev)
{
	struct connector_antiburn *conn = platform_get_drvdata(pdev);

	cancel_delayed_work(&conn->monitor_work);
	if (conn->mos_ctrl_gpio >= 0) {
		gpio_free(conn->mos_ctrl_gpio);
#line 1032
		mca_log_info("remove mos ctrl gpio success");
	}
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_THERMAL_TEMP, &conn->thermal_board_nb);
	devm_kfree(&pdev->dev, conn);
	return 0;
}

static void connector_antiburn_shutdown(struct platform_device *pdev)
{
}

static struct platform_driver connector_antiburn_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "connector_antiburn",
		.of_match_table = match_table,
	},
	.probe = connector_antiburn_probe,
	.remove = connector_antiburn_remove,
	.shutdown = connector_antiburn_shutdown,
};

static int __init connector_antiburn_init(void)
{
	return platform_driver_register(&connector_antiburn_driver);
}
module_init(connector_antiburn_init);

static void __exit connector_antiburn_exit(void)
{
	platform_driver_unregister(&connector_antiburn_driver);
}
module_exit(connector_antiburn_exit);

MODULE_DESCRIPTION("Connector antiburn");
MODULE_AUTHOR("muxinyi1@xiaomi.com");
MODULE_LICENSE("GPL v2");
