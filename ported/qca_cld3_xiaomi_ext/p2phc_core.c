// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi p2p header compression (p2phc): netfilter ingress hook + netdev tx hook around a VJ-style compressor (dlhc.c).
 * Reconstructed from the OS4 stock qca_cld3_*.ko disassembly; plain kernel headers only (the stock TU has no qdf).
 */
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/request_sock.h>
#include <linux/kthread.h>
#include "p2phc.h"

#define p2phc_info(fmt, ...) printk(KERN_INFO "p2phc-i: " fmt, ##__VA_ARGS__)
#define p2phc_err(fmt, ...) printk(KERN_ERR "p2phc-e: " fmt, ##__VA_ARGS__)

struct p2phc g_p2phc;
int sysctl_p2phc_enable;
int sysctl_p2phc_debug;
int sysctl_p2phc_interval = 10;

static unsigned int rx_inet_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state);

static struct nf_hook_ops p2phc_hooks[] = {
	{
		.hook = rx_inet_hook,
		.priv = &g_p2phc,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_NETDEV_INGRESS,
		.priority = NF_IP_PRI_FIRST,
	},
};

int p2phc_tx_netdev_hook(struct sk_buff *skb, void *unused)
{
	unsigned char buf[120];
	unsigned char *cp;
	spinlock_t *lock;
	struct iphdr *iph;
	struct tcphdr *th;
	struct dlhc *hc;
	int hlen, n;

	if (!sysctl_p2phc_enable)
		return -1;

	memset(buf, 0, sizeof(buf));
	cp = buf;
	if (strncmp(skb->dev->name, "p2p", 3) || skb->len > 1200)
		return 1;

	iph = ip_hdr(skb);
	if (!iph || iph->version != 4 || ipv4_is_multicast(iph->daddr))
		return 1;

	if (iph->protocol == IPPROTO_TCP) {
		struct sock *sk = skb->sk;

		if (sk) {
			if (sk->sk_state == TCP_NEW_SYN_RECV)
				sk = inet_reqsk(sk)->rsk_listener;
			if (sk && sk->sk_state == TCP_ESTABLISHED && unlikely(tcp_sk(sk)->rx_opt.tstamp_ok))
				tcp_sk(sk)->rx_opt.tstamp_ok = 0;
		}
		lock = &g_p2phc.tx_tcp_lock;
		th = tcp_hdr(skb);
		if (!th)
			return 1;
		hlen = iph->ihl * 4 + th->doff * 4;
	} else if (iph->protocol == IPPROTO_UDP) {
		lock = &g_p2phc.tx_udp_lock;
		hlen = iph->ihl * 4 + sizeof(struct udphdr);
	} else {
		return 1;
	}

	hc = g_p2phc.hc;
	spin_lock(lock);
	n = dlhc_compress(hc, (unsigned char *)iph, hlen, buf, &cp, 0);
	spin_unlock(lock);

	hlen -= n;
	if (!hlen) {
		unsigned char type = buf[0] & 0xf0;

		if (type == 0x90 || type == 0x70) {
			iph->version = type >> 4;
			iph->protocol = buf[9];
			ip_send_check(iph);
		}
		return 1;
	}

	memmove(skb->data + hlen, skb->data, ETH_HLEN);
	skb_pull(skb, hlen);
	skb_reset_mac_header(skb);
	memcpy((unsigned char *)iph + hlen, buf, n);
	skb->network_header += hlen;
	skb->ip_summed = CHECKSUM_NONE;
	return 1;
}

static inline unsigned int __rx_inet_hook(void *priv, struct sk_buff *skb)
{
	struct p2phc *p = priv;
	struct dlhc *hc;
	spinlock_t *lock;
	unsigned char *iph;
	unsigned char type;
	int len, ret;

	if (strncmp(skb->dev->name, "p2p", 3))
		return NF_ACCEPT;

	iph = skb_network_header(skb);
	if (!iph)
		return NF_ACCEPT;

	type = iph[0] & 0xf0;
	if (type == 0x70 || type == 0x80)
		lock = &p->rx_tcp_lock;
	else if (type == 0x90 || type == 0xa0)
		lock = &p->rx_udp_lock;
	else
		return NF_ACCEPT;

	hc = p->hc;
	if (!hc) {
		p2phc_err("%s hc rx err, type:%02x", __func__, type);
		return NF_DROP;
	}
	len = skb->len;

	if (type == 0x80 || type == 0xa0) {
		if (skb_tailroom(skb) < 120) {
			p2phc_err("%s hc uncompress err tailroom", __func__);
			return NF_DROP;
		}
		skb_put(skb, 120);
		spin_lock(lock);
		ret = dlhc_uncompress(hc, iph, len);
		spin_unlock(lock);
		if (ret <= 0) {
			p2phc_err("%s hc uncompress err [%02x %02x]", __func__, iph[0], iph[1]);
			return NF_DROP;
		}
		skb->transport_header = skb->network_header + (iph[0] & 0xf) * 4;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		return NF_ACCEPT;
	}
	if (type == 0x70 || type == 0x90) {
		spin_lock(lock);
		ret = dlhc_remember(hc, iph, len);
		spin_unlock(lock);
		if (ret > 0)
			return NF_ACCEPT;
		p2phc_err("%s hc remember err", __func__);
		return NF_DROP;
	}
	return NF_ACCEPT;
}

static unsigned int rx_inet_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	return __rx_inet_hook(priv, skb);
}

static __always_inline void p2phc_p2px_event(struct net_device *dev, unsigned long event)
{
	bool hooked = g_p2phc.hooked;
	int ret;

	if (event == NETDEV_REGISTER) {
		if (hooked)
			return;
		p2phc_hooks[0].dev = dev;
		ret = nf_register_net_hooks(&init_net, p2phc_hooks, 1);
		if (ret) {
			p2phc_err("%s nf_register_net_hooks err:%d", __func__, ret);
			return;
		}
		p2phc_info("%s nf_register_net_hooks", __func__);
		g_p2phc.hooked = true;
	} else if (event == NETDEV_UNREGISTER) {
		if (!hooked)
			return;
		nf_unregister_net_hooks(&init_net, p2phc_hooks, 1);
		g_p2phc.hooked = false;
		p2phc_info("%s nf_unregister_net_hooks", __func__);
	}
}

static int p2phc_device_event(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct dlhc *hc;

	if (strncmp(dev->name, "p2p", 3))
		return NOTIFY_DONE;

	if (event - NETDEV_REGISTER < 2) {
		p2phc_p2px_event(dev, event);
	} else {
		hc = g_p2phc.hc;
		if ((event == NETDEV_UP || event == NETDEV_CHANGE) && netif_oper_up(dev))
			dlhc_reset(hc);
	}
	return NOTIFY_DONE;
}

struct notifier_block p2phc_nb __read_mostly = {
	.notifier_call = p2phc_device_event,
};

int p2phc_init(void)
{
	int ret;

	g_p2phc.hc = dlhc_init(16, 16);
	if (IS_ERR(g_p2phc.hc)) {
		p2phc_err("%s dlhc_init err", __func__);
		ret = -1;
		goto free_hc;
	}

	spin_lock_init(&g_p2phc.rx_tcp_lock);
	spin_lock_init(&g_p2phc.tx_tcp_lock);
	spin_lock_init(&g_p2phc.rx_udp_lock);
	spin_lock_init(&g_p2phc.tx_udp_lock);
	ret = register_netdevice_notifier(&p2phc_nb);
	if (ret) {
		p2phc_err("%s register_netdevice_notifier err:%d", __func__, ret);
		unregister_netdevice_notifier(&p2phc_nb);
		goto free_hc;
	}
	p2phc_info("%s ok", __func__);
	return ret;

free_hc:
	if (g_p2phc.hc) {
		dlhc_free(g_p2phc.hc);
		g_p2phc.hc = NULL;
	}
	return ret;
}

int p2phc_cleanup(void)
{
	if (g_p2phc.thread) {
		kthread_stop(g_p2phc.thread);
		g_p2phc.thread = NULL;
		sysctl_p2phc_debug = 0;
	}
	if (g_p2phc.hc) {
		dlhc_free(g_p2phc.hc);
		g_p2phc.hc = NULL;
	}
	unregister_netdevice_notifier(&p2phc_nb);
	if (g_p2phc.hooked) {
		nf_unregister_net_hooks(&init_net, p2phc_hooks, 1);
		g_p2phc.hooked = false;
		p2phc_info("%s nf_unregister_net_hooks", __func__);
	}
	p2phc_info("%s ok", __func__);
	return 0;
}
