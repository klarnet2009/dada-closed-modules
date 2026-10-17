// SPDX-License-Identifier: GPL-2.0
/*
 * miscore: Xiaomi network score module.
 * Reconstructed from the HyperOS 4.0 stock module (no public source, ).
 *
 * Netfilter hooks (LOCAL_IN / POST_ROUTING, IPv4+IPv6) account TCP/UDP traffic of
 * the foreground applications per network interface (up to 4 links); the
 * counters are pushed periodically (timer) to user space over the generic
 * netlink family "nl_miscore" and cleared afterwards.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/sysctl.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/ip.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#include "miscore.h"

int miscore_debug;
static int miscore_enable;
static struct timer_list miscore_timer;
static spinlock_t miscore_app_lock;
static struct miscore_foreground_app_info_st miscore_foreground_app_info;
static unsigned int miscore_link_num;
static spinlock_t miscore_link_lock;
static struct miscore_link_info_st miscore_link_info[MISCORE_MAX_LINK];
static struct ctl_table_header *miscore_sysctl_hdr;
static int miscore_period = 1000;

static void update_downlink_tcp_state(struct miscore_link_info_st *p_link_info, struct sock *sk,
				      struct sk_buff *skb);
void miscore_timer_func(struct timer_list *t);

unsigned int miscore_get_uid(struct sock *sk)
{
	return sk->sk_uid.val;
}

struct sock *udp_get_sock(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	struct udphdr *uh = udp_hdr(skb);
	struct udp_table *udptable;
	struct net_device *dev;
	struct net *net;
	struct sock *sk;

	if (!iph || !uh) {
		MISCORE_DBG("iph or uh is NULL\n");
		return NULL;
	}
	dev = skb->dev;
	if (!dev)
		return NULL;
	net = dev_net(dev);
	if (!net)
		return NULL;
	udptable = net->ipv4.udp_table;
	if (!udptable) {
		MISCORE_DBG("udptable is NULL\n");
		return NULL;
	}
	MISCORE_DBG("before udp_get_sock:daddr=%pI4,saddr=%pI4,proto=%u,len=%d,id=%d,sport=%u,dport=%u\n",
		    &iph->daddr, &iph->saddr, iph->protocol, skb->len, ntohs(iph->id),
		    ntohs(uh->source), ntohs(uh->dest));
	sk = __udp4_lib_lookup(net, iph->saddr, uh->source, iph->daddr, uh->dest,
			       inet_iif(skb), 0, udptable, skb);
	if (!sk)
		return NULL;
	if (!refcount_inc_not_zero(&sk->sk_refcnt)) {
		MISCORE_DBG("udp_get_sock,ref=0:daddr=%pI4,saddr=%pI4,proto=%u,len=%d,id=%d,sport=%u,dport=%u\n",
			    &iph->daddr, &iph->saddr, iph->protocol, skb->len, ntohs(iph->id),
			    ntohs(uh->source), ntohs(uh->dest));
		return NULL;
	}
	MISCORE_DBG("udp_get_sock:uid=%d,daddr=%pI4,saddr=%pI4,proto=%u,len=%d,id=%d,sport=%u,dport=%u\n",
		    sk->sk_uid.val, &iph->daddr, &iph->saddr, iph->protocol, skb->len,
		    ntohs(iph->id), ntohs(uh->source), ntohs(uh->dest));
	return sk;
}

struct sock *udp6_get_sock(struct sk_buff *skb)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct udphdr *uh = udp_hdr(skb);
	struct udp_table *udptable;
	struct net_device *dev;
	struct net *net;
	struct sock *sk;

	if (!ip6h || !uh) {
		MISCORE_DBG("ipv6h or uh is NULL\n");
		return NULL;
	}
	dev = skb->dev;
	if (!dev)
		return NULL;
	net = dev_net(dev);
	if (!net)
		return NULL;
	udptable = net->ipv4.udp_table;
	if (!udptable) {
		MISCORE_DBG("udptable is NULL\n");
		return NULL;
	}
	MISCORE_DBG("before udp6_get_sock:raddr=%pI6c,laddr=%pI6c,proto=%u,len=%d,sport=%u,dport=%u\n",
		    &ip6h->daddr, &ip6h->saddr, ip6h->nexthdr, skb->len,
		    ntohs(uh->source), ntohs(uh->dest));
	sk = __udp6_lib_lookup(net, &ip6h->saddr, uh->source, &ip6h->daddr, uh->dest,
			       IP6CB(skb)->iif, 0, udptable, skb);
	if (!sk)
		return NULL;
	if (!refcount_inc_not_zero(&sk->sk_refcnt)) {
		MISCORE_DBG("udp6_get_sock,ref=0:raddr=%pI6c,laddr=%pI6c,proto=%u,len=%d\n",
			    &ip6h->daddr, &ip6h->saddr, ip6h->nexthdr, skb->len);
		return NULL;
	}
	MISCORE_DBG("udp6_get_sock:raddr=%pI6c,laddr=%pI6c,proto=%u,len=%d\n",
		    &ip6h->daddr, &ip6h->saddr, ip6h->nexthdr, skb->len);
	return sk;
}

void update_downlink_link_stat_v4(struct miscore_link_info_st *p_link_info, struct sock *sk,
				  struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	struct tcphdr *th;
	int i;

	if (!iph)
		return;
	if (sk) {
		spin_lock_bh(&miscore_app_lock);
		for (i = 0; i < miscore_foreground_app_info.app_num; i++) {
			if (miscore_foreground_app_info.app_uids[i] == miscore_get_uid(sk)) {
				spin_unlock_bh(&miscore_app_lock);
				if (iph->protocol == IPPROTO_TCP) {
					th = tcp_hdr(skb);
					if (!th || th->fin || th->rst)
						return;
					if (skb->len == iph->ihl * 4 + th->doff * 4)
					return;
					update_downlink_tcp_state(p_link_info, sk, skb);
				} else if (iph->protocol == IPPROTO_UDP) {
					p_link_info->network_para.downlink_udp_packets++;
					p_link_info->network_para.downlink_udp_bytes += skb->len;
					MISCORE_DBG("downlink_udp_packets=%d,downlink_udp_bytes=%d\n",
						    p_link_info->network_para.downlink_udp_packets,
						    p_link_info->network_para.downlink_udp_bytes);
				}
				return;
			}
		}
		spin_unlock_bh(&miscore_app_lock);
	}
	p_link_info->network_para.downlink_background_packets++;
	p_link_info->network_para.downlink_background_bytes += skb->len;
}

static void update_downlink_tcp_state(struct miscore_link_info_st *p_link_info, struct sock *sk,
				      struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcphdr *th = tcp_hdr(skb);
	int delta;

	if (before(ntohl(th->seq), tp->rcv_nxt)) {
		p_link_info->network_para.downlink_tcp_retrans_packets++;
		p_link_info->network_para.downlink_tcp_retrans_bytes += skb->len;
		return;
	}
	if (!RB_EMPTY_ROOT(&tp->out_of_order_queue) && tp->rcv_nxt == ntohl(th->seq)) {
		delta = (int)(inet_csk(sk)->icsk_ack.lrcvtime - (u32)jiffies);
		if (delta + 50 < 0) {
			p_link_info->network_para.downlink_tcp_ooo_very_bad_packets++;
			p_link_info->network_para.downlink_tcp_lost_packets++;
			p_link_info->network_para.downlink_tcp_lost_bytes += skb->len;
			MISCORE_DBG("OOO1:seq=%u,cp_seq=%u,ack=%u,len=%d,rcv_nxt=%u,OOO1=%d,losts=%d,uid=%u\n",
				    ntohl(th->seq), tp->copied_seq, ntohl(th->ack_seq), skb->len,
				    tp->rcv_nxt, p_link_info->network_para.downlink_tcp_ooo_very_bad_packets, p_link_info->network_para.downlink_tcp_lost_packets, sk->sk_uid.val);
			return;
		}
		delta = (int)(inet_csk(sk)->icsk_ack.lrcvtime - (u32)jiffies);
		if (delta + 25 < 0) {
			p_link_info->network_para.downlink_tcp_ooo_bad_packets++;
			p_link_info->network_para.downlink_tcp_lost_packets++;
			p_link_info->network_para.downlink_tcp_lost_bytes += skb->len;
			MISCORE_DBG("OOO2:seq=%u,cp_seq=%u,ack=%u,len=%d,rcv_nxt=%u,OOO1=%d,losts=%d,uid=%u\n",
				    ntohl(th->seq), tp->copied_seq, ntohl(th->ack_seq), skb->len,
				    tp->rcv_nxt, p_link_info->network_para.downlink_tcp_ooo_bad_packets, p_link_info->network_para.downlink_tcp_lost_packets, sk->sk_uid.val);
			return;
		}
		delta = (int)(inet_csk(sk)->icsk_ack.lrcvtime - (u32)jiffies);
		if (delta + 5 < 0) {
			p_link_info->network_para.downlink_tcp_ooo_normal_packets++;
			p_link_info->network_para.downlink_tcp_lost_packets++;
			p_link_info->network_para.downlink_tcp_lost_bytes += skb->len;
			MISCORE_DBG("OOO3:seq=%u,cp_seq=%u,ack=%u,len=%d,rcv_nxt=%u,OOO1=%d,losts=%d,uid=%u\n",
				    ntohl(th->seq), tp->copied_seq, ntohl(th->ack_seq), skb->len,
				    tp->rcv_nxt, p_link_info->network_para.downlink_tcp_ooo_normal_packets, p_link_info->network_para.downlink_tcp_lost_packets, sk->sk_uid.val);
			return;
		}
	}
	p_link_info->network_para.downlink_tcp_normal_packets++;
	p_link_info->network_para.downlink_tcp_normal_bytes += skb->len;
}

void update_downlink_link_stat_v6(struct miscore_link_info_st *p_link_info, struct sock *sk,
				  struct sk_buff *skb)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct tcphdr *th;
	int i;

	if (sk) {
		spin_lock_bh(&miscore_app_lock);
		for (i = 0; i < miscore_foreground_app_info.app_num; i++) {
			if (miscore_foreground_app_info.app_uids[i] == miscore_get_uid(sk)) {
				spin_unlock_bh(&miscore_app_lock);
				if (ip6h->nexthdr == IPPROTO_TCP) {
					th = tcp_hdr(skb);
					if (!th || th->fin || th->rst)
						return;
					if (ntohs(ip6h->payload_len) <= th->doff * 4)
					return;
					update_downlink_tcp_state(p_link_info, sk, skb);
				} else if (ip6h->nexthdr == IPPROTO_UDP) {
					p_link_info->network_para.downlink_udp_packets++;
					p_link_info->network_para.downlink_udp_bytes += skb->len;
					MISCORE_DBG("v6 downlink_udp_packets=%d,downlink_udp_bytes=%d\n",
						    p_link_info->network_para.downlink_udp_packets,
						    p_link_info->network_para.downlink_udp_bytes);
				}
				return;
			}
		}
		spin_unlock_bh(&miscore_app_lock);
	}
	p_link_info->network_para.downlink_background_packets++;
	p_link_info->network_para.downlink_background_bytes += skb->len;
}

static __always_inline void update_uplink_tcp_state(struct miscore_link_info_st *p_link_info,
						    struct sock *sk, struct sk_buff *skb,
						    struct tcphdr *th, bool log)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u32 rtt, cnt __maybe_unused;

	if (before(ntohl(th->seq), tp->snd_nxt)) {
		p_link_info->network_para.uplink_tcp_retrans_packets++;
		p_link_info->network_para.uplink_tcp_retrans_bytes += skb->len;
		if (log)
			MISCORE_DBG("retrans:seq=%u,ack=%u,len=%d,snd_nxt=%u,retrans=%d,uid=%u\n",
				    ntohl(th->seq), ntohl(th->ack_seq), skb->len, tp->snd_nxt,
				    p_link_info->network_para.uplink_tcp_retrans_packets,
				    sk->sk_uid.val);
	} else {
		p_link_info->network_para.uplink_tcp_normal_packets++;
		p_link_info->network_para.uplink_tcp_normal_bytes += skb->len;
	}
	rtt = tp->srtt_us >> 3;
	if (rtt) {
		u32 avg = p_link_info->network_para.rtt;
		u32 cnt = p_link_info->network_para.rtt_count;

		if (cnt < INT_MAX) {
			p_link_info->network_para.rtt = (cnt * avg + rtt) / (cnt + 1);
			p_link_info->network_para.rtt_count = cnt + 1;
		} else {
			p_link_info->network_para.rtt_count = 1;
		}
	}
}

void update_uplink_link_stat_v4(struct miscore_link_info_st *p_link_info, struct sock *sk,
				  struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	struct tcphdr *th;
	int i;

	if (sk) {
		spin_lock_bh(&miscore_app_lock);
		for (i = 0; i < miscore_foreground_app_info.app_num; i++) {
			if (miscore_foreground_app_info.app_uids[i] == miscore_get_uid(sk)) {
				spin_unlock_bh(&miscore_app_lock);
				if (iph->protocol == IPPROTO_TCP) {
					th = tcp_hdr(skb);
					if (!th || th->fin || th->rst)
						return;
					if (skb->len == iph->ihl * 4 + th->doff * 4)
					return;
					update_uplink_tcp_state(p_link_info, sk, skb, th, true);
				} else if (iph->protocol == IPPROTO_UDP) {
					p_link_info->network_para.uplink_udp_packets++;
					p_link_info->network_para.uplink_udp_bytes += skb->len;
					MISCORE_DBG("uplink_udp_packets=%d,uplink_udp_bytes=%d\n",
						    p_link_info->network_para.uplink_udp_packets,
						    p_link_info->network_para.uplink_udp_bytes);
				}
				return;
			}
		}
		spin_unlock_bh(&miscore_app_lock);
	}
	p_link_info->network_para.uplink_background_packets++;
	p_link_info->network_para.uplink_background_bytes += skb->len;
}

void update_uplink_link_stat_v6(struct miscore_link_info_st *p_link_info, struct sock *sk,
				  struct sk_buff *skb)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct tcphdr *th;
	int i;

	if (sk) {
		spin_lock_bh(&miscore_app_lock);
		for (i = 0; i < miscore_foreground_app_info.app_num; i++) {
			if (miscore_foreground_app_info.app_uids[i] == miscore_get_uid(sk)) {
				spin_unlock_bh(&miscore_app_lock);
				if (ip6h->nexthdr == IPPROTO_TCP) {
					th = tcp_hdr(skb);
					if (!th || th->fin || th->rst)
						return;
					if (ntohs(ip6h->payload_len) <= th->doff * 4)
					return;
					update_uplink_tcp_state(p_link_info, sk, skb, th, false);
				} else if (ip6h->nexthdr == IPPROTO_UDP) {
					p_link_info->network_para.uplink_udp_packets++;
					p_link_info->network_para.uplink_udp_bytes += skb->len;
					MISCORE_DBG("v6 uplink_udp_packets=%d,uplink_udp_bytes=%d\n",
						    p_link_info->network_para.uplink_udp_packets,
						    p_link_info->network_para.uplink_udp_bytes);
				}
				return;
			}
		}
		spin_unlock_bh(&miscore_app_lock);
	}
	p_link_info->network_para.uplink_background_packets++;
	p_link_info->network_para.uplink_background_bytes += skb->len;
}

void miscore_handle_score_enable(struct nlattr *nla)
{
	if (nla->nla_len < 8)
		return;
	miscore_enable = *(int *)((char *)nla + 4);
	MISCORE_DBG("miscore_enable:%d\n", READ_ONCE(miscore_enable));
	if (READ_ONCE(miscore_enable))
		mod_timer(&miscore_timer, jiffies + msecs_to_jiffies(READ_ONCE(miscore_period)));
	else
		timer_delete(&miscore_timer);
}

void miscore_handle_foreground_app(struct nlattr *nla)
{
	int *data = (int *)((char *)nla + 4);
	int num, i;

	if (nla->nla_len < 8)
		return;
	num = data[0];
	if (num > MISCORE_MAX_APP)
		return;
	spin_lock_bh(&miscore_app_lock);
	for (i = 0; i < num; i++) {
		miscore_foreground_app_info.app_uids[i] = data[i + 1];
		MISCORE_DBG("foreground_app uid:%d, i=%d\n", data[i + 1], i);
	}
	miscore_foreground_app_info.app_num = num;
	spin_unlock_bh(&miscore_app_lock);
	MISCORE_DBG("update foreground_app finished, num=%d\n", num);
}

void miscore_handle_link_change(struct nlattr *nla)
{
	int *data = (int *)((char *)nla + 4);
	int if_index, add_flag, i, empty_index = -1;
	struct net_device *dev;

	if (nla->nla_len < 8)
		return;
	if_index = data[0];
	add_flag = data[1];
	printk(KERN_INFO "miscore:link_change,if_index=%u,add_flag=%u,link_num=%d\n",
	       if_index, add_flag, miscore_link_num);
	spin_lock_bh(&miscore_link_lock);
	if (add_flag) {
		if (miscore_link_num > MISCORE_MAX_LINK - 1)
			goto out;
		for (i = 0; i < MISCORE_MAX_LINK; i++) {
			if (miscore_link_info[i].network_para.if_index == 0) {
				if (empty_index == -1)
					empty_index = i;
			} else if (miscore_link_info[i].network_para.if_index == if_index) {
				MISCORE_DBG("if_index:%d already exist, i=%d\n", if_index, i);
				goto err;
			}
		}
		if (empty_index == -1) {
err:
			MISCORE_DBG("if_index: err:empty_index=%d\n", empty_index);
			goto out;
		}
		dev = dev_get_by_index(&init_net, if_index);
		if (!dev) {
			MISCORE_DBG("dev_get_by_index err:%d\n", if_index);
			goto out;
		}
		miscore_link_info[empty_index].network_para.if_index = if_index;
		strcpy(miscore_link_info[empty_index].if_name, dev->name);
		miscore_link_num++;
		printk(KERN_INFO "miscore:link add,if_index=%u,name=%s,num=%u,index=%u\n",
		       if_index, dev->name, miscore_link_num, empty_index);
		dev_put(dev);
	} else {
		if (miscore_link_num == 0) {
			MISCORE_DBG("miscore_link_num reach 0\n");
			goto out;
		}
		for (i = 0; i < MISCORE_MAX_LINK; i++) {
			if (miscore_link_info[i].network_para.if_index == if_index) {
				printk(KERN_INFO "miscore:link del,if_index=%u,name=%s,num=%u\n",
				       if_index, miscore_link_info[i].if_name, miscore_link_num);
				miscore_link_info[i].network_para.if_index = 0;
				miscore_link_info[i].if_name[0] = '\0';
				miscore_link_num--;
				break;
			}
		}
	}
out:
	spin_unlock_bh(&miscore_link_lock);
}

void miscore_handle_period_change(struct nlattr *nla)
{
	if (nla->nla_len < 8)
		return;
	miscore_period = *(int *)((char *)nla + 4);
}

void miscore_send_link_info(void)
{
	char buf[1024];
	char *p = buf + 4;
	unsigned int num;
	int i, len;

	memset(buf, 0, sizeof(buf));
	spin_lock_bh(&miscore_link_lock);
	num = miscore_link_num;
	if (!num) {
		spin_unlock_bh(&miscore_link_lock);
		return;
	}
	*(unsigned int *)buf = num;
	for (i = 0; i < MISCORE_MAX_LINK; i++) {
		if (miscore_link_info[i].network_para.if_index) {
			memcpy(p, &miscore_link_info[i].network_para, sizeof(struct network_para_st));
			p += sizeof(struct network_para_st);
		}
	}
	len = 4 + num * sizeof(struct network_para_st);
	spin_unlock_bh(&miscore_link_lock);
	miscore_nl_send_msg(buf, len, MISCORE_NL_MSG_LINKINFO, MISCORE_NL_ATTR_LINKINFO);
}

void miscore_refresh_link_info(void)
{
	int i;

	spin_lock_bh(&miscore_link_lock);
	if (miscore_link_num) {
		for (i = 0; i < MISCORE_MAX_LINK; i++) {
			if (miscore_link_info[i].network_para.if_index) {
				miscore_link_info[i].network_para.rtt_count = 0;
				miscore_link_info[i].network_para.uplink_tcp_normal_packets = 0;
				miscore_link_info[i].network_para.uplink_tcp_normal_bytes = 0;
				miscore_link_info[i].network_para.uplink_tcp_retrans_packets = 0;
				miscore_link_info[i].network_para.uplink_tcp_retrans_bytes = 0;
				miscore_link_info[i].network_para.uplink_udp_packets = 0;
				miscore_link_info[i].network_para.uplink_udp_bytes = 0;
				miscore_link_info[i].network_para.uplink_background_packets = 0;
				miscore_link_info[i].network_para.uplink_background_bytes = 0;
				miscore_link_info[i].network_para.downlink_tcp_normal_packets = 0;
				miscore_link_info[i].network_para.downlink_tcp_normal_bytes = 0;
				miscore_link_info[i].network_para.downlink_tcp_lost_packets = 0;
				miscore_link_info[i].network_para.downlink_tcp_lost_bytes = 0;
				miscore_link_info[i].network_para.downlink_tcp_retrans_packets = 0;
				miscore_link_info[i].network_para.downlink_tcp_retrans_bytes = 0;
				miscore_link_info[i].network_para.downlink_tcp_ooo_normal_packets = 0;
				miscore_link_info[i].network_para.downlink_tcp_ooo_bad_packets = 0;
				miscore_link_info[i].network_para.downlink_tcp_ooo_very_bad_packets = 0;
				miscore_link_info[i].network_para.downlink_udp_packets = 0;
				miscore_link_info[i].network_para.downlink_udp_bytes = 0;
				miscore_link_info[i].network_para.downlink_background_packets = 0;
				miscore_link_info[i].network_para.downlink_background_bytes = 0;
			}
		}
	}
	spin_unlock_bh(&miscore_link_lock);
}

void miscore_timer_func(struct timer_list *t)
{
	if (READ_ONCE(miscore_enable)) {
		miscore_send_link_info();
		miscore_refresh_link_info();
		if (READ_ONCE(miscore_period) > 0)
			mod_timer(&miscore_timer, jiffies + msecs_to_jiffies(READ_ONCE(miscore_period)));
	}
}

static unsigned int miscore_input_hook4(void *priv, struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	struct iphdr *iph = ip_hdr(skb);
	struct sock *sk = skb_to_full_sk(skb);
	bool ref = false;
	int i, ifindex;

	if (!READ_ONCE(miscore_enable))
		return NF_ACCEPT;
	if (!iph)
		return NF_ACCEPT;
	if (iph->protocol == IPPROTO_UDP) {
		if (sk) {
			MISCORE_DBG("udp_sk:raddr=%pI4,laddr=%pI4,proto=%u,id=%d,len=%d,state=%d\n",
				    &iph->daddr, &iph->saddr, iph->protocol, ntohs(iph->id),
				    skb->len, sk->sk_state);
		} else {
			MISCORE_DBG("udp_no_sk:raddr=%pI4,laddr=%pI4,proto=%u,id=%d,len=%d\n",
				    &iph->daddr, &iph->saddr, iph->protocol, ntohs(iph->id),
				    skb->len);
			sk = udp_get_sock(skb);
			ref = sk != NULL;
			if (!sk)
				return NF_ACCEPT;
		}
	} else if (iph->protocol != IPPROTO_TCP || !sk) {
		return NF_ACCEPT;
	}
	if (iph->protocol == IPPROTO_TCP && !sk_fullsock(sk))
		return NF_ACCEPT;
	spin_lock_bh(&miscore_link_lock);
	if (skb->dev) {
		ifindex = skb->dev->ifindex;
		for (i = 0; i < MISCORE_MAX_LINK; i++) {
			if (ifindex == miscore_link_info[i].network_para.if_index) {
				update_downlink_link_stat_v4(&miscore_link_info[i], sk, skb);
				break;
			}
		}
	} else {
		MISCORE_DBG("skb->dev is NULL\n");
	}
	spin_unlock_bh(&miscore_link_lock);
	if (ref)
		sock_put(sk);
	return NF_ACCEPT;
}

static unsigned int miscore_postrouting_hook4(void *priv, struct sk_buff *skb,
					      const struct nf_hook_state *state)
{
	struct iphdr *iph = ip_hdr(skb);
	struct sock *sk = skb_to_full_sk(skb);
	int i, ifindex;

	if (!READ_ONCE(miscore_enable))
		return NF_ACCEPT;
	if (!iph || !sk || !sk_fullsock(sk))
		return NF_ACCEPT;
	spin_lock_bh(&miscore_link_lock);
	if (skb->dev) {
		ifindex = skb->dev->ifindex;
		for (i = 0; i < MISCORE_MAX_LINK; i++) {
			if (ifindex == miscore_link_info[i].network_para.if_index) {
				update_uplink_link_stat_v4(&miscore_link_info[i], sk, skb);
				break;
			}
		}
	} else {
		MISCORE_DBG("skb->dev is NULL\n");
	}
	spin_unlock_bh(&miscore_link_lock);
	return NF_ACCEPT;
}

static unsigned int miscore_input_hook6(void *priv, struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct sock *sk = skb_to_full_sk(skb);
	bool ref = false;
	int i, ifindex;

	if (!READ_ONCE(miscore_enable))
		return NF_ACCEPT;
	if (!ip6h)
		return NF_ACCEPT;
	if (ip6h->nexthdr == IPPROTO_UDP) {
		if (sk) {
			MISCORE_DBG("udp6_sk:raddr=%pI6c,laddr=%pI6c,proto=%u,len=%d,state=%d\n",
				    &ip6h->daddr, &ip6h->saddr, ip6h->nexthdr, skb->len,
				    sk->sk_state);
		} else {
			MISCORE_DBG("udp6_no_sk:raddr=%pI6c,laddr=%pI6c,proto=%u,len=%d\n",
				    &ip6h->daddr, &ip6h->saddr, ip6h->nexthdr, skb->len);
			sk = udp6_get_sock(skb);
			ref = sk != NULL;
			if (!sk)
				return NF_ACCEPT;
		}
	} else if (ip6h->nexthdr != IPPROTO_TCP || !sk) {
		return NF_ACCEPT;
	}
	if (ip6h->nexthdr == IPPROTO_TCP && !sk_fullsock(sk))
		return NF_ACCEPT;
	spin_lock_bh(&miscore_link_lock);
	if (skb->dev) {
		ifindex = skb->dev->ifindex;
		for (i = 0; i < MISCORE_MAX_LINK; i++) {
			if (ifindex == miscore_link_info[i].network_para.if_index) {
				update_downlink_link_stat_v6(&miscore_link_info[i], sk, skb);
				break;
			}
		}
	} else {
		MISCORE_DBG("skb->dev is NULL\n");
	}
	spin_unlock_bh(&miscore_link_lock);
	if (ref)
		sock_put(sk);
	return NF_ACCEPT;
}

static unsigned int miscore_postrouting_hook6(void *priv, struct sk_buff *skb,
					      const struct nf_hook_state *state)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct sock *sk = skb_to_full_sk(skb);
	int i, ifindex;

	if (!READ_ONCE(miscore_enable))
		return NF_ACCEPT;
	if (!ip6h || !sk || !sk_fullsock(sk))
		return NF_ACCEPT;
	spin_lock_bh(&miscore_link_lock);
	if (skb->dev) {
		ifindex = skb->dev->ifindex;
		for (i = 0; i < MISCORE_MAX_LINK; i++) {
			if (ifindex == miscore_link_info[i].network_para.if_index) {
				update_uplink_link_stat_v6(&miscore_link_info[i], sk, skb);
				break;
			}
		}
	} else {
		MISCORE_DBG("skb->dev is NULL\n");
	}
	spin_unlock_bh(&miscore_link_lock);
	return NF_ACCEPT;
}

static struct nf_hook_ops miscore_hooks[] = {
	{
		.hook = miscore_input_hook4,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = 1,
	},
	{
		.hook = miscore_postrouting_hook4,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_POST_ROUTING,
		.priority = -1,
	},
	{
		.hook = miscore_input_hook6,
		.pf = NFPROTO_IPV6,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = 1,
	},
	{
		.hook = miscore_postrouting_hook6,
		.pf = NFPROTO_IPV6,
		.hooknum = NF_INET_POST_ROUTING,
		.priority = -1,
	},
};

static int proc_miscore_enable(struct ctl_table *table, int write, void *buffer,
			       size_t *lenp, loff_t *ppos)
{
	int ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (!ret && READ_ONCE(miscore_enable) && READ_ONCE(miscore_period) > 0)
		mod_timer(&miscore_timer, jiffies + msecs_to_jiffies(READ_ONCE(miscore_period)));
	return ret;
}

static struct ctl_table miscore_sysctl_table[] = {
	{
		.procname = "miscore_enable",
		.data = &miscore_enable,
		.maxlen = sizeof(int),
		.mode = 0600,
		.proc_handler = proc_miscore_enable,
	},
	{
		.procname = "miscore_debug",
		.data = &miscore_debug,
		.maxlen = sizeof(int),
		.mode = 0600,
		.proc_handler = proc_dointvec,
	},
	{ }
};

static int register_miscore_sysctl(void)
{
	if (!miscore_sysctl_hdr) {
		miscore_sysctl_hdr = register_net_sysctl(&init_net, "net/miscore", miscore_sysctl_table);
		if (!miscore_sysctl_hdr) {
			printk(KERN_INFO "miscore:register_miscore_sysctl fail!\n");
			return -1;
		}
	}
	return 0;
}

static int __init miscore_init(void)
{
	int ret;

	spin_lock_init(&miscore_app_lock);
	spin_lock_init(&miscore_link_lock);
	ret = nf_register_net_hooks(&init_net, miscore_hooks, ARRAY_SIZE(miscore_hooks));
	if (ret) {
		printk(KERN_INFO "miscore:%s register miscore_hooks err:%d\n", __func__, ret);
		return ret;
	}
	printk(KERN_INFO "miscore:nf_register_net_hooks success\n");
	ret = miscore_nl_init();
	if (ret) {
		printk(KERN_INFO "miscore:%s register miscore_nl_family err:%d\n", __func__, ret);
		nf_unregister_net_hooks(&init_net, miscore_hooks, ARRAY_SIZE(miscore_hooks));
		return ret;
	}
	printk(KERN_INFO "miscore:miscore_nl_init success\n");
	ret = register_miscore_sysctl();
	if (ret) {
		printk(KERN_INFO "miscore:%s register miscore_sysctl err:%d\n", __func__, ret);
		nf_unregister_net_hooks(&init_net, miscore_hooks, ARRAY_SIZE(miscore_hooks));
		miscore_nl_exit();
		return ret;
	}
	timer_setup(&miscore_timer, miscore_timer_func, 0);
	printk(KERN_INFO "miscore:miscore_init success\n");
	return 0;
}

static void __exit miscore_exit(void)
{
	miscore_enable = 0;
	nf_unregister_net_hooks(&init_net, miscore_hooks, ARRAY_SIZE(miscore_hooks));
	timer_delete_sync(&miscore_timer);
	miscore_nl_exit();
	if (miscore_sysctl_hdr)
		unregister_net_sysctl_table(miscore_sysctl_hdr);
}

module_init(miscore_init);
module_exit(miscore_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wei Liu <liuwei91@xiaomi.com>");
MODULE_DESCRIPTION("module for xiaomi net score");
