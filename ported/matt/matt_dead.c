// SPDX-License-Identifier: GPL-2.0
/* The 2 out-of-line copies that the stock .ko still contains but nobody calls (static, kept by ThinLTO):
 * xm_fl6_sock_lookup (fl6_sock_lookup without the static key check),
 * matt_inet_get_local_port_range (the live copy is inlined in matt_sock.h). Reconstructed from the OS4 stock disassembly. */
#include "matt.h"
#include <net/ip6_checksum.h>
#include <net/ipv6.h>

extern struct ip6_flowlabel *(*xm__fl6_sock_lookup)(struct sock *sk, __be32 label);

__used noinline struct ip6_flowlabel *matt_dead_fl6_sock_lookup(struct sock *sk, __be32 label) __asm__("xm_fl6_sock_lookup");
__used noinline struct ip6_flowlabel *matt_dead_fl6_sock_lookup(struct sock *sk, __be32 label)
{
	if (READ_ONCE(sock_net(sk)->ipv6.flowlabel_has_excl))
		return xm__fl6_sock_lookup(sk, label) ?: ERR_PTR(-ENOENT);
	return NULL;
}

__used noinline void matt_dead_get_local_port_range(const struct net *net, int *low, int *high)
	__asm__("matt_inet_get_local_port_range");
__used noinline void matt_dead_get_local_port_range(const struct net *net, int *low, int *high)
{
	unsigned int seq;

	do {
		seq = read_seqbegin(&net->ipv4.ip_local_ports.lock);
		*low = net->ipv4.ip_local_ports.range[0];
		*high = net->ipv4.ip_local_ports.range[1];
	} while (read_seqretry(&net->ipv4.ip_local_ports.lock, seq));
}
