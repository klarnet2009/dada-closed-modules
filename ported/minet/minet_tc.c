/*
 * minet: traffic-class (TC) limiter.  Reconstructed from the OS4 stock minet.ko (no public source), .
 * A list of (uid/aid, type) entries; packets of the TC interface ("wlan0") of a listed uid re-arm an IDLETIMER copy.
 */
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include "minet_debug.h"
#include "minet_nl.h"
#include "minet_tc.h"

#define TC_APP_MAX 512
#define TC_VERSION_MAX 1

struct tc_app {
	u8 type;
	u32 uid_aid;
	struct list_head head;
};

struct tc_list {
	u32 cnt;
	rwlock_t lock;
	struct list_head head;
};

struct tc_enable_msg {
	u32 version;
	u32 tc_mask;
	u32 white_mark;
	u32 black_mark;
	u32 user_offset;
	u32 app_start;
};

struct tc_list_msg {
	u8 type;
	u8 opt;
	u16 pad;
	u32 uid_aid;
};

struct tc_cfg_reply {
	u32 enable;
	u32 tc_mask;
	u32 white_mark;
	u32 black_mark;
	u32 user_offset;
	u32 app_start;
	u32 max_ver;
	u32 timeout;
	char label[MI_IDLETIMER_LABEL_SIZE];
	char iface[16];
	u8 mask[16];
};

int m_tc_enable;
static u32 m_tc_mask = 0;
static u32 m_white_mark = 0;
static u32 m_black_mark = 0;
static u32 m_user_offset = 0;
static u32 m_app_start = 0;
static struct tc_list m_tc_list;
/* ThinLTO-promoted static of the stock (hash not reproducible): non-static object with the stock symbol name */
mi_idletimer_tg_info m_idletimer_info __asm__("m_idletimer_info.llvm.6912468423217759055") __attribute__((visibility("hidden")));

static void destroy_app(struct tc_app *app)
{
	if (!app) {
		minet_err("%s NULL info", __func__);
		return;
	}
	list_del_init(&app->head);
	kfree(app);
	m_tc_list.cnt--;
}

int add_tc_app(u32 uid_aid, u8 type)
{
	struct tc_app *app;
	int ret = 0;

	write_lock_bh(&m_tc_list.lock);
	if (!list_empty(&m_tc_list.head)) {
		list_for_each_entry(app, &m_tc_list.head, head) {
			if (app->uid_aid == uid_aid) {
				app->type |= type;
				goto out;
			}
		}
	}
	if (m_tc_list.cnt >= TC_APP_MAX) {
		minet_err("%s tc list is full", __func__);
		ret = -1;
		goto out;
	}
	app = kmalloc(sizeof(*app), GFP_ATOMIC);
	if (!app) {
		minet_err("%s create new app failed", __func__);
		ret = -1;
		goto out;
	}
	memset(app, 0, sizeof(*app));
	list_add_tail(&app->head, &m_tc_list.head);
	app->uid_aid = uid_aid;
	app->type = type;
	m_tc_list.cnt++;
out:
	write_unlock_bh(&m_tc_list.lock);
	return ret;
}

static int del_tc_app(u32 uid_aid, u8 type)
{
	struct tc_app *app;
	int ret = -1;

	write_lock_bh(&m_tc_list.lock);
	if (!list_empty(&m_tc_list.head)) {
		list_for_each_entry(app, &m_tc_list.head, head) {
			if (app->uid_aid == uid_aid) {
				app->type &= ~type;
				if (!app->type)
					destroy_app(app);
				ret = 0;
				goto out;
			}
		}
	}
	minet_err("%s not found app:%u", __func__, uid_aid);
out:
	write_unlock_bh(&m_tc_list.lock);
	return ret;
}

void clear_tc_app(void) __asm__("clear_tc_app.llvm.6912468423217759055") __attribute__((visibility("hidden"), no_sanitize("kcfi")));
void clear_tc_app(void)
{
	struct tc_app *app, *tmp;

	write_lock_bh(&m_tc_list.lock);
	if (!list_empty(&m_tc_list.head)) {
		list_for_each_entry_safe(app, tmp, &m_tc_list.head, head)
			destroy_app(app);
	}
	write_unlock_bh(&m_tc_list.lock);
	minet_info("%s clear all", __func__);
}

static inline bool ifname_masked_equal(const char *a, const char *b, const u8 *mask)
{
	const unsigned long *aa = (const unsigned long *)a;
	const unsigned long *bb = (const unsigned long *)b;
	const unsigned long *mm = (const unsigned long *)mask;
	unsigned long ret = 0;
	int i;

	for (i = 0; i < IFNAMSIZ / sizeof(unsigned long); i++)
		ret |= (aa[i] ^ bb[i]) & mm[i];
	return ret == 0;
}

void tl_after_filter_in(struct sk_buff *skb, u32 uid)
{
	struct tc_app *app;
	bool hit = false;
	int enable = READ_ONCE(m_tc_enable);

	if (!skb || !enable || !skb->dev)
		return;
	if (!ifname_masked_equal(skb->dev->name, m_idletimer_info.iface, m_idletimer_info.mask))
		return;

	read_lock_bh(&m_tc_list.lock);
	if (list_empty(&m_tc_list.head)) {
		read_unlock_bh(&m_tc_list.lock);
		return;
	}
	list_for_each_entry(app, &m_tc_list.head, head) {
		if (app->uid_aid == uid)
			hit = app->type & 1;
	}
	read_unlock_bh(&m_tc_list.lock);

	if (hit)
		reset_timer(m_idletimer_info.timer, m_idletimer_info.timer->timeout, (int *)&uid);
}

int minet_set_tc_enable(void *data, int len, struct genl_info *info)
{
	struct tc_enable_msg *msg = data;
	struct { u32 cur; u32 ret; u32 rsvd; } reply;

	reply.ret = 0;
	reply.cur = READ_ONCE(m_tc_enable);
	reply.rsvd = 0;
	if (!msg) {
		minet_err("%s NULL", __func__);
		reply.ret = 3;
		goto out;
	}
	if (len != sizeof(*msg)) {
		minet_err("%s unexpected len:%d expected length:%zu", __func__, len, sizeof(*msg));
		reply.ret = 3;
		goto out;
	}
	if (msg->version > TC_VERSION_MAX) {
		minet_err("%s unsupported version:%u max version:%u", __func__, msg->version, TC_VERSION_MAX);
		reply.ret = 3;
		goto out;
	}
	if (!reply.cur && msg->version) {
		m_tc_mask = msg->tc_mask;
		m_white_mark = msg->white_mark;
		m_black_mark = msg->black_mark;
		m_user_offset = msg->user_offset;
		m_app_start = msg->app_start;
		WRITE_ONCE(m_tc_enable, 1);
		reply.cur = msg->version;
		reset_timer(m_idletimer_info.timer, m_idletimer_info.timer->timeout, NULL);
		minet_info("%s switch on", __func__);
	} else if (msg->version) {
		minet_err("%s already enable version:%u can not enable version:%u", __func__, reply.cur, msg->version);
		reply.ret = 1;
	} else if (reply.cur) {
		WRITE_ONCE(m_tc_enable, 0);
		reply.cur = msg->version;
		minet_info("%s switch off", __func__);
	} else {
		minet_info("%s already off", __func__);
	}
out:
	return minet_nl_reply(&reply, sizeof(reply), info, 7, 1);
}

int minet_set_tc_list(void *data, int len, struct genl_info *info)
{
	struct tc_list_msg *msg = data;

	if (!msg) {
		minet_err("%s NULL", __func__);
		return -1;
	}
	if (len != sizeof(*msg)) {
		minet_err("%s unexpected len:%d expected length:%zu", __func__, len, sizeof(*msg));
		return -1;
	}
	if (msg->type >= 16) {
		minet_err("%s invalid type:%d", __func__, msg->type);
		return -1;
	}
	switch (msg->opt) {
	case 1:
		return add_tc_app(msg->uid_aid, msg->type);
	case 2:
		return del_tc_app(msg->uid_aid, msg->type);
	case 3:
		clear_tc_app();
		return 0;
	default:
		minet_err("%s invalid opt:%d", __func__, msg->opt);
		return -1;
	}
}

int minet_dump_tc_list(void *data, int len, struct genl_info *info)
{
	static bool dumping;
	struct tc_cfg_reply cfg;
	struct tc_app *app, *tmp;
	u32 status;
	u8 *buf, *p;
	size_t size, room;

	if (dumping) {
		minet_info("%s already dumping", __func__);
		return 0;
	}
	dumping = true;
	minet_info("MINET TC MODULE DUMP");
	cfg.enable = READ_ONCE(m_tc_enable);
	minet_info("tc_enable:%u", cfg.enable);
	cfg.tc_mask = m_tc_mask;
	cfg.white_mark = m_white_mark;
	cfg.black_mark = m_black_mark;
	cfg.user_offset = m_user_offset;
	cfg.app_start = m_app_start;
	cfg.max_ver = TC_VERSION_MAX;
	minet_info("tc_mask:0x%0x white_mark:0x%0x black_mark:0x%0x aid_user_offset:%u aid_app_start:%u max_ver:%u",
		   cfg.tc_mask, cfg.white_mark, cfg.black_mark, cfg.user_offset, cfg.app_start, cfg.max_ver);
	cfg.timeout = m_idletimer_info.timeout;
	memcpy(cfg.label, m_idletimer_info.label, sizeof(cfg.label));
	memcpy(cfg.iface, m_idletimer_info.iface, sizeof(cfg.iface));
	memcpy(cfg.mask, m_idletimer_info.mask, sizeof(cfg.mask));
	print_idletimer_info_safe(&m_idletimer_info, NULL, 0);

	status = 2;
	read_lock_bh(&m_tc_list.lock);
	room = (size_t)m_tc_list.cnt * 5;
	size = room + 8;
	buf = kzalloc(size, GFP_ATOMIC);
	if (!buf) {
		minet_err("%s alloc app_list failed", __func__);
		size = 0;
		room = 0;
		p = NULL;
		goto cnt;
	}
	*(u32 *)buf = 1;
	*(u32 *)(buf + 4) = m_tc_list.cnt;
	p = buf + 8;
	status = 3;
cnt:
	minet_info("m_tc_list.cnt:%u", m_tc_list.cnt);
	if (!list_empty(&m_tc_list.head)) {
		list_for_each_entry_safe(app, tmp, &m_tc_list.head, head) {
			minet_info("uid_aid:%u type:0x%0x", app->uid_aid, app->type);
			if (p && room >= 4) {
				*(u32 *)p = app->uid_aid;
				p += 4;
				room -= 4;
			}
			if (p && room) {
				*p++ = app->type;
				room--;
			}
		}
	}
	read_unlock_bh(&m_tc_list.lock);
	minet_info("MINET TC MODULE DUMP modules count:%d END", status);

	if (minet_nl_reply(&cfg, sizeof(cfg), info, 7, 3))
		minet_err("%s reply config failed", __func__);
	if (buf && room == 0) {
		if (minet_nl_reply(buf, size, info, 7, 4))
			minet_err("%s reply applist failed", __func__);
	}
	if (minet_nl_reply(&status, sizeof(status), info, 7, 2))
		minet_err("%s reply applist failed", __func__);
	dumping = false;
	if (buf)
		kfree(buf);
	return 0;
}

int minet_tc_limiter_init(void)
{
	rwlock_init(&m_tc_list.lock);
	INIT_LIST_HEAD(&m_tc_list.head);

	m_idletimer_info.timeout = 3;
	strncpy(m_idletimer_info.label, "tc_high", sizeof(m_idletimer_info.label));
	m_idletimer_info.send_nl_msg = 1;
	m_idletimer_info.timer_type = 0;
	strncpy(m_idletimer_info.iface, "wlan0", sizeof(m_idletimer_info.iface));
	memset(m_idletimer_info.mask, 0xff, 6);
	if (mi_idletimer_tg_checkentry_v1(&m_idletimer_info)) {
		minet_err("%s checkentry failed", __func__);
		return -1;
	}
	return 0;
}

void minet_tc_limiter_finit(void)
{
	clear_tc_app();
	mi_idletimer_tg_destroy_v1(m_idletimer_info.timer);
}
