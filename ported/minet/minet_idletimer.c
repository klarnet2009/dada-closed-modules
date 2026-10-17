/*
 * minet: in-module copy of the netfilter IDLETIMER target (ACK net/netfilter/xt_IDLETIMER.c, v1 only), sysfs removed,
 * uevent replaced by a minet netlink notification.  Reconstructed from the OS4 stock minet.ko.
 */
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>

#include "minet_debug.h"
#include "minet_tc.h"

#define NLMSG_MAX_SIZE 64

static LIST_HEAD(idletimer_tg_list);
static DEFINE_MUTEX(list_mutex);
static DEFINE_SPINLOCK(timestamp_lock);

static bool check_for_delayed_trigger(struct mi_idletimer_tg *timer, struct timespec64 *ts)
{
	int state;
	struct timespec64 temp;

	spin_lock_bh(&timestamp_lock);
	timer->work_pending = false;
	if ((ts->tv_sec - timer->last_modified_timer.tv_sec) > timer->timeout ||
	    timer->delayed_timer_trigger.tv_sec != 0) {
		state = false;
		temp.tv_sec = timer->timeout;
		temp.tv_nsec = 0;
		if (timer->delayed_timer_trigger.tv_sec != 0) {
			temp = timespec64_add(timer->delayed_timer_trigger, temp);
			ts->tv_sec = temp.tv_sec;
			ts->tv_nsec = temp.tv_nsec;
			timer->delayed_timer_trigger.tv_sec = 0;
			timer->work_pending = true;
			schedule_work(&timer->work);
		} else {
			temp = timespec64_add(timer->last_modified_timer, temp);
			ts->tv_sec = temp.tv_sec;
			ts->tv_nsec = temp.tv_nsec;
		}
	} else {
		state = timer->active;
	}
	spin_unlock_bh(&timestamp_lock);
	return state;
}

struct idletimer_nl_msg {
	char label[MI_IDLETIMER_LABEL_SIZE];
	u8 state;
	u64 time_ns;
	u32 uid;
};

static void idletimer_tg_work(struct work_struct *work)
{
	struct mi_idletimer_tg *timer = container_of(work, struct mi_idletimer_tg, work);
	char iface_msg[NLMSG_MAX_SIZE];
	char state_msg[NLMSG_MAX_SIZE];
	char timestamp_msg[NLMSG_MAX_SIZE];
	char uid_msg[NLMSG_MAX_SIZE];
	struct timespec64 ts;
	u64 time_ns;
	int state;
	int res;

	if (!timer->send_nl_msg)
		return;

	struct idletimer_nl_msg msg = {};

	res = snprintf(iface_msg, NLMSG_MAX_SIZE, "INTERFACE=%s", "wlan0");
	if (NLMSG_MAX_SIZE <= res)
		minet_err("iface_msg too long (%d)\n", res);

	ts = ktime_to_timespec64(ktime_get_boottime());
	state = check_for_delayed_trigger(timer, &ts);
	res = snprintf(state_msg, NLMSG_MAX_SIZE, "STATE=%s", state ? "active" : "inactive");
	if (NLMSG_MAX_SIZE <= res)
		minet_err("state_msg too long (%d)\n", res);

	if (state) {
		res = snprintf(uid_msg, NLMSG_MAX_SIZE, "UID=%u", timer->uid);
		if (NLMSG_MAX_SIZE <= res)
			minet_err("uid_msg too long (%d)\n", res);
	}

	time_ns = timespec64_to_ns(&ts);
	res = snprintf(timestamp_msg, NLMSG_MAX_SIZE, "TIME_NS=%llu", time_ns);
	if (NLMSG_MAX_SIZE <= res)
		minet_err("timestamp_msg too long (%d)\n", res);

	memcpy(msg.label, timer->name, sizeof(msg.label));
	msg.state = state;
	msg.time_ns = time_ns;
	msg.uid = timer->uid;
	minet_nl_msg(&msg, sizeof(msg), 0, 4, 1);
}

static void idletimer_tg_expired(struct timer_list *t)
{
	struct mi_idletimer_tg *timer = from_timer(timer, t, timer);

	spin_lock_bh(&timestamp_lock);
	timer->active = false;
	timer->work_pending = true;
	schedule_work(&timer->work);
	spin_unlock_bh(&timestamp_lock);
}

static int idletimer_resume(struct notifier_block *notifier, unsigned long pm_event, void *unused)
{
	struct timespec64 ts;
	unsigned long time_diff, now = jiffies;
	struct mi_idletimer_tg *timer = container_of(notifier, struct mi_idletimer_tg, pm_nb);

	if (!timer)
		return NOTIFY_DONE;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		timer->last_suspend_time = ktime_to_timespec64(ktime_get_boottime());
		timer->suspend_time_valid = true;
		break;
	case PM_POST_SUSPEND:
		if (!timer->suspend_time_valid)
			break;
		timer->suspend_time_valid = false;

		spin_lock_bh(&timestamp_lock);
		if (!timer->active) {
			spin_unlock_bh(&timestamp_lock);
			break;
		}
		if (time_after(timer->timer.expires, now)) {
			ts = ktime_to_timespec64(ktime_get_boottime());
			ts = timespec64_sub(ts, timer->last_suspend_time);
			time_diff = timespec64_to_jiffies(&ts);
			if (timer->timer.expires > (time_diff + now)) {
				mod_timer_pending(&timer->timer, (timer->timer.expires - time_diff));
			} else {
				del_timer(&timer->timer);
				timer->timer.expires = 0;
				timer->active = false;
				timer->work_pending = true;
				schedule_work(&timer->work);
			}
		}
		spin_unlock_bh(&timestamp_lock);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

void reset_timer(struct mi_idletimer_tg * const info_timer, const __u32 info_timeout, int *puid)
{
	unsigned long now = jiffies;
	bool timer_prev;

	spin_lock_bh(&timestamp_lock);
	timer_prev = info_timer->active;
	info_timer->active = true;
	if (!timer_prev) {
		if (puid)
			info_timer->uid = *puid;

		if (info_timer->work_pending)
			info_timer->delayed_timer_trigger = info_timer->last_modified_timer;
		else {
			info_timer->work_pending = true;
			schedule_work(&info_timer->work);
		}
	}

	info_timer->last_modified_timer = ktime_to_timespec64(ktime_get_boottime());
	mod_timer(&info_timer->timer, msecs_to_jiffies(info_timeout * 1000) + now);
	spin_unlock_bh(&timestamp_lock);
}

unsigned int mi_idletimer_tg_target_v1(int *puid, struct mi_idletimer_tg *timer)
{
	reset_timer(timer, timer->timeout, puid);
	return 0;
}

static struct mi_idletimer_tg *__idletimer_tg_find_by_label(const char *label)
{
	struct mi_idletimer_tg *entry;

	list_for_each_entry(entry, &idletimer_tg_list, entry) {
		if (!strcmp(label, entry->name))
			return entry;
	}
	return NULL;
}

int mi_idletimer_tg_checkentry_v1(mi_idletimer_tg_info *info)
{
	int ret;

	if (!info) {
		minet_err("%s NULL info", __func__);
		return -EINVAL;
	}

	/* idletimer_tg_helper() */
	if (info->timeout == 0) {
		minet_err("timeout value is zero\n");
		goto invalid;
	}
	if (info->timeout >= INT_MAX / 1000) {
		minet_err("timeout value is too big\n");
		goto invalid;
	}
	if (info->label[0] == '\0' || strnlen(info->label, MI_IDLETIMER_LABEL_SIZE) == MI_IDLETIMER_LABEL_SIZE) {
		minet_err("label is empty or not nul-terminated\n");
		goto invalid;
	}

	if (info->send_nl_msg > 1) {
		minet_err("invalid value for send_nl_msg");
		return -EINVAL;
	}

	mutex_lock(&list_mutex);

	info->timer = __idletimer_tg_find_by_label(info->label);
	if (info->timer) {
		info->timer->refcnt++;
		reset_timer(info->timer, info->timer->timeout, NULL);
	} else {
		/* idletimer_tg_create_v1() */
		info->timer = kzalloc(sizeof(*info->timer), GFP_KERNEL);
		if (!info->timer) {
			mutex_unlock(&list_mutex);
			return -ENOMEM;
		}
		strncpy(info->timer->name, info->label, MI_IDLETIMER_LABEL_SIZE);

		list_add(&info->timer->entry, &idletimer_tg_list);
		info->timer->timer_type = 0;
		info->timer->refcnt = 1;
		info->timer->send_nl_msg = true;
		info->timer->active = true;
		info->timer->timeout = info->timeout;

		info->timer->delayed_timer_trigger.tv_sec = 0;
		info->timer->delayed_timer_trigger.tv_nsec = 0;
		info->timer->work_pending = false;
		info->timer->uid = 0;
		info->timer->last_modified_timer = ktime_to_timespec64(ktime_get_boottime());

		info->timer->pm_nb.notifier_call = idletimer_resume;
		ret = register_pm_notifier(&info->timer->pm_nb);
		if (ret)
			printk(KERN_WARNING "[%s] Failed to register pm notifier %d\n", "idletimer_tg_create_v1", ret);

		INIT_WORK(&info->timer->work, idletimer_tg_work);

		timer_setup(&info->timer->timer, idletimer_tg_expired, 0);
		mod_timer(&info->timer->timer, msecs_to_jiffies(info->timeout * 1000) + jiffies);
	}

	mutex_unlock(&list_mutex);
	return 0;

invalid:
	minet_err("checkentry helper return invalid");
	return -EINVAL;
}

void mi_idletimer_tg_destroy_v1(struct mi_idletimer_tg *timer)
{
	if (!timer) {
		minet_err("%s NULL timer", __func__);
		return;
	}

	mutex_lock(&list_mutex);

	if (--timer->refcnt == 0) {
		list_del(&timer->entry);
		timer_shutdown_sync(&timer->timer);
		unregister_pm_notifier(&timer->pm_nb);
		cancel_work_sync(&timer->work);
		kfree(timer);
	}

	mutex_unlock(&list_mutex);
}

int print_idletimer_info_safe(mi_idletimer_tg_info *info, char *buf, int size)
{
	char local[148] = {};
	int len = 0, i;

	if (!info) {
		minet_info("%s null info", __func__);
		return 0;
	}
	{
		bool use_buf = (buf != NULL) & (size > 0x93);

		size = use_buf ? size : (int)sizeof(local);
		buf = use_buf ? buf : local;
	}

	len = snprintf(buf, size, "%u ", info->timeout);
	len += snprintf(buf + len, min_t(int, size - len, sizeof(info->label)), "%s", info->label);
	len += snprintf(buf + len, size - len, " ");
	len += snprintf(buf + len, size - len, "%u ", info->send_nl_msg);
	len += snprintf(buf + len, size - len, "%u ", info->timer_type);
	len += snprintf(buf + len, min_t(int, size - len, sizeof(info->iface)), "%s", info->iface);
	len += snprintf(buf + len, size - len, " ");
	len += snprintf(buf + len, size - len, "{");
	for (i = 0; i < 16; i++)
		len += snprintf(buf + len, size - len, "0x%02x ", info->mask[i]);
	len += snprintf(buf + len, size - len, "}");
	buf[(u32)(size - 1)] = '\0';
	minet_info("dump info{%s} size:%d len:%d", buf, size, len);
	return len;
}
