// MIUI ADD: Power_ProcessFreeze
/*
 * Copyright (c) Xiaomi Technologies Co., Ltd. 2019. All rights reserved.
 *
 * File name: millet.c
 * Description: smart frozen control
 * Author: guchao1@xiaomi.com
 * Version: 1.0
 * Date:  2019/11/27
 */

#define pr_fmt(fmt) "millet_millet-core: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/freezer.h>
#include <net/sock.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/proc_fs.h>
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include "millet.h"

#define MAX_CORE_PID 10

/* OS4: the core-uid record is a 64-bucket uid hash table (OS3: atomic_t core_uid_rec[74]); a second identical set records pkg uids */
struct millet_uid_node {
	uid_t uid;
	struct hlist_node node;
};

#define MILLET_UID_HASH(pfx) \
static DEFINE_SPINLOCK(pfx##_hash_lock); \
static DEFINE_HASHTABLE(pfx##_hashtable, 6); \
 \
int pfx##_hash_add(uid_t uid) \
{ \
	struct millet_uid_node *e; \
	unsigned long flags; \
 \
	if (!uid) \
		return -EINVAL; \
	spin_lock_irqsave(&pfx##_hash_lock, flags); \
	hash_for_each_possible(pfx##_hashtable, e, node, uid) { \
		if (e->uid == uid) { \
			spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
			return 0; \
		} \
	} \
	spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
	e = kmalloc(sizeof(*e), GFP_KERNEL); \
	if (!e) \
		return -ENOMEM; \
	e->uid = uid; \
	spin_lock_irqsave(&pfx##_hash_lock, flags); \
	hash_add(pfx##_hashtable, &e->node, uid); \
	spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
	return 0; \
} \
 \
int pfx##_hash_del(uid_t uid) \
{ \
	struct millet_uid_node *e; \
	unsigned long flags; \
 \
	spin_lock_irqsave(&pfx##_hash_lock, flags); \
	hash_for_each_possible(pfx##_hashtable, e, node, uid) { \
		if (e->uid == uid) { \
			hash_del(&e->node); \
			spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
			kfree(e); \
			return 0; \
		} \
	} \
	spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
	return -ENOENT; \
} \
 \
int pfx##_hash_find(uid_t uid) \
{ \
	struct millet_uid_node *e; \
	unsigned long flags; \
	int found = 0; \
 \
	if (!uid) \
		return 0; \
	spin_lock_irqsave(&pfx##_hash_lock, flags); \
	hash_for_each_possible(pfx##_hashtable, e, node, uid) { \
		if (e->uid == uid) { \
			found = 1; \
			break; \
		} \
	} \
	spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
	return found; \
} \
 \
int pfx##_hash_find_and_clear(uid_t uid) \
{ \
	struct millet_uid_node *e; \
	unsigned long flags; \
 \
	if (!uid) \
		return 0; \
	spin_lock_irqsave(&pfx##_hash_lock, flags); \
	hash_for_each_possible(pfx##_hashtable, e, node, uid) { \
		if (e->uid == uid) { \
			hash_del(&e->node); \
			spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
			kfree(e); \
			return 1; \
		} \
	} \
	spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
	return 0; \
} \
 \
void pfx##_hash_clear(void) \
{ \
	struct millet_uid_node *e; \
	struct hlist_node *tmp; \
	struct hlist_head del_list = HLIST_HEAD_INIT; \
	unsigned long flags; \
	int bkt; \
 \
	spin_lock_irqsave(&pfx##_hash_lock, flags); \
	hash_for_each_safe(pfx##_hashtable, bkt, tmp, e, node) { \
		hash_del(&e->node); \
		hlist_add_head(&e->node, &del_list); \
	} \
	spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
	hlist_for_each_entry_safe(e, tmp, &del_list, node) { \
		hlist_del(&e->node); \
		kfree(e); \
	} \
} \
 \
void pfx##_hash_show(struct seq_file *m, char *sep) \
{ \
	struct millet_uid_node *e; \
	uid_t uids[128] = { 0 }; \
	unsigned long flags; \
	int cnt = 0; \
	int bkt, i; \
 \
	spin_lock_irqsave(&pfx##_hash_lock, flags); \
	hash_for_each(pfx##_hashtable, bkt, e, node) { \
		if (cnt < ARRAY_SIZE(uids)) \
			uids[cnt++] = e->uid; \
	} \
	spin_unlock_irqrestore(&pfx##_hash_lock, flags); \
	for (i = 0; i < cnt; i++) \
		seq_printf(m, "%d%s", uids[i], sep); \
}

MILLET_UID_HASH(core_uid)
EXPORT_SYMBOL_GPL(core_uid_hash_add);
EXPORT_SYMBOL_GPL(core_uid_hash_del);
EXPORT_SYMBOL_GPL(core_uid_hash_find);
EXPORT_SYMBOL_GPL(core_uid_hash_clear);
EXPORT_SYMBOL_GPL(core_uid_hash_show);
MILLET_UID_HASH(uid_rec)
EXPORT_SYMBOL_GPL(uid_rec_hash_add);
EXPORT_SYMBOL_GPL(uid_rec_hash_del);
EXPORT_SYMBOL_GPL(uid_rec_hash_find);
EXPORT_SYMBOL_GPL(uid_rec_hash_find_and_clear);
EXPORT_SYMBOL_GPL(uid_rec_hash_clear);
EXPORT_SYMBOL_GPL(uid_rec_hash_show);
atomic_t core_black_pid_rec[MAX_CORE_PID];
EXPORT_SYMBOL_GPL(core_black_pid_rec);
atomic_t core_black_uid_rec[MAX_CORE_PID];
EXPORT_SYMBOL_GPL(core_black_uid_rec);
int freezebinder_enable;
EXPORT_SYMBOL_GPL(freezebinder_enable);
int frozen_uid_min = 10000;
EXPORT_SYMBOL_GPL(frozen_uid_min);
unsigned long binder_warn_ahead_space = WARN_AHEAD_SPACE;
EXPORT_SYMBOL_GPL(binder_warn_ahead_space);
static struct millet_sock millet_sk;
struct proc_dir_entry *millet_rootdir;
unsigned int millet_debug = 0;
EXPORT_SYMBOL_GPL(millet_debug);
bool millet_binder_switch = true;
EXPORT_SYMBOL_GPL(millet_binder_switch);
static int millet_freeze_switch;
module_param(millet_debug, uint, 0644);
module_param(frozen_uid_min, uint, 0644);
module_param(binder_warn_ahead_space, ulong, 0644);
module_param(millet_freeze_switch, int, 0660);
module_param(millet_binder_switch, bool, 0644);
enum MILLET_VERSION millet_v= VERSION_1_0;

static void dump_send_msg(struct millet_data *msg)
{
	if (!millet_debug)
		return;

	pr_info("-----up-report msg-head dump-----\n");
	if (!msg) {
		pr_err("msg is NULL");
		return;
	}

	pr_info("up msg: %d\n", msg->msg_type);
	pr_info("up type: %d\n", msg->owner);
	pr_info("up src_port: 0x%lu\n", msg->src_port);
	pr_info("up dest_port: 0x%lu\n", msg->dst_port);
	pr_info("up uid: %d\n", msg->uid);
	if (msg->owner == SIG_TYPE)
		pr_info("up sig: 0x%lu\n", msg->pri[0]);
}

static void dump_recv_msg(struct millet_userconf *msg)
{
	if (!millet_debug)
		return;

	pr_info("-----recv msg from user-----\n");
	if (!msg) {
		pr_err("msg is NULL");
		return;
	}

	pr_info("msg: %d\n", msg->msg_type);
	pr_info("type: %d\n", msg->owner);
	pr_info("src_port: 0x%lu\n", msg->src_port);
	pr_info("dest_port: 0x%lu\n", msg->dst_port);
}

bool judge_millet_freeze_switch(void)
{
	return 1 == millet_freeze_switch;
}


int millet_sendto_user(struct task_struct *tsk,
		struct millet_data *data, struct millet_sock *sk)
{
	int ret, msg_len = 0;
	int monitor, monitor_port = 0;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	struct millet_data *payload = NULL;
	struct timespec64 ts;

	if (!data || !MSG_VALID(data->msg_type)) {
		pr_err("%s:msg or  msg type is invalid! %d\n",
		       __func__, data->msg_type);
		return RET_ERR;
	}

	msg_len = sizeof(struct millet_data);
	skb = nlmsg_new(msg_len, GFP_ATOMIC);
	if (!skb) {
		pr_err("%s alloc_skb failed! %d\n",
		       __func__, data->owner);
		return RET_ERR;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, msg_len, 0);
	if (!nlh) {
		kfree_skb(skb);
		return RET_ERR;
	}

	payload = nlmsg_data(nlh);
	memset(payload, 0, msg_len);
	memcpy(payload, data, msg_len);
	payload->src_port = MILLET_KERNEL_ID;
	payload->dst_port = MILLET_USER_ID;
	monitor = sk->mod[payload->owner].monitor;
	if (!TYPE_VALID(monitor))
		monitor = payload->owner;

	if (!atomic_read(&sk->mod[monitor].has_init))
		return RET_ERR;

	if (!atomic_read(&sk->mod[monitor].loop_init)) {
		if (unlikely(millet_debug))
			pr_err("monitor %d no hand shake!!", monitor);
		return RET_ERR;
	}
	payload->monitor = monitor;
	ktime_get_ts64(&ts);
	payload->tm.sec  = ts.tv_sec;
	payload->tm.nsec = ts.tv_nsec;
	monitor_port = atomic_read(&sk->mod[monitor].port);
	dump_send_msg(payload);
	ret = nlmsg_unicast(sk->sock, skb, monitor_port);

	if (ret < 0) {
		if (millet_debug)
			pr_err("nlmsg_unicast failed! %s errno %d\n",
				__func__, ret);
		return RET_ERR;
	} else {
		if (millet_debug)
			pr_info("nlmsg_unicast snd msg success to %d\n",
					monitor_port);
	}

	return RET_OK;
}
EXPORT_SYMBOL_GPL(millet_sendto_user);


int millet_sendmsg(enum MILLET_TYPE type, struct task_struct *tsk,
		struct millet_data *data)
{
	u64 walltime, timecost;
	unsigned long flags;
	int ret = RET_OK;

	if (!TYPE_VALID(type)) {
		pr_err("wrong type valid %d\n", type);
		return RET_ERR;
	}

	if (!millet_sk.mod[type].send_to) {
		pr_err("mod %d send_to interface is NULL\n", type);
		return RET_ERR;
	}

	walltime = ktime_to_us(ktime_get());
	ret = millet_sk.mod[type].send_to(tsk, data, &millet_sk);

	if (!atomic_read(&millet_sk.mod[type].has_init))
                return ret;

	spin_lock_irqsave(&millet_sk.mod[type].lock, flags);

	if (ret < 0) {
		millet_sk.mod[type].stat.send_fail++;
	} else if (ret > 0)
		millet_sk.mod[type].stat.send_suc++;

	timecost = ktime_to_us(ktime_get()) - walltime;
	millet_sk.mod[type].stat.runtime += timecost;
	spin_unlock_irqrestore(&millet_sk.mod[type].lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(millet_sendmsg);


static void recv_handler(struct sk_buff *skb)
{
	struct millet_userconf *payload = NULL;
	struct nlmsghdr *nlh = NULL;
	unsigned int msglen = sizeof(struct millet_userconf);
	uid_t uid = 0;
	int from = -1;

	if (!skb) {
		pr_err("recv_handler %s: skb is	NULL!\n", __func__);
		return;
	}

	uid = (*NETLINK_CREDS(skb)).uid.val;
	if (uid > 1000) {
		pr_err("uid: %d, permission denied\n", uid);
		return;
	}

	if (millet_debug)
		pr_info("kernel millet receive msg now\n");

	if (unlikely(skb->len < NLMSG_SPACE(0))) {
		pr_err("msg len is invalid\n");
		return;
	}

	nlh = nlmsg_hdr(skb);
	if (nlh->nlmsg_len < NLMSG_SPACE(msglen)) {
		pr_err("msg len err %d need %d\n",
		       nlh->nlmsg_len, NLMSG_SPACE(msglen));
		return;
	}

	from = nlh->nlmsg_pid;
	payload = (struct millet_userconf *) NLMSG_DATA(nlh);
	if (payload->src_port != MILLET_USER_ID) {
		if (unlikely(millet_debug)) {
			pr_err("src_port %lu invalid! from %d need len %d len %d\n",
		       		payload->src_port,
				from,
				nlh->nlmsg_len,
				NLMSG_SPACE(msglen));
			pr_err("-------invalid msg dump------");
			pr_err("**current pid %d****\n", current->pid);
		}
		dump_recv_msg(payload);
		return;
	}

	if (payload->dst_port != MILLET_KERNEL_ID) {
		pr_err("dst_port is %lu not kernel!\n",
		       payload->dst_port);
		return;
	}

	if (!TYPE_VALID(payload->owner)) {
		pr_err("mod %d is not valid!\n",
		       payload->owner);
		return;
	}

	switch (payload->msg_type) {
	case LOOPBACK_MSG: {
		struct millet_data data;
		data.msg_type = LOOPBACK_MSG;
		data.owner = payload->owner;
		atomic_set(&millet_sk.mod[payload->owner].port, from);
		atomic_set(&millet_sk.mod[payload->owner].loop_init, 1);
		dump_recv_msg(payload);
		millet_sendto_user(current, &data, &millet_sk);
		break;
	}

	case MSG_TO_KERN: {
		if (millet_sk.mod[payload->owner].recv_from)
			millet_sk.mod[payload->owner].recv_from(
				payload, sizeof(struct millet_userconf));

		if (millet_debug)
			dump_recv_msg(payload);
		break;
	}

	default:
		pr_err("msg type is valid %d\n", payload->msg_type);
		break;
	}
}

static int millet_sock_show(struct seq_file *m, void *v)
{
	int i, val;
	unsigned long flags;
	u64 send_suc[MILLET_TYPES_NUM];
	u64 send_fail[MILLET_TYPES_NUM];
	u64 runtime[MILLET_TYPES_NUM];
	u64 total_send_suc, total_send_fail, total_runtime;

	memset(send_suc, 0, sizeof(u64) * MILLET_TYPES_NUM);
	memset(send_fail, 0, sizeof(u64) * MILLET_TYPES_NUM);
	memset(runtime, 0, sizeof(u64) * MILLET_TYPES_NUM);

	for (i = O_TYPE + 1; i < MILLET_TYPES_NUM; i++) {
		spin_lock_irqsave(&millet_sk.mod[i].lock, flags);
		send_suc[i] = millet_sk.mod[i].stat.send_suc;
		send_fail[i] = millet_sk.mod[i].stat.send_fail;
		runtime[i] = millet_sk.mod[i].stat.runtime;
		spin_unlock_irqrestore(&millet_sk.mod[i].lock, flags);
	}

	total_send_suc = total_send_fail = total_runtime = 0;
	seq_printf(m, "-----------------------------\n\n");
	for (i = O_TYPE + 1; i < MILLET_TYPES_NUM; i++) {
		if (!send_suc[i] && !send_fail[i])
			continue;

		total_send_suc += send_suc[i];
		total_send_fail += send_fail[i];
		total_runtime += runtime[i];

		seq_printf(m, "name: %s mod id %d:\n",
		           millet_sk.mod[i].name, i);
		seq_printf(m, "send suc: %llu\n", send_suc[i]);
		seq_printf(m, "send fail: %llu\n", send_fail[i]);
		seq_printf(m, "runtime: %llu us\n\n", runtime[i]);

	}

	seq_printf(m, "---------total info--------------------\n");
	seq_printf(m, "send_suc: %llu\n", total_send_suc);
	seq_printf(m, "send_fail: %llu\n", total_send_fail);
	seq_printf(m, "runtime: %llu us\n", total_runtime);
	seq_printf(m, "core_uid_rec:\n");
	core_uid_hash_show(m, ",");
	seq_printf(m, "\nuid_rec:\n");
	uid_rec_hash_show(m, "\t");
	seq_printf(m, "\ncore_black_pid_rec:\n");
	for (i = 0; i < MAX_CORE_PID; i++) {
		val = atomic_read(&core_black_pid_rec[i]);
		if (val)
			seq_printf(m, "%d,", val);
	}
	seq_printf(m, "\ncore_black_uid_rec:\n");
	for (i = 0; i < MAX_CORE_PID; i++) {
		val = atomic_read(&core_black_uid_rec[i]);
		if (val)
			seq_printf(m, "%d,", val);
	}
	return 0;
}

static int millet_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, millet_sock_show, NULL);
}

static void stat_reset(void)
{
	int i;
	unsigned long flags;

	for (i = O_TYPE + 1; i < MILLET_TYPES_NUM; i++) {
		spin_lock_irqsave(&millet_sk.mod[i].lock, flags);
		millet_sk.mod[i].stat.send_suc = 0;
		millet_sk.mod[i].stat.send_fail = 0;
		millet_sk.mod[i].stat.runtime = 0;
		spin_unlock_irqrestore(&millet_sk.mod[i].lock, flags);
	}
}

static ssize_t millet_stat_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *f_ops)
{
	unsigned char tmp = 0;
	int value = 0;

	get_user(tmp, buf);
	value = simple_strtol(&tmp, NULL, 10);
	pr_info("input value number: %d\n", value);
	if (value == 1)
		stat_reset();

	printk(KERN_WARNING "stat reset now\n");
	return count;
}

static const struct proc_ops millet_proc_fops = {
	.proc_open   = millet_stat_open,
	.proc_read   = seq_read,
	.proc_write   = millet_stat_write,
	.proc_lseek   = seq_lseek,
	.proc_release   = single_release,
};

static int millet_version_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", millet_v);
	return 0;
}

static int millet_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, millet_version_show, NULL);
}

static const struct proc_ops millet_version_fops = {
	.proc_open   = millet_version_open,
	.proc_read   = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release   = single_release,
};

int register_millet_hook(int type, recv_hook recv_from,
		send_hook send_to, init_hook init)
{
	if (!TYPE_VALID(type)) {
		pr_err("%s: type is invalid! %d\n",
				__func__, type);
		return RET_ERR;
	}

	if (millet_sk.mod[type].init) {
		pr_err("%s: init has been registered %d\n",
		       __func__, type);
		return RET_ERR;
	} else
		millet_sk.mod[type].init = init;

	if (millet_sk.mod[type].recv_from) {
		pr_err("%s: recv_from has been registered %d\n",
		       __func__, type);
		return RET_ERR;
	} else
		millet_sk.mod[type].recv_from = recv_from;

	if (millet_sk.mod[type].send_to) {
		pr_err("%s: send_to has been registered %d\n",
		       __func__, type);
		return RET_ERR;
	} else
		millet_sk.mod[type].send_to = send_to;

	if (millet_debug) {
		printk("type %d register hook\n", type);
		dump_stack();
	}

	return RET_OK;
}
EXPORT_SYMBOL_GPL(register_millet_hook);


int unregister_millet_hook(int type)
{
	if (!TYPE_VALID(type)) {
		pr_err("%s: type is invalid! %d\n",
		       __func__, type);
		return RET_ERR;
	}

	millet_sk.mod[type].recv_from = NULL;
	millet_sk.mod[type].send_to = NULL;
	millet_sk.mod[type].init = NULL;
	return RET_OK;
}
EXPORT_SYMBOL_GPL(unregister_millet_hook);

int init_millet_subsystem(int type)
{
	if (!TYPE_VALID(type)) {
		pr_err("%s: type is invalid! %d\n",
				__func__, type);
		return RET_ERR;
	}

	atomic_set(&millet_sk.mod[type].port, 0);
	atomic_set(&millet_sk.mod[type].loop_init, 0);
	spin_lock_init(&millet_sk.mod[type].lock);
	if (millet_sk.mod[type].init)
		millet_sk.mod[type].init(&millet_sk);

	strlcpy(millet_sk.mod[type].name, NAME_ARRAY[type], NAME_MAXLEN);
	return RET_OK;
}
EXPORT_SYMBOL_GPL(init_millet_subsystem);


static int __init millet_init(void)
{
	int ret = RET_ERR;
	struct proc_dir_entry *millet_stat_entry = NULL;
	struct proc_dir_entry *millet_version_entry = NULL;

	struct netlink_kernel_cfg cfg = {
		.input = recv_handler,
	};

	hash_init(core_uid_hashtable);
	hash_init(uid_rec_hashtable);
	pr_err("enter millet_init func!\n");
	millet_sk.sock =
		netlink_kernel_create(&init_net, NETLINK_MILLET, &cfg);
	if (!millet_sk.sock) {
		pr_err("%s: create socket error!\n", __func__);
		return ret;
	}

	millet_rootdir = proc_mkdir("millet", NULL);
	if (!millet_rootdir)
		pr_err("create /proc/millet failed\n");
	else {
		millet_stat_entry = proc_create("millet_stat",
			0644, millet_rootdir, &millet_proc_fops);
		if (!millet_stat_entry)
			pr_err("create millet stat failed\n");

		millet_version_entry = proc_create("version",
			0644, millet_rootdir, &millet_version_fops);
		if (!millet_version_entry)
			pr_err("create millet version failed\n");
	}

	return RET_OK;
}

static void __exit millet_exit(void)
{
	if (millet_rootdir) {
		remove_proc_entry("millet_stat", millet_rootdir);
		remove_proc_entry("version", millet_rootdir);
		remove_proc_entry("millet", NULL);
	}
	if (millet_sk.sock) {
		netlink_kernel_release(millet_sk.sock);
		millet_sk.sock = NULL;
	}
	uid_rec_hash_clear();
	core_uid_hash_clear();
}

late_initcall(millet_init);
module_exit(millet_exit);

MODULE_DESCRIPTION("Millet smart frozen control subsystem");
MODULE_LICENSE("GPL");
// END Power_ProcessFreeze
