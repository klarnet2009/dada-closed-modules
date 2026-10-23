// SPDX-License-Identifier: GPL-2.0
/* matt address-family glue (stock: mp_socket.c/mp_offload.c area): dispatchers *2 and the inet4/inet6 ops. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"
#include <net/ipv6.h>

extern int sysctl_inet_enable;

static struct matt_addr_family_ops matt_af_inet = {
	.iph_len = 20,
	.is_any_addr = matt_inet4_is_any_addr,
	.udp_conf_init = matt_inet4_udp_conf_init,
	.flow_route = matt_inet4_flow_route,
	.lower_xmit = matt_inet4_lower_xmit,
	.get_skb_addrs = matt_inet4_get_skb_addrs,
};
static struct matt_addr_family_ops matt_af_inet6 = {
	.iph_len = 40,
	.is_any_addr = matt_inet6_is_any_addr,
	.udp_conf_init = matt_inet6_udp_conf_init,
	.flow_route = matt_inet6_flow_route,
	.lower_xmit = matt_inet6_lower_xmit,
	.get_skb_addrs = matt_inet6_get_skb_addrs,
};
static struct matt_proto_family_ops matt_pf_inet = {
	.get_user_addr = matt_inet4_get_user_addr,
	.cmp_sk_addr = matt_inet4_cmp_sk_addr,
	.get_sk_addr = matt_inet4_get_sk_addr,
	.set_sk_addr = matt_inet4_set_sk_addr,
};
static struct matt_proto_family_ops matt_pf_inet6 = {
	.get_user_addr = matt_inet6_get_user_addr,
	.cmp_sk_addr = matt_inet6_cmp_sk_addr,
	.get_sk_addr = matt_inet6_get_sk_addr,
	.set_sk_addr = matt_inet6_set_sk_addr,
};
static struct matt_addr_family_ops *const matt_afs[2] = { &matt_af_inet, &matt_af_inet6 };
static struct matt_proto_family_ops *const matt_pfs[2] = { &matt_pf_inet, &matt_pf_inet6 };

/* ---- dispatchers (matt_afs / matt_pfs indexed by "is v6") ---- */
u32 matt_encap_len2(union matt_sockaddr *a)
{
	return matt_afs[a->sa.sa_family == AF_INET6]->iph_len + 8;
}

u32 matt_encap_lower_header_len2(union matt_sockaddr *a)
{
	return matt_afs[a->sa.sa_family == AF_INET6]->iph_len + 0x98;
}

int matt_is_any_addr2(union matt_sockaddr *a)
{
	return matt_afs[a->sa.sa_family == AF_INET6]->is_any_addr(a);
}

void matt_udp_conf_init2(struct sock *sk, struct udp_port_cfg *conf, union matt_sockaddr *a)
{
	matt_afs[a->sa.sa_family == AF_INET6]->udp_conf_init(sk, conf, a);
}

int matt_flow_route2(struct sock *sk, union matt_sockaddr *da, union matt_sockaddr *sa, struct flowi *fl)
{
	return matt_afs[da->sa.sa_family == AF_INET6]->flow_route(sk, da, sa, fl);
}

int matt_lower_xmit2(struct sock *sk, struct sk_buff *skb, union matt_sockaddr *da, struct flowi *fl, bool mgmt)
{
	return matt_afs[da->sa.sa_family == AF_INET6]->lower_xmit(sk, skb, fl, mgmt);
}

void matt_get_skb_addrs2(struct sk_buff *skb, union matt_sockaddr *da, union matt_sockaddr *sa)
{
	memset(sa, 0, sizeof(*sa));
	memset(da, 0, sizeof(*da));
	matt_afs[(skb_network_header(skb)[0] & 0xf0) == 0x60]->get_skb_addrs(skb, da, sa);
}

int matt_get_user_addr2(struct sock *sk, union matt_sockaddr *a, struct sockaddr *addr, int addr_len)
{
	memset(a, 0, sizeof(*a));
	return matt_pfs[sk->sk_family == AF_INET6]->get_user_addr(sk, a, addr, addr_len);
}

bool matt_cmp_sk_addr2(struct sock *sk, union matt_sockaddr *a, union matt_sockaddr *addr)
{
	return matt_pfs[sk->sk_family == AF_INET6]->cmp_sk_addr(sk, a, addr);
}

int matt_get_sk_addr2(struct socket *sock, struct sockaddr *a, int peer)
{
	return matt_pfs[sock->sk->sk_family == AF_INET6]->get_sk_addr(sock, a, peer);
}

void matt_set_sk_addr2(struct sock *sk, union matt_sockaddr *a, bool src)
{
	matt_pfs[sk->sk_family == AF_INET6]->set_sk_addr(sk, a, src);
}

/* ---- IPv4 ---- */
int matt_inet4_is_any_addr(union matt_sockaddr *a)
{
	return a->v4.sin_addr.s_addr == 0;
}

void matt_inet4_udp_conf_init(struct sock *sk, struct udp_port_cfg *conf, union matt_sockaddr *a)
{
	conf->family = AF_INET;
	conf->local_ip.s_addr = a->v4.sin_addr.s_addr;
	conf->local_udp_port = a->v4.sin_port;
	conf->use_udp6_rx_checksums = 1;
#line 87
	MATT_TRACE(0, 4, "v4");
}

void matt_inet4_get_skb_addrs(struct sk_buff *skb, union matt_sockaddr *da, union matt_sockaddr *sa)
{
	struct udphdr *uh = (struct udphdr *)(skb->head + MATT_SKB_CB(skb)->udph_offset);

	sa->v4.sin_family = AF_INET;
	sa->v4.sin_port = uh->source;
	sa->v4.sin_addr.s_addr = ip_hdr(skb)->saddr;
	da->v4.sin_family = AF_INET;
	da->v4.sin_port = uh->dest;
	da->v4.sin_addr.s_addr = ip_hdr(skb)->daddr;
}

int matt_inet4_get_user_addr(struct sock *sk, union matt_sockaddr *a, struct sockaddr *addr, int addr_len)
{
	if ((unsigned int)addr_len < 16 || addr->sa_family != sk->sk_family)
		return 1;
	memcpy(a, addr, 16);
	return 0;
}

bool matt_inet4_cmp_sk_addr(struct sock *sk, union matt_sockaddr *a, union matt_sockaddr *addr)
{
	if (a->v4.sin_port != addr->v4.sin_port)
		return false;
	if (a->v4.sin_family != addr->v4.sin_family)
		return false;
	if (!a->v4.sin_addr.s_addr)
		return true;
	return !addr->v4.sin_addr.s_addr || a->v4.sin_addr.s_addr == addr->v4.sin_addr.s_addr;
}

int matt_inet4_get_sk_addr(struct socket *sock, struct sockaddr *uaddr, int peer)
{
	struct sock *sk = sock->sk;
	struct sockaddr_in *sin = (struct sockaddr_in *)uaddr;
	const char *tag;
	__be32 addr;
	__be16 port;

	sin->sin_family = AF_INET;
	lock_sock(sk);
	if (peer) {
		if (sk->sk_state != TCP_ESTABLISHED) {
#line 565
			MATT_TRACE(0, 2, "sk:%p state:%d", sk, sk->sk_state);
			release_sock(sk);
			return -ENOTCONN;
		}
		port = sk->sk_dport;
		addr = sk->sk_daddr;
		tag = "dst";
	} else {
		addr = sk->sk_rcv_saddr;
		if (!addr)
			addr = inet_sk(sk)->inet_saddr;
		port = inet_sk(sk)->inet_sport;
		tag = "src";
	}
	sin->sin_port = port;
	sin->sin_addr.s_addr = addr;
#line 581
	MATT_TRACE(0, 9, "sk:%p %s %pI4:%u addr_len=%d", sk, tag, &sin->sin_addr, ntohs(port), 16);
	release_sock(sk);
	memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
	return sysctl_inet_enable ? 16 : 0;
}

void matt_inet4_set_sk_addr(struct sock *sk, union matt_sockaddr *a, bool src)
{
	__be16 port = a->v4.sin_port;

	if (src) {
		inet_sk(sk)->inet_sport = port;
		inet_sk(sk)->inet_saddr = a->v4.sin_addr.s_addr;
	} else {
		sk->sk_dport = port;
		sk->sk_daddr = a->v4.sin_addr.s_addr;
	}
}

/* ---- IPv6 ---- */
int matt_inet6_is_any_addr(union matt_sockaddr *a)
{
	return ipv6_addr_any(&a->v6.sin6_addr);
}

void matt_inet6_udp_conf_init(struct sock *sk, struct udp_port_cfg *conf, union matt_sockaddr *a)
{
	conf->family = AF_INET6;
	conf->local_ip6 = a->v6.sin6_addr;
	conf->use_udp6_rx_checksums = 1;
	conf->local_udp_port = a->v6.sin6_port;
	conf->ipv6_v6only = sk->sk_ipv6only;
#line 100
	MATT_TRACE(0, 9, "v6 only:%d port:%d any:%d %pI6c:%d", sk->sk_ipv6only, ntohs(a->v6.sin6_port),
		   matt_is_any_addr2(a) != 0, &a->v6.sin6_addr, ntohs(a->v6.sin6_port));
}

void matt_inet6_get_skb_addrs(struct sk_buff *skb, union matt_sockaddr *da, union matt_sockaddr *sa)
{
	struct udphdr *uh = (struct udphdr *)(skb->head + MATT_SKB_CB(skb)->udph_offset);

	sa->v6.sin6_family = AF_INET6;
	sa->v6.sin6_port = uh->source;
	sa->v6.sin6_addr = ipv6_hdr(skb)->saddr;
	da->v6.sin6_family = AF_INET6;
	da->v6.sin6_port = uh->dest;
	da->v6.sin6_addr = ipv6_hdr(skb)->daddr;
}

int matt_inet6_get_user_addr(struct sock *sk, union matt_sockaddr *a, struct sockaddr *addr, int addr_len)
{
	if ((unsigned int)addr_len < 16)
		return 1;
	if (addr->sa_family == sk->sk_family) {
		if ((unsigned int)addr_len < 28)
			return 1;
		memcpy(a, addr, 28);
		return 0;
	}
	if (sk->sk_ipv6only)
		return 1;
	memcpy(a, addr, 16);
	return 0;
}

bool matt_inet6_cmp_sk_addr(struct sock *sk, union matt_sockaddr *a, union matt_sockaddr *addr)
{
	if (a->v4.sin_port != addr->v4.sin_port)
		return false;
	if (a->sa.sa_family == AF_INET && addr->sa.sa_family == AF_INET) {
		if (!a->v4.sin_addr.s_addr)
			return true;
		return !addr->v4.sin_addr.s_addr || a->v4.sin_addr.s_addr == addr->v4.sin_addr.s_addr;
	}
	if (a->sa.sa_family == addr->sa.sa_family) {
		if (ipv6_addr_any(&a->v6.sin6_addr) || ipv6_addr_any(&addr->v6.sin6_addr))
			return true;
		return ipv6_addr_equal(&a->v6.sin6_addr, &addr->v6.sin6_addr);
	}
	if (sk->sk_ipv6only)
		return false;
	if (a->sa.sa_family == AF_INET6) {
		if (ipv6_addr_any(&a->v6.sin6_addr))
			return true;
		if (addr->sa.sa_family != AF_INET)
			return false;
		return ipv6_addr_v4mapped(&a->v6.sin6_addr) && a->v6.sin6_addr.s6_addr32[3] == addr->v4.sin_addr.s_addr;
	}
	if (a->sa.sa_family != AF_INET || addr->sa.sa_family != AF_INET6)
		return false;
	return ipv6_addr_v4mapped(&addr->v6.sin6_addr) && addr->v6.sin6_addr.s6_addr32[3] == a->v4.sin_addr.s_addr;
}

int matt_inet6_get_sk_addr(struct socket *sock, struct sockaddr *uaddr, int peer)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)uaddr;
	int ret;

	ret = xm_inet6_getname2(sock, uaddr, peer);
	if (ret < 0) {
#line 648
		MATT_TRACE(0, 2, "fail ret:%d", ret);
		return ret;
	}
	if (uaddr->sa_family != AF_INET) {
		if (uaddr->sa_family == AF_INET6 && ipv6_addr_v4mapped(&sin6->sin6_addr)) {
			struct sockaddr_in *sin = (struct sockaddr_in *)uaddr;

			sin->sin_addr.s_addr = sin6->sin6_addr.s6_addr32[3];
			sin->sin_family = AF_INET;
		} else {
			return sysctl_inet_enable ? 28 : 0;
		}
	}
	*(u64 *)(uaddr->sa_data + 6) = 0;
	return sysctl_inet_enable ? 16 : 0;
}

void matt_inet6_set_sk_addr(struct sock *sk, union matt_sockaddr *a, bool src)
{
	__be16 port = a->v4.sin_port;

	if (src) {
		inet_sk(sk)->inet_sport = port;
		if (a->sa.sa_family == AF_INET) {
			sk->sk_v6_rcv_saddr.s6_addr32[0] = 0;
			sk->sk_v6_rcv_saddr.s6_addr32[1] = 0;
			sk->sk_v6_rcv_saddr.s6_addr32[2] = htonl(0xffff);
			sk->sk_v6_rcv_saddr.s6_addr32[3] = a->v4.sin_addr.s_addr;
		} else
			sk->sk_v6_rcv_saddr = a->v6.sin6_addr;
	} else {
		sk->sk_dport = port;
		if (a->sa.sa_family == AF_INET) {
			sk->sk_v6_daddr.s6_addr32[0] = 0;
			sk->sk_v6_daddr.s6_addr32[1] = 0;
			sk->sk_v6_daddr.s6_addr32[2] = htonl(0xffff);
			sk->sk_v6_daddr.s6_addr32[3] = a->v4.sin_addr.s_addr;
		} else
			sk->sk_v6_daddr = a->v6.sin6_addr;
	}
}

int xm_inet6_getname2(struct socket *sock, struct sockaddr *uaddr, int peer)
{
	struct sockaddr_in6 *sin = (struct sockaddr_in6 *)uaddr;
	struct sock *sk = sock->sk;
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	int ret;

	if (!sin)
		return 0;
	sin->sin6_family = AF_INET6;
	sin->sin6_flowinfo = 0;
	sin->sin6_scope_id = 0;
	lock_sock(sk);
	if (peer) {
		if (sk->sk_state != TCP_ESTABLISHED) {
#line 613
			MATT_TRACE(0, 2, "sk:%p state:%d", sk, sk->sk_state);
			ret = -ENOTCONN;
			goto out;
		}
		sin->sin6_port = sk->sk_dport;
		sin->sin6_addr = sk->sk_v6_daddr;
		if (np && np->sndflow)
			sin->sin6_flowinfo = np->flow_label;
	} else {
		if (ipv6_addr_any(&sk->sk_v6_rcv_saddr))
			sin->sin6_addr = np->saddr;
		else
			sin->sin6_addr = sk->sk_v6_rcv_saddr;
		sin->sin6_port = inet->inet_sport;
	}
	sin->sin6_scope_id = ipv6_iface_scope_id(&sin->sin6_addr, sk->sk_bound_dev_if);
	ret = sizeof(*sin);
out:
	release_sock(sk);
	return ret;
}

#include "matt_inet4.h"
#include "matt_inet6.h"
