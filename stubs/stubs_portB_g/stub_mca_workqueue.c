// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_workqueue (only the export CRCs matter). */
#include <linux/module.h>
#include <mca/common/mca_workqueue.h>

int mca_queue_delayed_work(struct delayed_work *dwork, unsigned long delay) { return 0; }
EXPORT_SYMBOL(mca_queue_delayed_work);
int mca_mod_delayed_work(struct delayed_work *dwork, unsigned long delay) { return 0; }
EXPORT_SYMBOL(mca_mod_delayed_work);
