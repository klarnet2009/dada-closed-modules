// SPDX-License-Identifier: GPL-2.0
/*
 * mca_pwm_fan.c - Xiaomi MCA "PWM FAN" accessory driver (Xiaomi 15 "dada", HyperOS 4.0 / OS4 module).
 * Reconstructed from the OS4 stock mca_pwm_fan.ko (no public source exists): the structures come from its BTF
 * (START_ID=141904 btf_dump.py local --all), the code from the disassembly.  Log tag [xm_fan].
 * SAFETY: part of the Xiaomi MCA charging/accessory stack - community-only source, never flash it.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pwm.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>

#define MCA_LOG_TAG "xm_fan"

#define FAN_EVENT_BUF_SIZE 500

enum pwm_fan_sysfs_prop {
	FAN_PROP_FAN_SUPPORT = 0,
	FAN_PROP_PWM_DUTY,
	FAN_PROP_TARGET_LEVEL,
	FAN_PROP_REAL_SPEED,
	FAN_PROP_MAX,
};

enum pwm_fan_speed_level {
	FAN_SPEED_LEVEL_OFF = 0,
	FAN_SPEED_LEVEL_LOW,
	FAN_SPEED_LEVEL_MED,
	FAN_SPEED_LEVEL_HIGH,
	FAN_SPEED_LEVEL_TURBO,
	FAN_SPEED_LEVEL_MAX,
	FAN_SPEED_LEVEL_DBG_OFF = 100,
	FAN_SPEED_LEVEL_DBG_LOW,
	FAN_SPEED_LEVEL_DBG_MED,
	FAN_SPEED_LEVEL_DBG_HIGH,
};

struct pwm_fan_speed_adjust_cfg {
	unsigned int min_speed;
	unsigned int max_speed;
	unsigned int speed_gap_thres;
	unsigned int adjust_cnt_max;
	unsigned int duty_min;
	unsigned int duty_max;
};

struct pwm_fan_duty_speed_map {
	unsigned int duty;
	unsigned int speed;
};

#define FAN_DUTY_MAP_NUM 5

struct pwm_fan_ctx {
	char name[24];
	struct device *dev;
	struct mutex lock;
	struct regulator *ldo;
	struct notifier_block ldo_nb;
	struct pwm_device *pwm_dev;
	struct pwm_state fan_pwm_state;
	bool fan_support;
	bool fan_enabled;
	int gpio_shifter_en;
	unsigned int pwm_duty;
	unsigned int pwm_ch;
	u32 pwm_freq;
	u32 ldo_type;
	u32 ldo_voltage_uv;
	unsigned int target_level;
	unsigned int target_level_dbg;
	unsigned int target_speed;
	unsigned int real_speed;
	unsigned int speed_adjust_cnt;
	unsigned int err_stat_report_cnt;
	unsigned int unsupport_id[10];
	struct pwm_fan_speed_adjust_cfg speed_adjust_cfg[1];
	struct pwm_fan_duty_speed_map duty_speed_map[FAN_DUTY_MAP_NUM];
	struct delayed_work speed_dynamic_adjust_work;
	struct delayed_work speed_detect_req_work;
	struct delayed_work ldo_status_detect_work;
};
_Static_assert(sizeof(struct pwm_fan_ctx) == 712, "pwm_fan_ctx");
_Static_assert(offsetof(struct pwm_fan_ctx, speed_adjust_cfg) == 236, "speed_adjust_cfg");
_Static_assert(offsetof(struct pwm_fan_ctx, speed_dynamic_adjust_work) == 304, "works");
_Static_assert(offsetof(struct pwm_fan_ctx, ldo_nb) == 88, "ldo_nb");

/* functions not reconstructed yet (see STATUS.md) */
int pwm_fan_power_on(struct pwm_fan_ctx *ctx, bool on);
int set_pwn_fan_target_level(struct pwm_fan_ctx *ctx, unsigned int level);

static ssize_t fan_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t fan_sysfs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info fan_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(fan_sysfs, 0444, FAN_PROP_FAN_SUPPORT, fan_support),
	mca_sysfs_attr_rw(fan_sysfs, 0664, FAN_PROP_PWM_DUTY, pwm_duty),
	mca_sysfs_attr_rw(fan_sysfs, 0664, FAN_PROP_TARGET_LEVEL, target_level),
	mca_sysfs_attr_rw(fan_sysfs, 0664, FAN_PROP_REAL_SPEED, real_speed),
};

static struct attribute *fan_sysfs_attrs[FAN_PROP_MAX + 1];

static const struct attribute_group fan_sysfs_attr_group = {
	.attrs = fan_sysfs_attrs,
};

static ssize_t fan_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	const struct mca_hwid *hwid = mca_get_hwid_info();
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	int val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name, fan_sysfs_field_tbl, FAN_PROP_MAX);
	if (!attr_info)
		return -1;
	if (!ctx) {
#line 427
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}

	switch (attr_info->sysfs_attr_name) {
	case FAN_PROP_FAN_SUPPORT:
		val = ctx->fan_support;
		if (hwid && hwid->product_name && !strcmp(hwid->product_name, "yanan"))
			val = 0;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FAN_PROP_PWM_DUTY:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", ctx->pwm_duty);
		break;
	case FAN_PROP_TARGET_LEVEL:
		if (ctx->target_level_dbg == FAN_SPEED_LEVEL_DBG_OFF)
			count = scnprintf(buf, PAGE_SIZE, "%d\n", ctx->target_level);
		else
			count = scnprintf(buf, PAGE_SIZE, "%d\n", ctx->target_level_dbg - FAN_SPEED_LEVEL_DBG_OFF);
		break;
	case FAN_PROP_REAL_SPEED:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", ctx->real_speed);
		break;
	default:
		break;
	}

	return count;
}

static int fan_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(fan_sysfs_attrs, fan_sysfs_field_tbl, FAN_PROP_MAX);
	return mca_sysfs_create_link_group("hw_monitor", "pwm_fan", dev, &fan_sysfs_attr_group);
}

static void pwm_fan_cancel_work(struct pwm_fan_ctx *ctx)
{
	cancel_delayed_work_sync(&ctx->speed_dynamic_adjust_work);
	cancel_delayed_work_sync(&ctx->speed_detect_req_work);
	if (ctx->ldo_type == 1)
		cancel_delayed_work_sync(&ctx->ldo_status_detect_work);
}

static __always_inline void pwm_fan_cleanup(struct platform_device *pdev)
{
	struct pwm_fan_ctx *ctx = platform_get_drvdata(pdev);

	if (ctx) {
		pwm_fan_cancel_work(ctx);
		mca_sysfs_remove_link_group("hw_monitor", "pwm_fan", &pdev->dev, &fan_sysfs_attr_group);
		if (ctx->fan_support)
			pwm_fan_power_on(ctx, false);
	} else {
		mca_sysfs_remove_link_group("hw_monitor", "pwm_fan", &pdev->dev, &fan_sysfs_attr_group);
#line 692
		mca_log_err("ctx is NULL\n");
	}
}

static int pwm_fan_remove(struct platform_device *pdev)
{
	pwm_fan_cleanup(pdev);
	return 0;
}

static void pwm_fan_shutdown(struct platform_device *pdev)
{
	pwm_fan_cleanup(pdev);
}

static int pwm_fan_ldo_notify(struct notifier_block *nb, unsigned long event, void *data)
{
	struct pwm_fan_ctx *ctx = container_of(nb, struct pwm_fan_ctx, ldo_nb);
	char buf[FAN_EVENT_BUF_SIZE] = "MCA_FAN_ERROR_INFO=FAN_LDO_OCP";
	struct mca_event_notify_data event_data = { .event_len = 30 };

	if (!ctx || !ctx->fan_support) {
#line 708
		mca_log_err("ctx is NULL or not support\n");
		return -EINVAL;
	}

#line 712
	mca_log_err("get ldo event: %lu\n", event);
	if (event == REGULATOR_EVENT_OVER_CURRENT) {
		cancel_delayed_work_sync(&ctx->speed_dynamic_adjust_work);
		ctx->target_level_dbg = FAN_SPEED_LEVEL_DBG_OFF;
		ctx->real_speed = 2;
		set_pwn_fan_target_level(ctx, 0);
		event_data.event = buf;
		mca_event_report_uevent(&event_data);
	}

	return NOTIFY_OK;
}

static void speed_detect_req_work_func(struct work_struct *work)
{
	struct pwm_fan_ctx *ctx = container_of(work, struct pwm_fan_ctx, speed_detect_req_work.work);
	char buf[FAN_EVENT_BUF_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	if (!ctx || !ctx->fan_support) {
#line 630
		mca_log_err("ctx is NULL or not support\n");
		return;
	}

	if (ctx->target_level_dbg != FAN_SPEED_LEVEL_DBG_OFF || ctx->target_level || ctx->fan_enabled)
		memcpy(buf, "MCA_FAN_ENABLE=1", sizeof("MCA_FAN_ENABLE=1"));
	else
		memcpy(buf, "MCA_FAN_ENABLE=0", sizeof("MCA_FAN_ENABLE=0"));
	event_data.event = buf;
	event_data.event_len = 16;
	mca_event_report_uevent(&event_data);
}

static int pwm_fan_suspend(struct device *dev)
{
	return 0;
}

static int pwm_fan_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops pwm_fan_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(pwm_fan_suspend, pwm_fan_resume)
};

/* temporary: keep the not yet referenced statics alive for the comparison loop */
static void *const fan_keep[] __used = {
	fan_sysfs_create_group, pwm_fan_ldo_notify, speed_detect_req_work_func, pwm_fan_remove, pwm_fan_shutdown, (void *)&pwm_fan_pm,
};

MODULE_DESCRIPTION("PWM FAN driver");
MODULE_LICENSE("GPL");
