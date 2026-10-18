// SPDX-License-Identifier: GPL-2.0
/* included from matt_addr.c: IPv6 lower layer (stock: mp_ip.c): route lookup and UDP-tunnel transmit. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include <net/ip6_route.h>
#include <net/ip6_checksum.h>
#include <net/ipv6.h>
#include <net/ip6_tunnel.h>
#include <net/addrconf.h>
#include <net/transp_v6.h>
#include <net/ip6_fib.h>

/* kernel symbols resolved at init through the kprobe kallsyms lookup */
struct ip6_flowlabel *(*xm__fl6_sock_lookup)(struct sock *sk, __be32 label);
struct dst_entry *(*xm_ip6_dst_lookup_flow)(struct net *net, const struct sock *sk, struct flowi6 *fl6, const struct in6_addr *final_dst);
u32 (*xm__get_hash_from_flowi6)(const struct flowi6 *fl6, struct flow_keys *keys);

int matt_inet6_flow_route(struct sock *sk, union matt_sockaddr *da, union matt_sockaddr *sa, struct flowi *fl)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi flow;
	struct flowi6 *fl6 = &flow.u.ip6;
	struct dst_entry *dst;

	if (xm_sk_dst_check(sk, np->dst_cookie)) {
#line 162
		MATT_TRACE(0, 9, "sk:%p dst existed", sk);
		return 1;
	}
	memset(&flow, 0, sizeof(flow));
	fl6->daddr = da->v6.sin6_addr;
	fl6->saddr = sa->v6.sin6_addr;
	fl6->fl6_dport = da->v6.sin6_port;
	fl6->fl6_sport = sa->v6.sin6_port;
#line 176
	MATT_TRACE(0, 9, "sk:%p sa: %pI6c:%d -> da: %pI6c:%d", sk, &fl6->saddr, ntohs(fl6->fl6_sport), &fl6->daddr, ntohs(fl6->fl6_dport));
	if (np->sndflow) {
		fl6->flowlabel = da->v6.sin6_flowinfo & IPV6_FLOWINFO_MASK;
		if ((fl6->flowlabel & IPV6_FLOWLABEL_MASK) && READ_ONCE(sock_net(sk)->ipv6.flowlabel_has_excl)) {
			struct ip6_flowlabel *flowlabel = xm__fl6_sock_lookup(sk, fl6->flowlabel);

			if (IS_ERR_OR_NULL(flowlabel))
				return -EINVAL;
			fl6_sock_release(flowlabel);
		}
	}
	dst = xm_ip6_dst_lookup_flow(sock_net(sk), sk, fl6, NULL);
	if (IS_ERR(dst)) {
#line 194
		MATT_TRACE(0, 9, "sk:%p route output fail", sk);
		return PTR_ERR(dst);
	}
	if (!sa->v6.sin6_family) {
		sa->v6.sin6_family = AF_INET6;
		sa->v6.sin6_addr = fl6->saddr;
	}
	ip6_dst_store(sk, dst, NULL, NULL);
	*fl = flow;
	return 0;
}

int matt_inet6_lower_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl, bool mgmt)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct matt_sock *ms = MATT_SK(sk);
	struct flowi6 *fl6 = &fl->u.ip6;
	struct dst_entry *dst;
	u8 tclass = np->tclass;
	__be32 label;

#line 335
	MATT_TRACE(0, 9, "sk:%p skb:%p len:%d %pI6c:%d -> %pI6c:%d", sk, skb, skb->len, &fl6->saddr, ntohs(fl6->fl6_sport),
		   &fl6->daddr, ntohs(fl6->fl6_dport));
	dst = sk_dst_get(sk);
	if (!dst) {
#line 340
		MATT_TRACE(0, 2, "cant get dst, try again");
		if (matt_flow_route2(sk, &ms->dst.addr.addr, &ms->src.addr.addr, fl)) {
#line 343
			MATT_TRACE(0, 2, "fail, no rt!");
			kfree_skb(skb);
			return -1;
		}
		dst = sk_dst_get(sk);
		if (!dst) {
#line 349
			MATT_TRACE(0, 2, "drop! cant get dst!");
			kfree_skb(skb);
			return -1;
		}
	}
	skb->inner_ipproto = IPPROTO_MATT;
	skb_reset_inner_mac_header(skb);
	skb_reset_inner_network_header(skb);
	skb_reset_inner_transport_header(skb);
	skb->encapsulation = 1;
	skb->inner_protocol_type = ENCAP_TYPE_IPPROTO;
	label = fl6->flowlabel & IPV6_FLOWLABEL_MASK;
	if (!label) {
		struct net *net = sock_net(sk);

		if (net->ipv6.sysctl.auto_flowlabels != IP6_AUTO_FLOW_LABEL_OFF) {
			u32 hash;

			if (!skb->l4_hash && !skb->sw_hash) {
				struct flow_keys keys;
				u32 h = xm__get_hash_from_flowi6(fl6, &keys);

				__skb_set_sw_hash(skb, h, flow_keys_have_l4(&keys));
			}
			hash = rol32(skb->hash, 16);
			label = (__force __be32)hash & IPV6_FLOWLABEL_MASK;
			if (net->ipv6.sysctl.flowlabel_state_ranges)
				label |= IPV6_FLOWLABEL_STATELESS_FLAG;
		}
	}
	udp_tunnel6_xmit_skb(dst, sk, skb, NULL, &fl6->saddr, &fl6->daddr, tclass, ip6_dst_hoplimit(dst), label,
			     fl6->fl6_sport, fl6->fl6_dport, false);
	return 0;
}
