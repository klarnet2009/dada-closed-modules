// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of the metis symbols imported by speed_touch. */
#include <linux/module.h>

atomic_t speed_touch_producer_in_boost = ATOMIC_INIT(0);
EXPORT_SYMBOL(speed_touch_producer_in_boost);
atomic_t speed_touch_consumer_in_boost = ATOMIC_INIT(0);
EXPORT_SYMBOL(speed_touch_consumer_in_boost);

bool in_interest_input_event(void)
{
	return false;
}
EXPORT_SYMBOL(in_interest_input_event);

void speed_touch_store_freq(int target)
{
}
EXPORT_SYMBOL(speed_touch_store_freq);

void speed_touch_restore_freq(int target)
{
}
EXPORT_SYMBOL(speed_touch_restore_freq);

void trigger_fboost_work(void)
{
}
EXPORT_SYMBOL(trigger_fboost_work);

MODULE_LICENSE("GPL");
