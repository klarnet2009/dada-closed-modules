// MIUI ADD: Performance_TurboSched
/*
 * Copyright (C) Xiaomi Technologies Co., Ltd. All rights reserved.
 *
 * File name: speed_touch.c
 * Description: boost the performance of input event processing.
 * Author: suzhidao@xiaomi.com
 * Version: 1.0
 * Author: xiehuilong@xiaomi.com
 * Version: 1.1
 * Date: 2024/06/06
 *
 * This program is free sofrware; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation;
 */


#define pr_fmt(fmt) "speed touch: " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>

enum speed_touch_boost_target {
	ST_UI_TASK,
	ST_RENDER_TASK,
	ST_SF_TASK,
	ST_HWC_TASK,
	BOOST_TARGET_MAX,
};

static int speed_touch_debug = 1;
module_param(speed_touch_debug, uint, 0644);
static int vsync_period;
module_param(vsync_period, uint, 0644);
static int sf_available_buffer_size;
static char cur_layer_name[128];
module_param_string(cur_layer_name, cur_layer_name, sizeof(cur_layer_name), 0644);
static int track_vsync_signal;
module_param(track_vsync_signal, uint, 0644);

extern atomic_t speed_touch_producer_in_boost;
extern atomic_t speed_touch_consumer_in_boost;

extern bool in_interest_input_event(void);
extern void speed_touch_store_freq(int target);
extern void speed_touch_restore_freq(int target);
extern void trigger_fboost_work(void);

static int set_sf_available_buffer_size(const char *buf, const struct kernel_param *kp) {
	unsigned int val = 0;
	bool need_trigger = false;

	if (sscanf(buf, "%u\n", &val) != 1) {
		return -EINVAL;
	}
	sf_available_buffer_size = val;
	if (in_interest_input_event()) {
		if(unlikely(speed_touch_debug)){
			pr_err("set_sf_available_buffer_size: buffer count %d, state (%d, %d)\n", 
				sf_available_buffer_size, atomic_read(&speed_touch_consumer_in_boost), atomic_read(&speed_touch_producer_in_boost));
		}
		// SF and HWC burdun
		if (sf_available_buffer_size >= 2) {
			//boost sf & hwc and reset ui & render
			if (atomic_read(&speed_touch_consumer_in_boost)) {
				need_trigger = true;
				speed_touch_restore_freq(ST_UI_TASK);
				speed_touch_restore_freq(ST_RENDER_TASK);
				atomic_set(&speed_touch_consumer_in_boost, 0);
			}

			if (!atomic_read(&speed_touch_producer_in_boost)) {
				need_trigger = true;
				speed_touch_store_freq(ST_SF_TASK);
				speed_touch_store_freq(ST_HWC_TASK);
				atomic_set(&speed_touch_producer_in_boost, 1);
			}

		} else if (sf_available_buffer_size == 0) {
			//boost ui & render and reset sf & hwc
			if (atomic_read(&speed_touch_producer_in_boost)) {
				need_trigger = true;
				speed_touch_restore_freq(ST_SF_TASK);
				speed_touch_restore_freq(ST_HWC_TASK);
				atomic_set(&speed_touch_producer_in_boost, 0);
			}

			if (!atomic_read(&speed_touch_consumer_in_boost)) {
				need_trigger = true;
				speed_touch_store_freq(ST_UI_TASK);
				speed_touch_store_freq(ST_RENDER_TASK);
				atomic_set(&speed_touch_consumer_in_boost, 1);
			}

		} else {
			if (atomic_read(&speed_touch_producer_in_boost)) {
				//cancle sf & hwc
				need_trigger = true;
				speed_touch_restore_freq(ST_SF_TASK);
				speed_touch_restore_freq(ST_HWC_TASK);
				atomic_set(&speed_touch_producer_in_boost, 0);
			}

			if (atomic_read(&speed_touch_consumer_in_boost)) {
				//cancle ui & sf
				need_trigger = true;
				speed_touch_restore_freq(ST_UI_TASK);
				speed_touch_restore_freq(ST_RENDER_TASK);
				atomic_set(&speed_touch_consumer_in_boost, 0);
			}
		}
	} else {
		//if not in input event, cancle all boost and reset flag.
		if (atomic_read(&speed_touch_producer_in_boost)) {
			//cancle sf & hwc
			need_trigger = true;
			speed_touch_restore_freq(ST_SF_TASK);
			speed_touch_restore_freq(ST_HWC_TASK);
			atomic_set(&speed_touch_producer_in_boost, 0);
		}

		if (atomic_read(&speed_touch_consumer_in_boost)) {
			//cancle ui & sf
			need_trigger = true;
			speed_touch_restore_freq(ST_UI_TASK);
			speed_touch_restore_freq(ST_RENDER_TASK);
			atomic_set(&speed_touch_consumer_in_boost, 0);
		}
	}
	if(in_interest_input_event() && unlikely(speed_touch_debug)){
			pr_err("set_sf_available_buffer_size: buffer count %d, state (%d, %d)\n", 
				sf_available_buffer_size, atomic_read(&speed_touch_consumer_in_boost), atomic_read(&speed_touch_producer_in_boost));
	}
	if (need_trigger) {
		trigger_fboost_work();
	}
	return 0;
}


static const struct kernel_param_ops param_ops_sf_buffer = {
	.set = set_sf_available_buffer_size,
	.get = param_get_int,
};

module_param_cb(sf_available_buffer_size, &param_ops_sf_buffer, &sf_available_buffer_size, 0644);

static struct miscdevice speed_touch_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "speed_touch",
};

static int __init speed_touch_init(void)
{
	int ret;
	ret = misc_register(&speed_touch_dev);
	return ret;
}

static void __exit speed_touch_exit(void)
{
	misc_deregister(&speed_touch_dev);
}

late_initcall(speed_touch_init);
module_exit(speed_touch_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Speed Touch Driver");
// END Performance_TurboSched
