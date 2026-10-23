/* SPDX-License-Identifier: GPL-2.0 */
/* miev/mievent.h - Xiaomi MiSight event API (provider miev.ko); reconstructed, prototypes verified against the stock export CRCs */
#ifndef __MIEV_MIEVENT_H__
#define __MIEV_MIEVENT_H__

#include <linux/types.h>

struct misight_mievent;

struct misight_mievent *cdev_tevent_alloc(int event_code);
void cdev_tevent_add_int(struct misight_mievent *event, const char *name, int64_t value);
void cdev_tevent_add_str(struct misight_mievent *event, const char *name, const char *value);
void cdev_tevent_write(struct misight_mievent *event);
void cdev_tevent_destroy(struct misight_mievent *event);

#endif /* __MIEV_MIEVENT_H__ */
