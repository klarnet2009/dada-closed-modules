/* minet: traffic-class limiter + in-module IDLETIMER copy (reconstructed from the OS4 stock minet.ko, ) */
#ifndef _MINET_TC_H_
#define _MINET_TC_H_

#include <linux/timer.h>
#include <linux/alarmtimer.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/suspend.h>
#include <linux/skbuff.h>
#include <net/genetlink.h>

/* stock struct (BTF `struct mi_idletimer_tg`, 376 bytes): the ACK xt_IDLETIMER `struct idletimer_tg` with the sysfs part replaced by a name buffer */
struct mi_idletimer_tg {
	struct list_head entry;
	struct alarm alarm;
	struct timer_list timer;
	struct work_struct work;
	char name[32];
	struct timespec64 delayed_timer_trigger;
	struct timespec64 last_modified_timer;
	struct timespec64 last_suspend_time;
	struct notifier_block pm_nb;
	int timeout;
	unsigned int refcnt;
	u8 timer_type;
	bool work_pending;
	bool send_nl_msg;
	bool active;
	uid_t uid;
	bool suspend_time_valid;
};

#define MI_IDLETIMER_LABEL_SIZE 28

typedef struct _mi_idletimer_tg_info {
	__u32 timeout;
	char label[MI_IDLETIMER_LABEL_SIZE];
	__u8 send_nl_msg;
	__u8 timer_type;
	char iface[16];
	__u8 mask[16];
	struct mi_idletimer_tg *timer __attribute__((aligned(8)));
} mi_idletimer_tg_info;

_Static_assert(sizeof(struct mi_idletimer_tg) == 0x178, "mi_idletimer_tg size");
_Static_assert(offsetof(struct mi_idletimer_tg, timer) == 0x90, "timer off");
_Static_assert(offsetof(struct mi_idletimer_tg, work) == 0xc8, "work off");
_Static_assert(offsetof(struct mi_idletimer_tg, pm_nb) == 0x148, "pm_nb off");
_Static_assert(offsetof(struct mi_idletimer_tg, uid) == 0x16c, "uid off");
_Static_assert(sizeof(mi_idletimer_tg_info) == 80, "info size");

/* idletimer (minet_idletimer.c) */
int mi_idletimer_tg_checkentry_v1(mi_idletimer_tg_info *info);
unsigned int mi_idletimer_tg_target_v1(int *puid, struct mi_idletimer_tg *timer);
void mi_idletimer_tg_destroy_v1(struct mi_idletimer_tg *timer);
int print_idletimer_info_safe(mi_idletimer_tg_info *info, char *buf, int size);

/* tc limiter (minet_tc.c) */
extern int m_tc_enable;
int minet_tc_limiter_init(void);
void minet_tc_limiter_finit(void);
void tl_after_filter_in(struct sk_buff *skb, u32 uid);
int add_tc_app(u32 uid_aid, u8 type);
int minet_set_tc_enable(void *data, int len, struct genl_info *info);
int minet_set_tc_list(void *data, int len, struct genl_info *info);
int minet_dump_tc_list(void *data, int len, struct genl_info *info);

/* netlink (minet_nl.c) */
int minet_nl_msg(void *data, int len, pid_t pid, u8 cmd, u8 attr);
int minet_nl_reply(void *data, int len, struct genl_info *info, u8 cmd, u8 attr);
int minet_idletimer_notify(void *data, int len);

#endif
