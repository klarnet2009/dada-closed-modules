// SPDX-License-Identifier: GPL-2.0
/*
 * mca_qcom_panel.c
 *
 * get panel event infomation for the MCA power module (Qualcomm panel_event_notifier variant)
 *
 * Reconstructed from the stock mca_qcom_panel.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
 * Derived from the generic XRING version (mca_xring_panel.c) of the same vendor code base.
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of.h>
#include <soc/dada/panel_event_notifier.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <drm/drm_panel.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_panel"
#endif

struct mca_panel_dev {
	struct device *dev;
	struct delayed_work panel_notify_register_work;
	void *notifier_cookie;

	int screen_state;
	int hbm_state;
};

static struct mca_panel_dev *g_panel;

int mca_panel_get_screen_state(void)
{
	if (!g_panel)
		return 0;
	return g_panel->screen_state;
}
EXPORT_SYMBOL(mca_panel_get_screen_state);

int mca_panel_get_hbm_state(void)
{
	if (!g_panel)
		return 0;
	return g_panel->hbm_state;
}
EXPORT_SYMBOL(mca_panel_get_hbm_state);

static void mca_panel_event_notifier_callback(enum panel_event_notifier_tag tag,
			struct panel_event_notification *notification, void *data)
{
	struct mca_panel_dev *panel_dev = data;

	if (!notification) {
		mca_log_debug("Invalid panel notification\n");
		return;
	}

	mca_log_info("panel event received, type: %d\n", notification->notif_type);
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_BLANK:
	case DRM_PANEL_EVENT_UNBLANK:
		panel_dev->screen_state = notification->notif_type == DRM_PANEL_EVENT_BLANK ? 0 : 1;
		mca_event_block_notify(MCA_EVENT_TYPE_PANEL,
			MCA_EVENT_PANEL_SCREEN_STATE_CHANGE, &panel_dev->screen_state);
		break;
	case DRM_PANEL_EVENT_HBM_ON:
	case DRM_PANEL_EVENT_HBM_OFF:
		panel_dev->hbm_state = notification->notif_type == DRM_PANEL_EVENT_HBM_ON ? 1 : 0;
		mca_event_block_notify(MCA_EVENT_TYPE_PANEL,
			MCA_EVENT_PANEL_HBM_STATE_CHANGE, &panel_dev->hbm_state);
		break;
	default:
		mca_log_debug("Ignore panel event: %d\n", notification->notif_type);
		break;
	}
}

static int mca_panel_register_panel_notifier(struct mca_panel_dev *panel_dev)
{
	struct device_node *node;
	struct device_node *pnode;
	struct drm_panel *panel = NULL;
	void *cookie;
	int i, count, rc;

	node = of_find_node_by_name(NULL, "charge-screen");
	if (!node) {
		mca_log_err("ERROR: Cannot find node with panel!");
		return -ENODEV;
	}

	count = of_count_phandle_with_args(node, "panel", NULL);
	if (count <= 0)
		return -ENODEV;

	for (i = 0; i < count; i++) {
		pnode = of_parse_phandle(node, "panel", i);
		if (!pnode)
			return -ENODEV;
		panel = of_drm_find_panel(pnode);
		if (!IS_ERR(panel))
			break;
	}

	/*
	 * No usable panel: probe deferral is expected while the display
	 * driver is not up yet, the work is retried by the caller.
	 */
	if (IS_ERR_OR_NULL(panel)) {
		rc = PTR_ERR(panel);
		if (rc != -EPROBE_DEFER)
			mca_log_err("Failed to find active panel, rc=%d\n", rc);
		return rc;
	}


	/* register for the primary panel events as the battery charger client */
	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_BATTERY_CHARGER, panel,
			mca_panel_event_notifier_callback, panel_dev);
	if (IS_ERR(cookie)) {
		/* the cookie stays unset, so the work is retried */
		rc = PTR_ERR(cookie);
		mca_log_err("Failed to register panel event notifier, rc=%d\n", rc);
		return rc;
	}

	mca_log_err("register panel notifier successful\n");
	panel_dev->notifier_cookie = cookie;
	return 0;
}

static void mca_panel_register_panel_notifier_work(struct work_struct *work)
{
	struct mca_panel_dev *panel_dev = container_of(work, struct mca_panel_dev,
		panel_notify_register_work.work);
	static int retry_count = 3;

	mca_panel_register_panel_notifier(panel_dev);

	if (retry_count-- && !panel_dev->notifier_cookie) {
		mca_log_err("retry register panel notifier, retry_count = %d\n", retry_count);
		schedule_delayed_work(&panel_dev->panel_notify_register_work, msecs_to_jiffies(5000));
	}
}

static int mca_panel_probe(struct platform_device *pdev)
{
	struct mca_panel_dev *panel_dev;
	static int probe_cnt;
	/* probe is only called once per device, the counter is for debugging */

	mca_log_info("probe_cnt = %d\n", ++probe_cnt);

	panel_dev = devm_kzalloc(&pdev->dev, sizeof(*panel_dev), GFP_KERNEL);
	if (!panel_dev) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}
	panel_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, panel_dev);

	INIT_DELAYED_WORK(&panel_dev->panel_notify_register_work, mca_panel_register_panel_notifier_work);
	schedule_delayed_work(&panel_dev->panel_notify_register_work, msecs_to_jiffies(5000));

	/* first try 5 s after probe, panel driver may not be ready earlier */

	mca_log_err("probe OK");
	return 0;
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,mca_panel"},
	{},
};

static int mca_panel_remove(struct platform_device *pdev)
{
	struct mca_panel_dev *panel_dev = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&panel_dev->panel_notify_register_work);
	if (!IS_ERR(panel_dev->notifier_cookie))
		panel_event_notifier_unregister(panel_dev->notifier_cookie);

	devm_kfree(&pdev->dev, panel_dev);
	return 0;
}

static void mca_panel_shutdown(struct platform_device *pdev)
{
}

static struct platform_driver mca_panel_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_panel",
		.of_match_table = match_table,
	},
	.probe = mca_panel_probe,
	.remove = mca_panel_remove,
	.shutdown = mca_panel_shutdown,
};

static int __init mca_panel_init(void)
{
	return platform_driver_register(&mca_panel_driver);
}
module_init(mca_panel_init);

static void __exit mca_panel_exit(void)
{
	platform_driver_unregister(&mca_panel_driver);
}
module_exit(mca_panel_exit);

MODULE_DESCRIPTION("mca get panel event");
MODULE_AUTHOR("muxinyi1@xiaomi.com");
MODULE_LICENSE("GPL v2");
