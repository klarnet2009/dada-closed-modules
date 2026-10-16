/* SPDX-License-Identifier: GPL-2.0 */
/* mca_workqueue.h - API of mca_workqueue.ko (the work queue the MCA modules share) */
#ifndef __MCA_WORKQUEUE_H__
#define __MCA_WORKQUEUE_H__

#include <linux/workqueue.h>

int mca_queue_delayed_work(struct delayed_work *dwork, unsigned long delay);
int mca_mod_delayed_work(struct delayed_work *dwork, unsigned long delay);

#endif
