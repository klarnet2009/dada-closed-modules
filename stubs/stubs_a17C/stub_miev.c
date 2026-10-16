// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the miev.ko exports (the real one is the a17D port); only so modpost resolves the imports with the real CRCs. */
#include <linux/module.h>
#include <miev/mievent.h>

struct misight_mievent *cdev_tevent_alloc(unsigned int eventid) { return NULL; }
EXPORT_SYMBOL_GPL(cdev_tevent_alloc);
int cdev_tevent_add_int(struct misight_mievent *event, const char *key, long value) { return 0; }
EXPORT_SYMBOL_GPL(cdev_tevent_add_int);
int cdev_tevent_add_str(struct misight_mievent *event, const char *key, const char *value) { return 0; }
EXPORT_SYMBOL_GPL(cdev_tevent_add_str);
int cdev_tevent_write(struct misight_mievent *event) { return 0; }
EXPORT_SYMBOL_GPL(cdev_tevent_write);
void cdev_tevent_destroy(struct misight_mievent *event) { }
EXPORT_SYMBOL_GPL(cdev_tevent_destroy);
MODULE_LICENSE("GPL");
