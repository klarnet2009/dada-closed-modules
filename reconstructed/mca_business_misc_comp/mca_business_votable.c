// SPDX-License-Identifier: GPL-2.0
/*
 * mca_business_votable.c
 *
 * mca business voter driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
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
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_business_voter"
#endif

struct business_votable_info {
	struct device *dev;
	struct mca_votable *awake_votable;
	struct mca_votable *smart_batt_votable;
};

static int business_awake_vote_callback(struct mca_votable *votable,
			void *data, int awake, const char *client)
{
	struct business_votable_info *vote_info = data;

	if (awake)
		pm_stay_awake(vote_info->dev);
	else
		pm_relax(vote_info->dev);

	return 0;
}

static int startegy_chg_smart_batt_vote_callback(struct mca_votable *votable,
					void *data, int value, const char *client)
{
	int ret = 0;

	mca_log_info("vote SMART_BATT = %d\n", value);
	return ret;
}

static int business_create_votable(struct business_votable_info *vote_info)
{
	int ret = 0;

	vote_info->awake_votable = mca_create_votable("AWAKE", MCA_VOTE_OR,
		business_awake_vote_callback, 0, vote_info);
	if (IS_ERR(vote_info->awake_votable)) {
		ret = PTR_ERR(vote_info->awake_votable);
		vote_info->awake_votable = NULL;
		mca_destroy_votable(vote_info->awake_votable);
		mca_log_err("%s: failed to create voter AWAKE\n", __func__);
		return ret;
	}

	vote_info->smart_batt_votable = mca_create_votable("SMART_BATT", MCA_VOTE_MIN,
		startegy_chg_smart_batt_vote_callback, 0, vote_info);
	if (IS_ERR(vote_info->smart_batt_votable)) {
		ret = PTR_ERR(vote_info->smart_batt_votable);
		vote_info->smart_batt_votable = NULL;
		mca_destroy_votable(vote_info->smart_batt_votable);
		mca_log_err("%s: failed to create voter SMART_BATT\n", __func__);
	}


	return 0;
}

int business_votable_init(struct device *dev)
{
	struct business_votable_info *vote_info;
	int ret = 0;

	mca_log_info("init start\n");
	vote_info = devm_kzalloc(dev, sizeof(*vote_info), GFP_KERNEL);
	if (!vote_info)
		return -ENOMEM;
	vote_info->dev = dev;

	ret = business_create_votable(vote_info);
	if (ret < 0) {
		mca_log_err("create votable fail\n");
		return ret;
	}

	mca_log_info("int end\n");

	return 0;
}

MODULE_DESCRIPTION("business votable");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_LICENSE("GPL v2");

