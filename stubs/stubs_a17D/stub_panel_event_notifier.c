// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the Qualcomm panel_event_notifier.ko exports (dada header variant). */
#include <linux/module.h>
#include <soc/dada/panel_event_notifier.h>

void *panel_event_notifier_register(enum panel_event_notifier_tag tag,
		enum panel_event_notifier_client client_handle, struct drm_panel *panel,
		panel_event_notifier_handler notif_handler, void *pvt_data) { return NULL; }
EXPORT_SYMBOL(panel_event_notifier_register);
void panel_event_notifier_unregister(void *cookie) { }
EXPORT_SYMBOL(panel_event_notifier_unregister);
void panel_event_notification_trigger(enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification) { }
EXPORT_SYMBOL(panel_event_notification_trigger);
MODULE_LICENSE("GPL");
