// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the mca_event.ko exports. */
#include <linux/module.h>
#include <mca/common/mca_event.h>

int mca_event_block_notify_register(unsigned int type, struct notifier_block *nb) { return 0; }
EXPORT_SYMBOL(mca_event_block_notify_register);
int mca_event_block_notify_unregister(unsigned int type, struct notifier_block *nb) { return 0; }
EXPORT_SYMBOL(mca_event_block_notify_unregister);
void mca_event_block_notify(unsigned int type, unsigned long event, void *data) { }
EXPORT_SYMBOL(mca_event_block_notify);
void mca_event_report_uevent(const struct mca_event_notify_data *n_data) { }
EXPORT_SYMBOL(mca_event_report_uevent);
MODULE_LICENSE("GPL");
