// MIUI ADD: Power_ProcessFreeze
#define pr_fmt(fmt) "millet-millet_pkg: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/types.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>
#include "millet.h"


#define MAX_CORE_PID 10
#define FREEZEBINDER_UID 111
extern int core_uid_hash_add(uid_t uid);
extern int core_uid_hash_del(uid_t uid);
extern int core_uid_hash_find(uid_t uid);
extern void core_uid_hash_clear(void);
extern int uid_rec_hash_add(uid_t uid);
extern int uid_rec_hash_del(uid_t uid);
extern int uid_rec_hash_find_and_clear(uid_t uid);
extern void uid_rec_hash_clear(void);
extern void uid_rec_hash_show(struct seq_file *m, char *sep);
extern atomic_t core_black_pid_rec[MAX_CORE_PID];
extern atomic_t core_black_uid_rec[MAX_CORE_PID];
extern int freezebinder_enable;
extern int frozen_uid_min;
extern unsigned int millet_debug;
extern int millet_sendmsg(enum MILLET_TYPE type, struct task_struct *t,
		struct millet_data *data);
extern int millet_sendto_user(struct task_struct *tsk,
		struct millet_data *data, struct millet_sock *sk);
extern int register_millet_hook(int type, recv_hook recv_from, send_hook send_to,
		init_hook init);
extern int unregister_millet_hook(int type);
extern int init_millet_subsystem(int type);



int pkg_stat_show(struct seq_file *m, void *v)
{
	uid_rec_hash_show(m, "\t");
	seq_printf(m, "\n");

	return 0;
}

static void pkg_add_uid(uid_t uid, atomic_t *rec, int max)
{
	int i, j;
	uid_t inner_uid;

	for (i = 0, j = max; i < max; i++) {
		inner_uid = atomic_read(&rec[i]);
		if (inner_uid == 0 && j == max)
			j = i;
		else if (inner_uid == uid)
			goto out;
	}

	if (j < max)
		atomic_set(&rec[j], uid);
	else
		pr_err("%s : add uid:%d failed (full)!\n", __func__, uid);

out:
	return;
}

static int pkg_sendmsg(struct task_struct *tsk,
		struct millet_data *data, struct millet_sock *sk)
{
	int ret = RET_OK;

	if (!sk || !data || !tsk) {
		pr_err("%s input invalid\n", __FUNCTION__);
		return RET_ERR;
	}

	data->msg_type = MSG_TO_USER;
	data->owner = PKG_TYPE;
	data->uid = data->mod.k_priv.pkg.pkg_owner;
	ret = millet_sendto_user(tsk, data, sk);

	return ret;
}

static inline void pkg_del_uid(atomic_t *rec, int max, uid_t uid)
{
	int i;
	uid_t inner_uid;

	for (i = 0; i < max; i++) {
		inner_uid = (uid_t) atomic_read(&rec[i]);
		if (inner_uid == uid) {
			atomic_set(&rec[i], 0);
			break;
		}
	}

	return;
}

int find_and_clear_piduid(uid_t srcuid, struct task_struct *src, uid_t dstuid)
{
	int i;
	int pid;
	int ret = 1;
	uid_t appid;

	if (srcuid == 0 || !freezebinder_enable)
		goto out;

	ret = core_uid_hash_find(dstuid);
	if (ret) {
		if (millet_debug)
			pr_err("find_and_clear_piduid dstuid srcuid=%d srcpid=%d, dstuid=%d\n",
				srcuid, src->pid, dstuid);
		goto out;
	}

	pid = src->pid;
	for (i = 0; i < MAX_CORE_PID; i++) {
		if (atomic_read(&core_black_pid_rec[i]) == pid) {
			ret = 0;
			goto out;
		}
	}

	appid = srcuid % 100000;
	if (appid >= frozen_uid_min) {
		ret = core_uid_hash_find(srcuid);
		if (ret) {
			if (millet_debug)
				pr_err("find_and_clear_piduid srcuid srcuid=%d srcpid=%d, dstuid=%d\n",
					srcuid, src->pid, dstuid);
		}
		goto out;
	}

	for (i = 0; i < MAX_CORE_PID; i++) {
		if (atomic_read(&core_black_uid_rec[i]) == appid) {
			ret = 0;
			goto out;
		}
	}

	if (millet_debug)
		pr_err("find_and_clear_piduid realuid srcuid=%d srcpid=%d, dstuid=%d\n",
			srcuid, src->pid, dstuid);
	ret = 1;
out:
	return ret;
}
EXPORT_SYMBOL_GPL(find_and_clear_piduid);

static void pkg_recv_hook(void *data, unsigned int len)
{
	struct millet_userconf *payload = (struct millet_userconf *) data;
	struct millet_data mdata;
	uid_t uid;
	int ret;

	switch (payload->mod.u_priv.pkg.cmd) {
	case ADD_UID:
		uid_rec_hash_add(payload->mod.u_priv.pkg.uid);
		break;
	case DEL_UID:
		uid_rec_hash_del(payload->mod.u_priv.pkg.uid);
		break;
	case CLEAR_ALL_UID:
		uid_rec_hash_clear();
		break;
	case ADD_CORE_UID:
		uid = payload->mod.u_priv.pkg.uid;
		if (uid == FREEZEBINDER_UID) {
			freezebinder_enable = 1;
			mdata.mod.k_priv.pkg.pkg_owner = FREEZEBINDER_UID;
			if (millet_sendmsg(PKG_TYPE, current, &mdata) < 0)
				pr_err("%s : up report failed!\n", __func__);
			pr_err("freezebinder_enable=%d : set!\n", freezebinder_enable);
		} else {
			ret = core_uid_hash_add(uid);
			if (ret < 0)
				pr_err("%s: core_uid_hash_add failed for uid %d, ret=%d\n",
					__func__, payload->mod.u_priv.pkg.uid, ret);
		}
		break;
	case DEL_CORE_UID:
		uid = payload->mod.u_priv.pkg.uid;
		if (uid == FREEZEBINDER_UID) {
			freezebinder_enable = 0;
			pr_err("freezebinder_enable=%d : set!\n", freezebinder_enable);
		} else {
			core_uid_hash_del(uid);
		}
		break;
	case ADD_CORE_BLACK_PID:
		pkg_add_uid(payload->mod.u_priv.pkg.uid, core_black_pid_rec, MAX_CORE_PID);
		break;
	case DEL_CORE_BLACK_PID:
		pkg_del_uid(core_black_pid_rec, MAX_CORE_PID, payload->mod.u_priv.pkg.uid);
		break;
	case ADD_CORE_BLACK_UID:
		pkg_add_uid(payload->mod.u_priv.pkg.uid, core_black_uid_rec, MAX_CORE_PID);
		break;
	case DEL_CORE_BLACK_UID:
		pkg_del_uid(core_black_uid_rec, MAX_CORE_PID, payload->mod.u_priv.pkg.uid);
		break;
	default:
		break;
	}

	return;
}

static void pkg_init_millet(struct millet_sock *sk)
{
	if (sk) {
		sk->mod[PKG_TYPE].monitor = PKG_TYPE;
		atomic_set(&sk->mod[PKG_TYPE].has_init, 1);
	}
}
static uid_t __sock_i_uid(struct sock *sk)
{
	uid_t uid;

	if (sk) {
		read_lock_bh(&sk->sk_callback_lock);
		uid = sk->sk_socket ? SOCK_INODE(sk->sk_socket)->i_uid.val : 0;
		read_unlock_bh(&sk->sk_callback_lock);
		return uid;
	}

	return 0;
}

static unsigned int pkg_ip4_in(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	struct sock *sk;
	struct millet_data data;
	uid_t uid;
	int found;
	int protocol;

	protocol = ip_hdr(skb)->protocol;
	if (protocol != IPPROTO_TCP)
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (sk == NULL || !sk_fullsock(sk))
		return NF_ACCEPT;

	uid = __sock_i_uid(sk);
	if (uid < UID_MIN_VALUE)
		return NF_ACCEPT;

	found = uid_rec_hash_find_and_clear(uid);
	if (!found)
		return NF_ACCEPT;
	data.mod.k_priv.pkg.owner_pid = 0;
	data.mod.k_priv.pkg.pkg_owner = (int) uid;
	if (millet_sendmsg(PKG_TYPE, current, &data) < 0)
		pr_err("%s : up report failed!\n", __func__);

	return NF_ACCEPT;
}

static unsigned int pkg_ip6_in(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	struct sock *sk;
	struct millet_data data;
	unsigned int thoff = 0;
	unsigned short frag_off = 0;
	int protohdr;
	uid_t uid;
	int found;

	protohdr = ipv6_find_hdr(skb, &thoff, -1, &frag_off, NULL);
	if (protohdr != IPPROTO_TCP)
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (sk == NULL || !sk_fullsock(sk))
		return NF_ACCEPT;

	uid = __sock_i_uid(sk);
	if (uid < UID_MIN_VALUE)
		return NF_ACCEPT;

	found = uid_rec_hash_find_and_clear(uid);
	if (!found)
		return NF_ACCEPT;

	data.mod.k_priv.pkg.owner_pid = 0;
	data.mod.k_priv.pkg.pkg_owner = (int) uid;
	if (millet_sendmsg(PKG_TYPE, current, &data) < 0)
		pr_err("%s : up report failed!\n", __func__);

	return NF_ACCEPT;
}

static inline unsigned int pkg_ip4_out(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	return NF_ACCEPT;
}

static inline unsigned int pkg_ip6_out(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
	return NF_ACCEPT;
}

static struct nf_hook_ops pkg_nf_ops[] = {

		{
				.hook        =    pkg_ip4_in,
				.pf        =    NFPROTO_IPV4,
				.hooknum    =    NF_INET_LOCAL_IN,
				.priority    =    NF_IP_PRI_SELINUX_LAST + 1,
		},
		{
				.hook   =    pkg_ip6_in,
				.pf        =    NFPROTO_IPV6,
				.hooknum    =    NF_INET_LOCAL_IN,
				.priority    =    NF_IP6_PRI_SELINUX_LAST + 1,
		},

		{
				.hook        =    pkg_ip4_out,
				.pf        =    NFPROTO_IPV4,
				.hooknum    =    NF_INET_LOCAL_OUT,
				.priority    =    NF_IP_PRI_SELINUX_LAST + 1,
		},
		{
				.hook        =    pkg_ip6_out,
				.pf        =    NFPROTO_IPV6,
				.hooknum    =    NF_INET_LOCAL_OUT,
				.priority    =    NF_IP6_PRI_SELINUX_LAST + 1,
		},

};

static int __init millet_pkg_init(void)
{
	int ret;
	struct net *net = &init_net;

	freezebinder_enable = 0;
	pr_err("enter millet_pkg_init func!\n");

	ret = nf_register_net_hooks(net, pkg_nf_ops, ARRAY_SIZE(pkg_nf_ops));
	if (ret < 0) {
		pr_err("nf_register_hooks(millet hooks) error\n");
		return RET_ERR;
	}
	pr_err("nf_register_hooks(millet hooks) success\n");
	register_millet_hook(PKG_TYPE, pkg_recv_hook, pkg_sendmsg,
			pkg_init_millet);
	init_millet_subsystem(PKG_TYPE);

	return RET_OK;
}

static void __exit millet_pkg_exit(void)
{
	struct net *net = &init_net;

	unregister_millet_hook(PKG_TYPE);
	nf_unregister_net_hooks(net, pkg_nf_ops, ARRAY_SIZE(pkg_nf_ops));
}


module_init(millet_pkg_init);
module_exit(millet_pkg_exit);

MODULE_LICENSE("GPL");
// END Power_ProcessFreeze
