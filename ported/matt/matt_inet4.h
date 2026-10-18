// SPDX-License-Identifier: GPL-2.0
/* included from matt_addr.c (so that the dispatchers inline like in the stock) */
/* matt IPv4 lower layer (stock: mp_ip.c): route lookup and UDP-tunnel transmit. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include <net/route.h>
#include <net/ip.h>
#include <net/dst.h>

static inline int matt_dst_hoplimit(const struct dst_entry *dst)
{
	int hoplimit = dst_metric_raw(dst, RTAX_HOPLIMIT);

	if (hoplimit == 0)
		hoplimit = READ_ONCE(read_pnet(&dst->dev->nd_net)->ipv4.sysctl_ip_default_ttl);
	return hoplimit;
}

struct dst_entry *(*xm_sk_dst_check)(struct sock *sk, u32 cookie);

int matt_inet4_flow_route(struct sock *sk, union matt_sockaddr *da, union matt_sockaddr *sa, struct flowi *fl)
{
	struct flowi flow;
	struct flowi4 *fl4 = &flow.u.ip4;
	struct rtable *rt;

	if (xm_sk_dst_check(sk, 0)) {
#line 112
		MATT_TRACE(0, 2, "sk:%p dst existed", sk);
		return 1;
	}
	memset(&flow, 0, sizeof(flow));
	fl4->daddr = da->v4.sin_addr.s_addr;
	fl4->saddr = sa->v4.sin_addr.s_addr;
	fl4->fl4_dport = da->v4.sin_port;
	fl4->fl4_sport = sa->v4.sin_port;
#line 126
	MATT_TRACE(0, 9, "sk:%p sa:%pI4:%d da:%pI4:%d", sk, &fl4->saddr, ntohs(fl4->fl4_sport), &fl4->daddr, ntohs(fl4->fl4_dport));
	rt = ip_route_output_flow(sock_net(sk), fl4, NULL);
	if (IS_ERR(rt)) {
#line 130
		MATT_TRACE(0, 2, "sk:%p route output fail", sk);
		return PTR_ERR(rt);
	}
	if (!sa->v4.sin_family) {
		sa->v4.sin_family = AF_INET;
		sa->v4.sin_addr.s_addr = fl4->saddr;
	}
	sk_setup_caps(sk, &rt->dst);
	*fl = flow;
	return 0;
}

int matt_inet4_lower_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl, bool mgmt)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct flowi4 *fl4 = &fl->u.ip4;
	struct dst_entry *dst;
	struct rtable *rt;
	u8 tos = inet_sk(sk)->tos;
	__be16 df;

#line 230
	MATT_TRACE(0, 9, "sk:%p skb:%p len:%d tos:%d %pI4:%d -> %pI4:%d", sk, skb, skb->len, tos, &fl4->saddr,
		   ntohs(fl4->fl4_sport), &fl4->daddr, ntohs(fl4->fl4_dport));
	dst = sk_dst_get(sk);
	if (!dst) {
#line 235
		MATT_TRACE(0, 2, "cant get dst, try again");
		if (matt_flow_route2(sk, &ms->dst.addr.addr, &ms->src.addr.addr, fl)) {
#line 238
			MATT_TRACE(0, 2, "fail, no rt!");
			kfree_skb(skb);
			return -1;
		}
		dst = sk_dst_get(sk);
		if (!dst) {
#line 244
			MATT_TRACE(0, 2, "drop! cant get dst!");
			kfree_skb(skb);
			return -1;
		}
	}
	rt = container_of(dst, struct rtable, dst);
	if (skb_shinfo(skb)->gso_size)
		skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL_CSUM;
	df = (ip_dont_fragment(sk, dst) && !skb->ignore_df) ? htons(IP_DF) : 0;
	skb->inner_ipproto = IPPROTO_MATT;
	skb_reset_inner_mac_header(skb);
	skb_reset_inner_network_header(skb);
	skb_reset_inner_transport_header(skb);
	skb->encapsulation = 1;
	skb->inner_protocol_type = ENCAP_TYPE_IPPROTO;
	udp_tunnel_xmit_skb(rt, sk, skb, fl4->saddr, fl4->daddr, tos, matt_dst_hoplimit(dst), df,
			    fl4->fl4_sport, fl4->fl4_dport, false, false);
	return 0;
}
