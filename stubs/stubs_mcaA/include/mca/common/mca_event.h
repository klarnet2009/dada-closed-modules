/* SPDX-License-Identifier: GPL-2.0 */
/* mca_event.h - reconstructed; enumerator values are only those proven by stock code. */
#ifndef __MCA_EVENT_H__
#define __MCA_EVENT_H__

#include <linux/notifier.h>

/* only values proven by stock code are listed */
enum mca_event_type {
	MCA_EVENT_TYPE_BATTERY_INFO = 2,
	MCA_EVENT_TYPE_PANEL = 7,
};

enum mca_event_battery_info {
	MCA_EVENT_BATTERY_STS_CHANGE = 0x11,
	MCA_EVENT_BATTERY_FAKE_POWER = 0x12,
};

enum mca_event_panel {
	MCA_EVENT_PANEL_SCREEN_STATE_CHANGE = 0x40,
	MCA_EVENT_PANEL_HBM_STATE_CHANGE = 0x41,
};

#define MCA_EVENT_NOTIFY_SIZE	128

struct mca_event_notify_data {
	const char *event;
	int event_len;
};

void mca_event_report_uevent(const struct mca_event_notify_data *n_data);
int mca_event_block_notify_register(unsigned int type, struct notifier_block *nb);
int mca_event_block_notify_unregister(unsigned int type, struct notifier_block *nb);
void mca_event_block_notify(unsigned int type, unsigned long event, void *data);

#endif /* __MCA_EVENT_H__ */
