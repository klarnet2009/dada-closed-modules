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
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>

#define MCA_LOG_TAG "xm_fan"

/*
 * The stock link (module.lds) merges all .bss.* sections, so the zero-sized mutex key of pwm_fan_probe() shares its address
 * (and, being first in the symbol table, its name) with the static last_target_speed; put the key into that variable's
 * (-fdata-sections) section so that compare_ko resolves the same relocation target names.
 */
#undef mutex_init
#define mutex_init(mutex)							\
do {										\
	static struct lock_class_key __key					\
		__attribute__((section(".bss.speed_dynamic_adjust_work_func.last_target_speed")));	\
	__mutex_init((mutex), #mutex, &__key);					\
} while (0)

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
	FAN_SPEED_LEVEL_DBG_TURBO,
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
	char name[20];
	struct device *dev;
	struct mutex lock;
	struct regulator *ldo;
	struct notifier_block ldo_nb;
	struct pwm_device *pwm_dev;
	struct pwm_state fan_pwm_state;
	u8 fan_support;
	u8 fan_enabled;
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
static int pwm_fan_power_on(struct pwm_fan_ctx *ctx, bool on);
static int set_pwn_fan_target_level(struct pwm_fan_ctx *ctx, unsigned int level);
int pwm_reg_config(struct pwm_fan_ctx *ctx, int duty_percent);
static int set_pwm(struct pwm_fan_ctx *ctx, unsigned long duty);

static ssize_t fan_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fan_sysfs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

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

static int pwm_fan_power_on(struct pwm_fan_ctx *ctx, bool on)
{
	int ret;

	if (!ctx->fan_support) {
#line 68
		mca_log_err("ctx is NULL or not support\n");
		return -EINVAL;
	}
	if (ctx->fan_enabled == on) {
#line 73
		mca_log_err("pwm_fan power mode no change\n");
		return 0;
	}
	if (!ctx->ldo) {
#line 78
		mca_log_err("ldo regulator is NULL\n");
		return -EINVAL;
	}

	if (on) {
		ret = regulator_set_voltage(ctx->ldo, ctx->ldo_voltage_uv, INT_MAX);
		if (ret < 0) {
#line 84
			mca_log_err("set ldo voltage failed\n");
			return ret;
		}
		ret = regulator_enable(ctx->ldo);
		if (ret < 0) {
#line 89
			mca_log_err("regulator_enable failed\n");
			return ret;
		}
	} else {
		ret = regulator_disable(ctx->ldo);
		if (ret < 0) {
#line 95
			mca_log_err("regulator_disable failed\n");
			return ret;
		}
	}
#line 101
	mca_log_err("Power %s\n", on ? "ON" : "OFF");
	ctx->fan_enabled = on;
	if (!on)
		ctx->real_speed = 2;
	return 0;
}

int pwm_reg_config(struct pwm_fan_ctx *ctx, int duty_percent)
{
	struct pwm_state state;
	int ret;
	unsigned int duty = duty_percent;

	if (!ctx || !ctx->fan_support || !ctx->pwm_dev) {
#line 115
		mca_log_err("ctx is NULL or not support\n");
		return -EINVAL;
	}

	if (duty > 100) {
#line 122
		mca_log_err("Invalid duty_percent: %d, must be %d-%d\n", duty, 0, 100);
		return -EINVAL;
	}

	ctx->fan_pwm_state.enabled = duty != 0;
	ctx->fan_pwm_state.duty_cycle = div_u64(ctx->fan_pwm_state.period * duty, 100);
	state = ctx->fan_pwm_state;
#line 137
	mca_log_err("[Set PWM] period=%llu duty=%llu enable=%d polarity=%d\n", state.period, state.duty_cycle, state.enabled, state.polarity);

	ret = pwm_apply_state(ctx->pwm_dev, &state);
	if (ret < 0) {
#line 141
		mca_log_err("pwm_apply_state failed\n");
		return ret;
	}

	{
		struct pwm_state *cur = &ctx->pwm_dev->state;
		u64 per = cur->period, du = cur->duty_cycle;
		int pol = cur->polarity;
		bool en = cur->enabled;

#line 150
		mca_log_err("[Set PWM AFTER] period=%llu duty=%llu enable=%d polarity=%d\n", per, du, en, pol);
	}
	return ret;
}

static int __set_pwm(struct pwm_fan_ctx *ctx, unsigned long duty)
{
	int ret;

	if (!ctx->fan_support) {
#line 159
		mca_log_err("ctx is NULL or not support\n");
		return -EINVAL;
	}

#line 164
	mca_log_err("__set_pwm enter: duty=%lu, cur_duty=%lu, enabled=%d\n", duty, (unsigned long)ctx->pwm_duty, ctx->fan_enabled);
	if (ctx->pwm_duty == duty) {
#line 167
		mca_log_err("pwm duty is same, no need to set\n");
		return 0;
	}

	if (!ctx->fan_enabled && duty) {
		ret = pwm_fan_power_on(ctx, true);
		if (ret < 0) {
#line 174
			mca_log_err("pwm_fan_power_on failed\n");
			return ret;
		}
		if (ctx->gpio_shifter_en >= 0) {
			gpio_set_value(ctx->gpio_shifter_en, 1);
#line 180
			mca_log_err("enable gpio_shifter success\n");
		}
		ret = pwm_reg_config(ctx, 0);
		if (ret < 0) {
#line 184
			mca_log_err("pre pwm_reg_config failed\n");
			return ret;
		}
		msleep(100);
		ret = pwm_reg_config(ctx, duty);
		if (ret < 0) {
#line 190
			mca_log_err("pwm_reg_config failed\n");
			return ret;
		}
	} else if (ctx->fan_enabled && !duty) {
		ret = pwm_reg_config(ctx, 0);
		if (ret < 0) {
#line 196
			mca_log_err("pwm_reg_config failed\n");
			return ret;
		}
		msleep(100);
		if (ctx->gpio_shifter_en >= 0) {
			gpio_set_value(ctx->gpio_shifter_en, 0);
#line 202
			mca_log_err("set gpio_shifter_en = 0\n");
		}
		ret = pwm_fan_power_on(ctx, false);
		if (ret < 0) {
#line 207
			mca_log_err("pwm_fan_power_on failed\n");
			return ret;
		}
	} else {
		ret = pwm_reg_config(ctx, duty);
		if (ret < 0) {
#line 213
			mca_log_err("pwm_reg_config failed\n");
			return ret;
		}
		if (!duty && ctx->gpio_shifter_en >= 0) {
			gpio_set_value(ctx->gpio_shifter_en, 0);
#line 218
			mca_log_err("set gpio_shifter_en = 0\n");
		}
	}

#line 222
	mca_log_err("__set_pwm exit: new duty=%lu\n", duty);
	ctx->pwm_duty = duty;
	return 0;
}

static int set_pwm(struct pwm_fan_ctx *ctx, unsigned long duty)
{
	int ret;

	if (!ctx->fan_support) {
#line 231
		mca_log_err("ctx is NULL or not support\n");
		return -EINVAL;
	}

	mutex_lock(&ctx->lock);
	ret = __set_pwm(ctx, duty);
	mutex_unlock(&ctx->lock);
	return ret;
}

static int set_pwn_fan_target_level(struct pwm_fan_ctx *ctx, unsigned int level)
{
	int ret;

	if (!ctx->fan_support) {
#line 265
		mca_log_err("ctx is NULL or not support\n");
		return -EINVAL;
	}

	if (level >= FAN_SPEED_LEVEL_MAX) {
		if (level > FAN_SPEED_LEVEL_DBG_OFF && level < FAN_SPEED_LEVEL_DBG_TURBO + 1) {
			ctx->target_level_dbg = level;
			level -= FAN_SPEED_LEVEL_DBG_OFF;
		} else if (level == FAN_SPEED_LEVEL_DBG_OFF) {
			if (ctx->target_level_dbg == FAN_SPEED_LEVEL_DBG_OFF) {
#line 286
				mca_log_err("target_level_dbg is same, no need to set\n");
				return 0;
			}
			level = ctx->target_level;
			ctx->target_level_dbg = FAN_SPEED_LEVEL_DBG_OFF;
		} else {
#line 300
			mca_log_err("target_level invalid\n");
			return 0;
		}
	} else {
		if (ctx->target_level_dbg != FAN_SPEED_LEVEL_DBG_OFF) {
#line 276
			mca_log_err("target_level_dbg active, target_level not set\n");
			return 0;
		}
		if (ctx->target_level == level) {
#line 279
			mca_log_err("target_level is same, no need to set\n");
			return 0;
		}
		ctx->target_level = level;
	}

	ctx->target_speed = ctx->duty_speed_map[level].speed;

	mutex_lock(&ctx->lock);
	if (!!ctx->fan_enabled ^ !!level) {
		ctx->speed_adjust_cnt = 0;
		ctx->err_stat_report_cnt = 0;
		cancel_delayed_work_sync(&ctx->speed_detect_req_work);
		if (ctx->ldo_type == 1)
			cancel_delayed_work_sync(&ctx->ldo_status_detect_work);
		schedule_delayed_work(&ctx->speed_detect_req_work, msecs_to_jiffies(1000));
		if (ctx->ldo_type == 1)
			schedule_delayed_work(&ctx->ldo_status_detect_work, msecs_to_jiffies(2000));
	}
	mutex_unlock(&ctx->lock);

	ret = set_pwm(ctx, ctx->duty_speed_map[level].duty);
	if (ret) {
#line 320
		mca_log_err("setting speed_level failed!\n");
		return ret;
	}

#line 326
	mca_log_err("level:%d[%d %d], duty:%d, speed:%d, en:%d\n", level, ctx->target_level, ctx->target_level_dbg, ctx->duty_speed_map[level].duty, ctx->target_speed, ctx->fan_enabled);
	cancel_delayed_work_sync(&ctx->speed_dynamic_adjust_work);
	if (ctx->fan_enabled)
		schedule_delayed_work(&ctx->speed_dynamic_adjust_work, msecs_to_jiffies(5000));
	return 0;
}

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
	char buf[FAN_EVENT_BUF_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	event_data.event_len = 30;
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
		memcpy(buf, "MCA_FAN_ERROR_INFO=FAN_LDO_OCP", sizeof("MCA_FAN_ERROR_INFO=FAN_LDO_OCP"));
		event_data.event = buf;
		mca_event_report_uevent(&event_data);
	}

	return NOTIFY_OK;
}

static int pwm_fan_ldo_init(struct pwm_fan_ctx *ctx)
{
	int ret;

	if (!of_find_property(ctx->dev->of_node, "ldo-supply", NULL)) {
#line 812
		mca_log_err("Failed to get ldo-supply cfg\n");
		return -EINVAL;
	}

	ctx->ldo = devm_regulator_get(ctx->dev, "ldo");
	if (IS_ERR(ctx->ldo)) {
		ret = PTR_ERR(ctx->ldo);
#line 818
		mca_log_err("Failed to get ldo regulator: %d\n", ret);
		ctx->ldo = NULL;
		return ret;
	}

	ctx->ldo_nb.notifier_call = pwm_fan_ldo_notify;
	ret = devm_regulator_register_notifier(ctx->ldo, &ctx->ldo_nb);
	if (ret) {
#line 825
		mca_log_err("Failed to register ldo notifier: %d\n", ret);
		return ret;
	}

	if (regulator_is_enabled(ctx->ldo)) {
#line 829
		mca_log_err("ldo is on, need to turn off\n");
		ret = regulator_disable(ctx->ldo);
		if (ret)
#line 832
			mca_log_err("Failed to disable ldo: %d\n", ret);
		msleep(10);
	}

	ctx->fan_enabled = regulator_is_enabled(ctx->ldo) != 0;
#line 836
	mca_log_err("ldo is %s\n", ctx->fan_enabled ? "on" : "off");
	return 0;
}

static void ldo_status_detect_work_func(struct work_struct *work)
{
	struct pwm_fan_ctx *ctx = container_of(work, struct pwm_fan_ctx, ldo_status_detect_work.work);
	char buf[FAN_EVENT_BUF_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	int mode;

	if (!ctx || !ctx->fan_support) {
#line 657
		mca_log_err("ctx is NULL or not support\n");
		return;
	}

	if (!ctx->fan_enabled) {
#line 662
		mca_log_err("pwm fan is disabled\n");
		ctx->err_stat_report_cnt = 0;
		return;
	}

	if (ctx->target_level_dbg == FAN_SPEED_LEVEL_DBG_OFF && !ctx->target_level) {
#line 668
		mca_log_err("target level is 0\n");
		ctx->err_stat_report_cnt = 0;
		return;
	}

	mode = regulator_get_mode(ctx->ldo);
#line 674
	mca_log_err("ldo_error flags:%d, cnt:%d\n", mode, ctx->err_stat_report_cnt);
	if (mode) {
		ctx->err_stat_report_cnt++;
		memcpy(buf, "MCA_FAN_ERROR_INFO=FAN_LDO_ERROR", sizeof("MCA_FAN_ERROR_INFO=FAN_LDO_ERROR"));
		event_data.event = buf;
		event_data.event_len = 32;
		mca_event_report_uevent(&event_data);
	}
	if (ctx->err_stat_report_cnt <= 2)
		schedule_delayed_work(&ctx->ldo_status_detect_work, msecs_to_jiffies(2000));
}

static void speed_detect_req_work_func(struct work_struct *work)
{
	struct pwm_fan_ctx *ctx = container_of(work, struct pwm_fan_ctx, speed_detect_req_work.work);
	char buf[FAN_EVENT_BUF_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { .event = NULL, .event_len = 0 };

	if (!ctx || !ctx->fan_support) {
#line 630
		mca_log_err("ctx is NULL or not support\n");
		return;
	}

	if (ctx->target_level_dbg != FAN_SPEED_LEVEL_DBG_OFF || ctx->target_level || ctx->fan_enabled) {
		memcpy(buf, "MCA_FAN_ENABLE=1", sizeof("MCA_FAN_ENABLE=1"));
		event_data.event = buf;
		event_data.event_len = 16;
	} else {
		memcpy(buf, "MCA_FAN_ENABLE=0", sizeof("MCA_FAN_ENABLE=0"));
		event_data.event = buf;
		event_data.event_len = 16;
	}
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

static void speed_dynamic_adjust_work_func(struct work_struct *work)
{
	static unsigned int last_target_speed;
	struct pwm_fan_ctx *ctx = container_of(work, struct pwm_fan_ctx, speed_dynamic_adjust_work.work);
	char buf[FAN_EVENT_BUF_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	struct pwm_fan_speed_adjust_cfg *cfg;
	int gap, adjust, new_duty;
	bool ok;

	if (!ctx || !ctx->fan_support) {
#line 546
		mca_log_err("ctx is NULL or not support\n");
		return;
	}

	if (!ctx->fan_enabled) {
#line 551
		mca_log_err("pwm fan is disabled\n");
		ctx->speed_adjust_cnt = 0;
		return;
	}

	if (ctx->target_level_dbg == FAN_SPEED_LEVEL_DBG_OFF && !ctx->target_level) {
#line 557
		mca_log_err("target level is 0\n");
		ctx->speed_adjust_cnt = 0;
		return;
	}

	if (ctx->real_speed == 2) {
#line 563
		mca_log_err("speed detection is not ready, waiting...\n");
		schedule_delayed_work(&ctx->speed_dynamic_adjust_work, msecs_to_jiffies(1000));
		return;
	}

	cfg = &ctx->speed_adjust_cfg[0];
	if (ctx->real_speed < cfg->min_speed || ctx->real_speed > cfg->max_speed) {
#line 569
		mca_log_err("real speed is out of range, report it\n");
		memcpy(buf, "MCA_FAN_ERROR_INFO=SPEED_OUT_OF_RANGE", sizeof("MCA_FAN_ERROR_INFO=SPEED_OUT_OF_RANGE"));
		event_data.event = buf;
		event_data.event_len = 37;
		mca_event_report_uevent(&event_data);
		return;
	}

	if (last_target_speed != ctx->target_speed) {
#line 578
		mca_log_err("target speed changed, reset counter\n");
		ctx->speed_adjust_cnt = 0;
		last_target_speed = ctx->target_speed;
	}

	gap = ctx->real_speed - ctx->target_speed;
	if (gap > (int)cfg->speed_gap_thres) {
		ctx->speed_adjust_cnt++;
		adjust = -1;
		ok = false;
	} else if (gap < -(int)cfg->speed_gap_thres) {
		ctx->speed_adjust_cnt++;
		adjust = 1;
		ok = false;
	} else {
		ctx->speed_adjust_cnt = 0;
		adjust = 0;
		ok = true;
	}

	if (ctx->pwm_duty + adjust < cfg->duty_min)
		new_duty = cfg->duty_min;
	else
		new_duty = min(ctx->pwm_duty + adjust, cfg->duty_max);

	if (ctx->speed_adjust_cnt >= cfg->adjust_cnt_max) {
#line 602
		mca_log_err("speed adjust failed, report it\n");
		memcpy(buf, "MCA_FAN_ERROR_INFO=SPEED_ADJUST_FAIL", sizeof("MCA_FAN_ERROR_INFO=SPEED_ADJUST_FAIL"));
		event_data.event = buf;
		event_data.event_len = 36;
		mca_event_report_uevent(&event_data);
		return;
	}

#line 612
	mca_log_err("speed_gap:%d[%d %d], adjust:%d[%d->%d]\n", gap, ctx->target_speed, ctx->real_speed, ctx->speed_adjust_cnt, ctx->pwm_duty, new_duty);
	if (new_duty != ctx->pwm_duty)
		set_pwm(ctx, new_duty);
	if (!ok)
		schedule_delayed_work(&ctx->speed_dynamic_adjust_work, msecs_to_jiffies(2000));
}

static ssize_t fan_sysfs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int val = 0;
	int ret;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name, fan_sysfs_field_tbl, FAN_PROP_MAX);
	if (!attr_info)
		return -1;
	if (!ctx) {
#line 473
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case FAN_PROP_PWM_DUTY:
		if (!ctx->fan_support) {
#line 483
			mca_log_err("ctx is NULL or not support\n");
			break;
		}
		if (val < 0)
			val = 0;
		else if (val > 100)
			val = 100;
		if ((ctx->fan_enabled && !val) || (!ctx->fan_enabled && val)) {
			ctx->err_stat_report_cnt = 0;
			cancel_delayed_work_sync(&ctx->speed_detect_req_work);
			if (ctx->ldo_type == 1)
				cancel_delayed_work_sync(&ctx->ldo_status_detect_work);
			schedule_delayed_work(&ctx->speed_detect_req_work, msecs_to_jiffies(1000));
			if (ctx->ldo_type == 1)
				schedule_delayed_work(&ctx->ldo_status_detect_work, msecs_to_jiffies(2000));
		}
		ret = set_pwm(ctx, val);
		if (ret) {
#line 502
			mca_log_err("set pwm_duty failed!\n");
		}
		break;
	case FAN_PROP_TARGET_LEVEL:
		if (!ctx->fan_support) {
#line 506
			mca_log_err("ctx is NULL or not support\n");
			break;
		}
		ret = set_pwn_fan_target_level(ctx, val);
		if (ret) {
#line 511
			mca_log_err("set target_level failed!\n");
		}
		break;
	case FAN_PROP_REAL_SPEED:
		if (!ctx->fan_support) {
#line 515
			mca_log_err("ctx is NULL or not support\n");
			break;
		}
		ctx->real_speed = val;
#line 519
		mca_log_err("set real_speed: %d\n", val);
		break;
	default:
		break;
	}

	return count;
}

static __always_inline int pwm_fan_support_check(struct pwm_fan_ctx *ctx)
{
	if (!ctx || !ctx->dev) {
#line 343
		mca_log_err("ctx or dev is NULL\n");
		return -EINVAL;
	}
	if (!ctx->dev->of_node) {
#line 350
		mca_log_err("device node missing\n");
		return -EINVAL;
	}
	ctx->fan_support = true;
	return 0;
}

static const struct pwm_fan_speed_adjust_cfg default_speed_adjust_cfg = { 0, 40000, 500, 30, 0, 100 };
static const struct pwm_fan_duty_speed_map default_duty_speed_map[FAN_DUTY_MAP_NUM] = {
	{ 0, 0 }, { 51, 12000 }, { 65, 14000 }, { 76, 16000 }, { 98, 19800 },
};

static __always_inline void pwm_fan_parse_dt(struct pwm_fan_ctx *ctx)
{
	struct device_node *np = ctx->dev->of_node;
	int n;

	if (!np) {
#line 737
		mca_log_err("ctx or node is NULL\n");
		return;
	}

	mca_parse_dts_u32(np, "pwm-freq", &ctx->pwm_freq, 37500);
	if (!ctx->pwm_freq)
		ctx->pwm_freq = 37500;
	ctx->fan_pwm_state.enabled = false;
	ctx->fan_pwm_state.period = DIV_ROUND_CLOSEST(1000000000U, ctx->pwm_freq);
	ctx->fan_pwm_state.duty_cycle = 0;
#line 749
	mca_log_info("pwm freq=%u Hz, period=%llu ns\n", ctx->pwm_freq, ctx->fan_pwm_state.period);

	mca_parse_dts_u32(np, "ldo-type", &ctx->ldo_type, 0);
#line 752
	mca_log_info("ldo_type=%u\n", ctx->ldo_type);
	mca_parse_dts_u32(np, "ldo-voltage-microvolt", &ctx->ldo_voltage_uv, 3056000);
#line 756
	mca_log_info("ldo_voltage_uv=%u uV\n", ctx->ldo_voltage_uv);

	n = mca_parse_dts_u32_count(np, "speed_adjust_cfg", 1, 6);
	if (n >= 1) {
		mca_parse_dts_u32_array(np, "speed_adjust_cfg", (u32 *)ctx->speed_adjust_cfg, n);
	} else {
		ctx->speed_adjust_cfg[0] = default_speed_adjust_cfg;
#line 767
		mca_log_err("speed_adjust_cfg not found, using default values\n");
	}

	n = mca_parse_dts_u32_count(np, "duty_speed_map", FAN_DUTY_MAP_NUM, 2);
	if (n >= 1) {
		mca_parse_dts_u32_array(np, "duty_speed_map", (u32 *)ctx->duty_speed_map, n);
	} else {
		memcpy(ctx->duty_speed_map, default_duty_speed_map, sizeof(ctx->duty_speed_map));
#line 779
		mca_log_err("duty_speed_map not found, using default values\n");
	}
}

static __always_inline int pwm_fan_gpio_init(struct pwm_fan_ctx *ctx)
{
	int ret;

	ctx->gpio_shifter_en = of_get_named_gpio(ctx->dev->of_node, "shifter_en", 0);
	if (ctx->gpio_shifter_en < 0) {
#line 789
		mca_log_err("Invalid GPIO_SHIFTER_EN: %d\n", ctx->gpio_shifter_en);
		return ctx->gpio_shifter_en;
	}

	ret = devm_gpio_request(ctx->dev, ctx->gpio_shifter_en, "shifter_en");
	if (ret) {
#line 794
		mca_log_err("Failed to request GPIO_SHIFTER_EN: %d\n", ret);
		return ret;
	}

	ret = gpio_direction_output(ctx->gpio_shifter_en, 0);
	if (ret) {
#line 799
		mca_log_err("Failed to set GPIO_SHIFTER_EN as output: %d\n", ret);
		return ret;
	}
#line 802
	mca_log_err("GPIO_SHIFTER_EN set as output with initial value 0\n");
	return 0;
}

static int pwm_fan_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_fan_ctx *ctx;
	struct device_node *np;
	int ret;

#line 880
	mca_log_err("enter pwm_fan_probe\n");
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
#line 884
		mca_log_err("failed to allocate memory\n");
		return -ENOMEM;
	}

	ctx->dev = dev;
	ctx->target_level_dbg = FAN_SPEED_LEVEL_DBG_OFF;
	ctx->real_speed = 2;
	ctx->err_stat_report_cnt = 0;
	mutex_init(&ctx->lock);
	platform_set_drvdata(pdev, ctx);

	np = pdev->dev.of_node;
	if (!np) {
#line 898
		mca_log_err("device tree node missing\n");
		return -EINVAL;
	}

	ret = strscpy(ctx->name, np->name, sizeof(ctx->name));
	if (ret < 0) {
#line 905
		mca_log_err("copy fan name failed\n");
		return ret;
	}

	ret = pwm_fan_support_check(ctx);
	if (ret) {
#line 912
		mca_log_err("fan support check failed: %d\n", ret);
		return ret;
	}

	ctx->pwm_dev = devm_fwnode_pwm_get(ctx->dev, of_fwnode_handle(np), NULL);
	if (IS_ERR(ctx->pwm_dev)) {
		ret = PTR_ERR(ctx->pwm_dev);
#line 925
		mca_log_err("failed to get pwm: %d\n", ret);
		return ret;
	}

	pwm_fan_parse_dt(ctx);

	ret = pwm_fan_gpio_init(ctx);
	if (ret) {
#line 933
		mca_log_err("pwm_fan_gpio_init failed: %d\n", ret);
		return ret;
	}

	ret = pwm_fan_ldo_init(ctx);
	if (ret) {
#line 939
		mca_log_err("pwm_fan_ldo_init failed: %d\n", ret);
		return ret;
	}

	{
		struct pwm_state *cur = &ctx->pwm_dev->state;
		u64 per = cur->period, du = cur->duty_cycle;
		int pol = cur->polarity;
		bool en = cur->enabled;

#line 948
		mca_log_err("[PWM INIT] period=%llu duty=%llu enable=%d polarity=%d\n", per, du, en, pol);
	}
	ctx->pwm_duty = 0;
	ret = pwm_reg_config(ctx, 0);
	if (ret) {
#line 952
		mca_log_err("Failed to configure PWM: %d\n", ret);
	}

	INIT_DELAYED_WORK(&ctx->speed_dynamic_adjust_work, speed_dynamic_adjust_work_func);
	INIT_DELAYED_WORK(&ctx->speed_detect_req_work, speed_detect_req_work_func);
	if (ctx->ldo_type == 1)
		INIT_DELAYED_WORK(&ctx->ldo_status_detect_work, ldo_status_detect_work_func);

	ret = fan_sysfs_create_group(dev);
	if (ret) {
#line 967
		mca_log_err("failed to create sysfs group\n");
		pwm_fan_cancel_work(ctx);
		return ret;
	}

#line 973
	mca_log_err("pwm_fan_probe successful\n");
	return 0;
}

static const struct of_device_id of_pwm_fan_match[] = {
	{ .compatible = "xm,xm-pwm-fan" },
	{ },
};
MODULE_DEVICE_TABLE(of, of_pwm_fan_match);

static struct platform_driver pwm_fan_driver = {
	.probe = pwm_fan_probe,
	.remove = pwm_fan_remove,
	.shutdown = pwm_fan_shutdown,
	.driver = {
		.name = "pwm-fan",
		.of_match_table = of_pwm_fan_match,
		.pm = &pwm_fan_pm,
	},
};
module_platform_driver(pwm_fan_driver);

MODULE_AUTHOR("linjiashuo <linjiashuo@xiaomi.com>");
MODULE_ALIAS("platform:pwm-fan");
MODULE_DESCRIPTION("PWM FAN driver");
MODULE_LICENSE("GPL");
