/*
 * minet: xiaomi network engine.
 */
#include <linux/ip.h>
#include <net/ipv6.h>
#include <uapi/linux/netfilter_ipv4.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack.h>

#include "minet_debug.h"
#include "minet_nl.h"

#define DRV_NAME "minet"
#define DRV_VERSION "1.0"

typedef struct _flow_id {
	u8 protocol;
	u16 family;
	u16 dest_port;
	u16 src_port;
	union {
		struct in6_addr ip6_addr;
		__be32 ip4_addr;
	} dest_addr;
	union {
		struct in6_addr ip6_addr;
		__be32 ip4_addr;
	} src_addr;
} flow_id;

typedef struct _flow_stat {
	u32 in_pkt;
	u32 in_len;
	u32 out_pkt;
	u32 out_len;
} flow_stat;

/* a += b */
#define FLOW_STAT_ADD(a, b) do { \
		(a)->in_pkt += (b)->in_pkt; \
		(a)->in_len += (b)->in_len; \
		(a)->out_pkt += (b)->out_pkt; \
		(a)->out_len += (b)->out_len; \
	} while(0);

typedef struct _flow_info {
	flow_id id;
	flow_stat stat;
	struct list_head head;
} flow_info;

typedef struct _flow_head {
	u32 cnt;
	spinlock_t lock;
	struct list_head head;
} flow_head;

typedef struct _app_info {
	u32 pid;
	u32 uid;
	flow_head flow;
	struct list_head head;
} app_info;

typedef struct _app_head {
	u32 cnt;
	spinlock_t lock;
	struct list_head head;
} app_head;

typedef struct _minet_ctx {
	spinlock_t lock;
	struct task_struct *task;
	app_head app[CONFIG_NR_CPUS];
} minet_ctx;

enum {
	PKT_DIR_IN = 1,
	PKT_DIR_OUT
};

/**
 * data struct for netlink
*/
#define NL_SET_APP			1
#define NL_NOTIFY_APP_FLOW	1

typedef struct _set_app {
	u16 type;

	u16 opt; // 1:add; 2:del; 3:clear;
	u32 cnt;
	u32 uid[];
} set_app;

typedef struct _app_flow_info {
	flow_id id;
	flow_stat stat;
} app_flow_info;

typedef struct _notify_flow {
	u16 type;
	
	u32 uid;
	u16 cnt;
	app_flow_info afi[];
} notify_flow;

minet_ctx *g_minet = NULL;

flow_info *add_new_flow(flow_head *flow)
{
	flow_info *fi;

	if (flow == NULL) {
		minet_err("%s NULL", __func__);
		return NULL;
	}

	fi = kmalloc(sizeof(flow_info), GFP_ATOMIC);
	if (fi == NULL)
		return fi;
	memset(fi, 0, sizeof(flow_info));
	list_add_tail(&fi->head, &flow->head);
	flow->cnt ++;

	return fi;
}

static bool __flow_equal(flow_id *id1, flow_id *id2)
{
	if (id1->family != id2->family)
		return false;
	if (id1->protocol != id2->protocol)
		return false;
	if (/*id1->src_port != id2->src_port || */id1->dest_port != id2->dest_port)
		return false;
	if (id1->family == PF_INET)
		return (id1->dest_addr.ip4_addr == id2->dest_addr.ip4_addr);
	else
		return ipv6_addr_equal(&id1->dest_addr.ip6_addr, &id2->dest_addr.ip6_addr);
}

flow_info *match_flow(flow_head *flow, flow_id *id)
{
	struct list_head *p = NULL;
	struct list_head *n = NULL;
	flow_info *pos = NULL;
	flow_info *fi = NULL;

	list_for_each_safe(p, n, &flow->head) {
		pos = list_entry(p, flow_info, head);
		if(__flow_equal(&pos->id, id)) {
			fi = pos;
			break;
		}
	}

	return fi;
}

app_info *add_new_app(app_head *app)
{
	app_info *ai;

	if (app == NULL) {
		minet_err("%s NULL", __func__);
		return NULL;
	}

	ai = kmalloc(sizeof(app_info), GFP_ATOMIC);
	if (ai == NULL)
		return ai;
	memset(ai, 0, sizeof(app_info));
	spin_lock_init(&ai->flow.lock);
	INIT_LIST_HEAD(&ai->flow.head);
	list_add_tail(&ai->head, &app->head);
	app->cnt ++;

	return ai;
}

app_info *match_app(app_head *app, u32 uid)
{
	struct list_head *p = NULL;
	struct list_head *n = NULL;
	app_info *pos = NULL;
	app_info *ai = NULL;

	list_for_each_safe(p, n, &app->head) {
		pos = list_entry(p, app_info, head);
		if(pos->uid == uid) {
			ai = pos;
			break;
		}
	}

	return ai;
}

#if 0
int flush_flow(flow_head *flow)
{
	struct list_head *p = NULL;
	struct list_head *n = NULL;
	flow_info *pos = NULL;

	spin_lock_bh(&flow->lock);
	list_for_each_safe(p, n, &flow->head) {
		pos = list_entry(p, flow_info, head);
		list_del_init(p);
		flow->cnt --;
		/* handle flow_info */
		if (pos->id.family == AF_INET) {
			minet_info("    flow_info: AF_NET  protocol:%d src:"IPv4_FMT":%d dst:"IPv4_FMT":%d",
				pos->id.protocol, 
				IPv4_ARG(&pos->id.src_addr.ip4_addr), pos->id.src_port,
				IPv4_ARG(&pos->id.dest_addr.ip4_addr), pos->id.dest_port);
		} else if (pos->id.family == AF_INET6) {
			// TODO
			minet_info("    flow_info: AF_NET6 protocol:%d src:%04X:%d dst:%04X:%d",
				pos->id.protocol, 
				pos->id.src_addr.ip4_addr, pos->id.src_port,
				pos->id.dest_addr.ip4_addr, pos->id.dest_port);
		}
		minet_info("               in:%d(%d) out:%d(%d)",
			pos->stat.in_pkt, pos->stat.in_len, pos->stat.out_pkt, pos->stat.out_len);
		kfree(pos);
	}
	spin_unlock_bh(&flow->lock);

	return 0;
}

int flush_app(app_head *app)
{
	struct list_head *p = NULL;
	struct list_head *n = NULL;
	app_info *pos = NULL;

	spin_lock_bh(&app->lock);
	list_for_each_safe(p, n, &app->head) {
		pos = list_entry(p, app_info, head);
		list_del_init(p);
		app->cnt --;
		/* handle app_info */
		minet_info("app_info: pid:%d uid:%d", pos->pid, pos->uid);
		flush_flow(&pos->flow);
		kfree(pos);
	}
	spin_unlock_bh(&app->lock);

	return 0;
}
#endif

static inline app_head *new_app_head(void)
{
	app_head *app = kmalloc(sizeof(app_head), GFP_KERNEL);
	if (app == NULL)
		return NULL;
	memset(app, 0, sizeof(app_head));

	spin_lock_init(&app->lock);
	INIT_LIST_HEAD(&app->head);

	return app;
}

static inline int free_app_head(app_head *app)
{
	if (app)
		kfree(app);
	return 0;
}

int merge_one_app_flow(int uid, flow_id *fid, flow_stat *stat, app_head *app)
{
	struct list_head *p = NULL;
	struct list_head *n = NULL;
	app_info *ai = NULL;
	flow_head *flow = NULL;
	flow_info *fi = NULL;

	/**
	 * find app by uid.
	*/
	spin_lock_bh(&app->lock);
	list_for_each_safe(p, n, &app->head) {
		ai = list_entry(p, app_info, head);
		if (ai->uid == uid) {
			flow = &ai->flow;
			break;
		}
	}

	if (flow == NULL) {
		ai = add_new_app(app);
		if (ai) {
			ai->uid = uid;
			app->cnt ++;
		}
		flow = &ai->flow;
	}
	spin_unlock_bh(&app->lock);

	/**
	 *  find flow by flow_id, and add stat to flow.
	*/
	spin_lock_bh(&flow->lock);
	list_for_each_safe(p, n, &flow->head) {
		fi = list_entry(p, flow_info, head);
		if (__flow_equal(&fi->id, fid)) {
			FLOW_STAT_ADD(&(fi->stat), stat);
			break;
		}
		fi = NULL;
	}

	if (fi == NULL) {
		fi = add_new_flow(flow);
		if (fi) {
			fi->id = *fid;
			fi->stat = *stat;
			flow->cnt ++;
		}
	}
	spin_unlock_bh(&flow->lock);

	return 0;
}

app_head *merge_app_flow(void)
{
	struct list_head *p = NULL, *p1 = NULL;
	struct list_head *n = NULL, *n1 = NULL;
	int cpu;
	app_head *app = NULL, *merge_app = NULL;
	app_info *ai = NULL;
	flow_head *flow = NULL;
	flow_info *fi = NULL;

	merge_app = new_app_head();
	if (merge_app == NULL) {
		minet_dbg("%s err.", __func__);
		return NULL;
	}

	for (cpu = 0; cpu < CONFIG_NR_CPUS; cpu ++) {
		app = &g_minet->app[cpu];
		if (list_empty(&app->head))
			continue;
		spin_lock_bh(&app->lock);
		list_for_each_safe(p, n, &app->head) {
			ai = list_entry(p, app_info, head);
			//list_del_init(p);
			//app->cnt --;

			flow = &ai->flow;
			if (list_empty(&flow->head))
				continue;
			spin_lock_bh(&flow->lock);
			list_for_each_safe(p1, n1, &flow->head) {
				fi = list_entry(p1, flow_info, head);
				list_del_init(p1);
				flow->cnt --;
				merge_one_app_flow(ai->uid, &fi->id, &fi->stat, merge_app);
				kfree(fi);
			}
			spin_unlock_bh(&flow->lock);

			//kfree(ai);
		}
		spin_unlock_bh(&app->lock);
	}

	return merge_app;
}

int minet_notify_app_flow(app_head *app)
{
	struct list_head *p = NULL, *p1 = NULL;
	struct list_head *n = NULL, *n1 = NULL;
	notify_flow *nf = NULL;
	app_info *ai = NULL;
	flow_head *flow = NULL;
	flow_info *fi = NULL;

	if (app == NULL) {
		minet_err("%s NULL", __func__);
		return -1;
	}

	spin_lock_bh(&app->lock);
	list_for_each_safe(p, n, &app->head) {
		ai = list_entry(p, app_info, head);
		list_del_init(p);

		flow = &ai->flow;
		nf = kmalloc(sizeof(notify_flow) + (flow->cnt * sizeof(app_flow_info))
			, GFP_ATOMIC);
		if (nf) {
			nf->type = NL_NOTIFY_APP_FLOW;
			nf->uid = ai->uid;
			nf->cnt = 0;
		} else {
			minet_err("%s nf NULL", __func__);
		}

		spin_lock_bh(&flow->lock);
		list_for_each_safe(p1, n1, &flow->head) {
			fi = list_entry(p1, flow_info, head);
			list_del_init(p1);

			if (nf) {
				nf->afi[nf->cnt].id = fi->id;
				nf->afi[nf->cnt].stat = fi->stat;
#if 1 /* flow_info  */
				if (fi->id.family == PF_INET) {
					minet_dbg("flow: p:%d "IPv4_FMT":%d <--> "IPv4_FMT":%d",
						fi->id.protocol, 
						IPv4_ARG(&fi->id.src_addr.ip4_addr), fi->id.src_port,
						IPv4_ARG(&fi->id.dest_addr.ip4_addr), fi->id.dest_port);
				} else if (fi->id.family == PF_INET6) {
					minet_dbg("flow: p:%d "IPv6_FMT":%d <--> "IPv6_FMT":%d",
						fi->id.protocol,
						IPv6_ARG(fi->id.src_addr.ip6_addr), fi->id.src_port,
						IPv6_ARG(fi->id.dest_addr.ip6_addr), fi->id.dest_port);
				}
					minet_dbg("      in:%d(%d) out:%d(%d)",
						fi->stat.in_pkt, fi->stat.in_len,
						fi->stat.out_pkt, fi->stat.out_len);
#endif
				nf->cnt ++;
			}

			kfree(fi);
		}
		spin_unlock_bh(&flow->lock);

		kfree(ai);
		if (nf) {
			minet_dbg("%s uid:%d, cnt:%d", __func__, nf->uid, nf->cnt);
			minet_nl_notify(nf,
				sizeof(notify_flow) + (nf->cnt * sizeof(app_flow_info)));
			kfree(nf);
		}
	}
	spin_unlock_bh(&app->lock);

	free_app_head(app);
	return 0;
}

static bool is_sk_fullsock(struct sock *sk)
{
	if (sk != NULL && sk_fullsock(sk))
		return true;

	return false;
}

static bool is_tcp_udp_sock(struct sock *sk)
{
	if (!is_sk_fullsock(sk))
		return false;

	return (sk->sk_type == SOCK_STREAM || sk->sk_type == SOCK_DGRAM);
}

static bool is_local_or_lan_addr(__be32 ip_addr)
{
	if (ipv4_is_loopback(ip_addr) ||
		ipv4_is_multicast(ip_addr) ||
		ipv4_is_local_multicast(ip_addr) ||
		ipv4_is_lbcast(ip_addr) ||
		ipv4_is_private_10(ip_addr) ||
		ipv4_is_private_172(ip_addr) ||
		ipv4_is_private_192(ip_addr) ||
		ipv4_is_linklocal_169(ip_addr))
		return true;
	else
		return false;
}

inline u32 get_sock_uid(struct sock *sk)
{
	return (u32)sk->sk_uid.val;
}

static int get_flow_id(struct sk_buff *skb, u8 dir, flow_id *id)
{
	struct iphdr *iph = NULL;
	struct ipv6hdr *iph6 = NULL;
	struct tcphdr *tcph = NULL;

	if (id->family == PF_INET) {
		iph = ip_hdr(skb);
		if (iph == NULL)
			return -1;
		tcph = tcp_hdr(skb);
		if (tcph == NULL)
			return -1;
		id->protocol = iph->protocol;
		if (dir == PKT_DIR_IN) { /* ingress */
			id->dest_addr.ip4_addr = iph->saddr;
			id->src_addr.ip4_addr = iph->daddr;
			id->dest_port = ntohs(tcph->source);
			id->src_port = ntohs(tcph->dest);
		} else {
			id->dest_addr.ip4_addr = iph->daddr;
			id->src_addr.ip4_addr = iph->saddr;
			id->dest_port = ntohs(tcph->dest);
			id->src_port = ntohs(tcph->source);
		}
	} else if (id->family == PF_INET6) {
		iph6 = ipv6_hdr(skb);
		if (iph6 == NULL)
			return -1;
		tcph = tcp_hdr(skb);
		if (tcph == NULL)
			return -1;
		id->protocol = iph6->nexthdr;
		if (dir == PKT_DIR_IN) { /* ingress */
			id->dest_addr.ip6_addr = iph6->saddr;
			id->src_addr.ip6_addr = iph6->daddr;
			id->dest_port = ntohs(tcph->source);
			id->src_port = ntohs(tcph->dest);
		} else {
			id->dest_addr.ip6_addr = iph6->daddr;
			id->src_addr.ip6_addr = iph6->saddr;
			id->dest_port = ntohs(tcph->dest);
			id->src_port = ntohs(tcph->source);
		}
	}
	return 0;
}

static void app_flow_stat(app_info *ai, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	flow_head *flow = &ai->flow;
	flow_info *fi = NULL;
	flow_id id;
	u8 dir = 0;

	if (state->hook == NF_INET_LOCAL_IN) {
		dir = PKT_DIR_IN;
	} else if (state->hook == NF_INET_LOCAL_OUT) {
		dir = PKT_DIR_OUT;
	}

	id.family = state->pf;
	get_flow_id(skb, dir, &id);
	spin_lock_bh(&flow->lock);
	fi = match_flow(flow, &id);
	if (NULL == fi) {
		fi = add_new_flow(flow);
		if (NULL == fi)
			goto end;
		fi->id = id;
	}
	if (state->hook == NF_INET_LOCAL_IN) {
		fi->stat.in_pkt ++;
		fi->stat.in_len += skb->len;
	} else if (state->hook == NF_INET_LOCAL_OUT) {
		fi->stat.out_pkt ++;
		fi->stat.out_len += skb->len;
	}
end:
	spin_unlock_bh(&flow->lock);
}

static inline int save_connect_uid(struct sk_buff *skb, u32 uid)
{
	struct nf_conn *nfc = NULL;
	enum ip_conntrack_info ctinfo;

	nfc = nf_ct_get(skb, &ctinfo);
	if (nfc == NULL) {
		return -1;
	}

	if (ctinfo == IP_CT_NEW) {
		nfc->android_oem_data1 = uid;
	}

	return 0;
}

static inline int get_connect_uid_by_sock(struct sk_buff *skb, u32 *puid)
{
	u32 uid = 0xffffffff;
	struct sock *sk = NULL;

	sk = skb_to_full_sk(skb);
	if (sk == NULL)
		return -1;

	if (!is_tcp_udp_sock(sk))
		return -1;

	uid = get_sock_uid(sk);
	*puid = uid;
	return 0;
}

static inline int get_connect_uid(struct sk_buff *skb, u32 *puid)
{
	u32 uid = 0xffffffff;
	int ret = 0;
	struct nf_conn *nfc = NULL;
	enum ip_conntrack_info ctinfo;

	ret = get_connect_uid_by_sock(skb, puid);
	if (ret >= 0)
		return ret;

	nfc = nf_ct_get(skb, &ctinfo);
	if (nfc == NULL) {
		return -1;
	}

	if (ctinfo == IP_CT_IS_REPLY || ctinfo == IP_CT_ESTABLISHED) {
		uid = nfc->android_oem_data1;
	}

	if (uid == 0xffffffff)
		return -1;

	*puid = uid;
	return 0;
}

static unsigned int hook4(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	const struct iphdr *iph = NULL;
	unsigned int cpu;
	app_head *app = NULL;
	app_info *ai = NULL;
	u32 uid = 0xffffffff;
	int ret = 0;

	iph = ip_hdr(skb);
	if (iph == NULL)
		return NF_ACCEPT;

	if (iph->protocol != IPPROTO_TCP && iph->protocol != IPPROTO_UDP)
		return NF_ACCEPT;

	ret = get_connect_uid(skb, &uid);
	if (ret < 0) {
		return NF_ACCEPT;
	}

	if (state->hook == NF_INET_LOCAL_OUT) {
		save_connect_uid(skb, uid);
	}

	/* match addr */
	if (state->hook == NF_INET_LOCAL_OUT && is_local_or_lan_addr(iph->daddr))
		return NF_ACCEPT;
	if (state->hook == NF_INET_LOCAL_IN && is_local_or_lan_addr(iph->saddr))
		return NF_ACCEPT;

	/* get per-CPU app */
	cpu = get_cpu();
	if (cpu < 0 || cpu >= CONFIG_NR_CPUS)
		goto put_cpu;
	app = &g_minet->app[cpu];

	/* match app */
	spin_lock_bh(&app->lock);
	ai = match_app(app, uid);
	if (NULL == ai) {
		spin_unlock_bh(&app->lock);
		goto put_cpu;
	}
	app_flow_stat(ai, skb, state);
	spin_unlock_bh(&app->lock);

put_cpu:
	put_cpu();
	return NF_ACCEPT;
}

static unsigned int hook6(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	const struct ipv6hdr *iph = NULL;
	unsigned int cpu;
	app_head *app = NULL;
	app_info *ai = NULL;
	u32 uid = 0xffffffff;
	int ret = 0;

	iph = ipv6_hdr(skb);
	if (iph == NULL)
		return NF_ACCEPT;

	if (iph->nexthdr != IPPROTO_TCP && iph->nexthdr != IPPROTO_UDP)
		return NF_ACCEPT;

	ret = get_connect_uid(skb, &uid);
	if (ret < 0) {
		return NF_ACCEPT;
	}

	if (state->hook == NF_INET_LOCAL_OUT) {
		save_connect_uid(skb, uid);
	}

	/* match addr */
	/*if (is_private_v6_addr(sk))
		return NF_ACCEPT;*/

	/* get per-CPU app */
	cpu = get_cpu();
	if (cpu < 0 || cpu >= CONFIG_NR_CPUS)
		goto put_cpu;
	app = &g_minet->app[cpu];

	/* match app */
	spin_lock_bh(&app->lock);
	ai = match_app(app, uid);
	if (NULL == ai) {
		spin_unlock_bh(&app->lock);
		goto put_cpu;
	}
	app_flow_stat(ai, skb, state);
	spin_unlock_bh(&app->lock);

put_cpu:
	put_cpu();
	return NF_ACCEPT;
}

static struct nf_hook_ops minet_hooks[] = {
	{
		.hook = hook4,
		.pf = PF_INET,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = NF_IP_PRI_FILTER + 1,
	},
	{
		.hook = hook4,
		.pf = PF_INET,
		.hooknum = NF_INET_LOCAL_OUT,
		.priority = NF_IP_PRI_FILTER - 1,
	},
	{
		.hook = hook6,
		.pf = PF_INET6,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = NF_IP_PRI_FILTER + 1,
	},
	{
		.hook = hook6,
		.pf = PF_INET6,
		.hooknum = NF_INET_LOCAL_OUT,
		.priority = NF_IP_PRI_FILTER - 1,
	},
};

int add_app(int uid)
{
	int cpu;
	app_head *app = NULL;
	app_info *ai = NULL;

	for (cpu = 0; cpu < CONFIG_NR_CPUS; cpu ++) {
		app = &g_minet->app[cpu];
		spin_lock_bh(&app->lock);
		ai = add_new_app(app);
		if (ai) {
			ai->uid = uid;
			//minet_dbg("cpu[%d] add app:%d", cpu, uid);
		}
		spin_unlock_bh(&app->lock);
	}
	return 0;
}

int del_app(int uid)
{
	struct list_head *p = NULL, *p1 = NULL;
	struct list_head *n = NULL, *n1 = NULL;
	int cpu;
	app_head *app = NULL;
	app_info *ai = NULL;
	flow_head *flow = NULL;
	flow_info *fi = NULL;

	for (cpu = 0; cpu < CONFIG_NR_CPUS; cpu ++) {
		app = &g_minet->app[cpu];
		if (list_empty(&app->head))
			continue;
		spin_lock_bh(&app->lock);
		list_for_each_safe(p, n, &app->head) {
			ai = list_entry(p, app_info, head);
			if (ai->uid != uid)
				continue;
			list_del_init(p);
			app->cnt --;

			flow = &ai->flow;
			if (list_empty(&flow->head)) {
				kfree(ai);
				continue;
			}
			spin_lock_bh(&flow->lock);
			list_for_each_safe(p1, n1, &flow->head) {
				fi = list_entry(p1, flow_info, head);
				list_del_init(p1);
				flow->cnt --;
				kfree(fi);
			}
			spin_unlock_bh(&flow->lock);

			kfree(ai);
		}
		spin_unlock_bh(&app->lock);
	}

	return 0;
}

int clear_app(void)
{
	struct list_head *p = NULL, *p1 = NULL;
	struct list_head *n = NULL, *n1 = NULL;
	int cpu;
	app_head *app = NULL;
	app_info *ai = NULL;
	flow_head *flow = NULL;
	flow_info *fi = NULL;

	for (cpu = 0; cpu < CONFIG_NR_CPUS; cpu ++) {
		app = &g_minet->app[cpu];
		if (list_empty(&app->head))
			continue;
		spin_lock_bh(&app->lock);
		list_for_each_safe(p, n, &app->head) {
			ai = list_entry(p, app_info, head);
			list_del_init(p);
			app->cnt --;

			flow = &ai->flow;
			if (list_empty(&flow->head)) {
				kfree(ai);
				continue;
			}
			spin_lock_bh(&flow->lock);
			list_for_each_safe(p1, n1, &flow->head) {
				fi = list_entry(p1, flow_info, head);
				list_del_init(p1);
				flow->cnt --;
				kfree(fi);
			}
			spin_unlock_bh(&flow->lock);

			kfree(ai);
		}
		spin_unlock_bh(&app->lock);
	}

	return 0;
}

int minet_set_app(void *arg)
{
	set_app *sa = NULL;
	int i;

	if (arg == NULL) {
		minet_err("%s NULL", __func__);
		return -1;
	}

	sa = (set_app *)arg;
	if (sa->type != NL_SET_APP) {
		minet_err("%s type:%d", __func__, sa->type);
		return -1;
	}

	if (sa->opt == 1) {
		for (i = 0; i < sa->cnt; i ++) {
			add_app(sa->uid[i]);
			minet_dbg("%s add %d", __func__, sa->uid[i]);
		}
	} else if (sa->opt == 2) {
		for (i = 0; i < sa->cnt; i ++) {
			del_app(sa->uid[i]);
			minet_dbg("%s del %d", __func__, sa->uid[i]);
		}
	} else if (sa->opt == 3) {
		clear_app();
		minet_dbg("%s clear", __func__);
	}

	return 0;
}

static int minet_thread(void *data)
{
	minet_dbg("%s", __func__);
	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(HZ * 5);
		//minet_dbg("%s run[cpu%d]", __func__, smp_processor_id());

		//flush_app(merge_app_flow());
		minet_notify_app_flow(merge_app_flow());
	}
	return 0;
}

static int __init minet_init_module(void)
{
	int cpu, ret = 0;
	app_head *app = NULL;

	g_minet = kmalloc(sizeof(minet_ctx), GFP_KERNEL);
	if (g_minet == NULL)
		return -1;
	memset(g_minet, 0, sizeof(minet_ctx));

	spin_lock_init(&g_minet->lock);
	for (cpu = 0; cpu < CONFIG_NR_CPUS; cpu ++) {
		app = &g_minet->app[cpu];
		spin_lock_init(&app->lock);
		INIT_LIST_HEAD(&app->head);
	}

	ret = nf_register_net_hooks(&init_net, minet_hooks,
				    ARRAY_SIZE(minet_hooks));
	if (ret) {
		minet_err("%s nf_register_net_hooks err:%d", __func__, ret);
		goto init_err;
	}

	g_minet->task = kthread_run(minet_thread, g_minet, "minet_thread");
	if (IS_ERR_OR_NULL(g_minet->task)) {
		minet_err("%s kthread_run err", __func__);
		goto init_err;
	}

	ret = minet_nl_init();
	if (ret)
		goto init_err;
	minet_dbg("%s ok", __func__);
	return ret;

init_err:
	if (g_minet != NULL)
		kfree(g_minet);
	g_minet = NULL;
	return -1;
}

static void __exit minet_cleanup_module(void)
{
	minet_dbg("%s", __func__);

	nf_unregister_net_hooks(&init_net, minet_hooks,
				    ARRAY_SIZE(minet_hooks));

	if (g_minet->task) {
		kthread_stop(g_minet->task);
		g_minet->task = NULL;
	}

	clear_app();

	if (g_minet) {
		kfree(g_minet);
		g_minet = NULL;
	}

	minet_nl_finit();
	minet_dbg("%s ok", __func__);
}

static int minet_uid = 1;
module_param(minet_uid, int, 00400);
MODULE_PARM_DESC(minet_uid, "app uid for minet to monitor");

module_init(minet_init_module);
module_exit(minet_cleanup_module);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Pengtao He <hepengtao@xiaomi.com>");
MODULE_DESCRIPTION("driver for minet");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
MODULE_VERSION(DRV_VERSION);