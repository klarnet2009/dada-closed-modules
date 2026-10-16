#include <linux/module.h>
#include <linux/notifier.h>
int register_ucsi_glink_notifier(struct notifier_block *nb) { return 0; }
EXPORT_SYMBOL(register_ucsi_glink_notifier);
int unregister_ucsi_glink_notifier(struct notifier_block *nb) { return 0; }
EXPORT_SYMBOL(unregister_ucsi_glink_notifier);
MODULE_LICENSE("GPL");
