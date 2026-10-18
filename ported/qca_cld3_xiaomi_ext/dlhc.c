/*
 * Xiaomi p2p header compression (dlhc): a TCP and a UDP header compressor in the style of
 * Van Jacobson's slhc.c, reconstructed from the OS4 stock qca_cld3_*.ko.
 *
 * The state machine (connection slots, LRU ring of transmit slots, uncompressed packets that carry the
 * connection id in the protocol field) is the one of slhc.c, the compressed formats are Xiaomi's:
 *
 *	compressed TCP (type 0x80):  [flags][cid]  id(2) seq(4) ack(4) window(2) check(2)  data
 *	compressed UDP (type 0xa0):  [flags][cid]  id(2) len(2) check(2)  data
 *	uncompressed TCP is type 0x70, uncompressed UDP is type 0x90 (high nibble of the first byte),
 *	flags bit 3 = connection id follows, bit 2 = TCP PSH.
 *
 * Original slhc.c notice:
 *
 * Copyright (c) 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Van Jacobson (van@helios.ee.lbl.gov), Dec 31, 1989:
 *	- Initial distribution.
 *	... (modified for KA9Q / Linux by Katie Stevens, Dmitry Gorodchanin, Bjorn Ekwall et al.)
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <net/slhc_vj.h>
#include "p2phc.h"

#ifdef CONFIG_INET
#include <linux/in.h>
#include <net/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/checksum.h>
#include <asm/unaligned.h>

#define DL_TYPE_UNCOMPRESSED_TCP	0x70
#define DL_TYPE_COMPRESSED_TCP		0x80
#define DL_TYPE_UNCOMPRESSED_UDP	0x90
#define DL_TYPE_COMPRESSED_UDP		0xa0
#define DL_NEW_C			0x08
#define DL_PUSH				0x04

/* Xiaomi variant of slhc_init() for one compressor. NOTE (as in the stock): the dlcompress structure is
 * allocated with kmalloc, not zeroed; only the fields written here are defined. */
static struct dl_slcompress *__dlhc_init(struct dl_slcompress *comp, int rslots, int tslots, int flags)
{
	short i;
	struct dl_cstate *ts;

	if (!comp)
		return ERR_PTR(-ENOMEM);

	if (rslots > 0) {
		comp->rstate = kmalloc(rslots * sizeof(struct dl_cstate), GFP_KERNEL);
		if (!comp->rstate)
			return ERR_PTR(-ENOMEM);
		comp->rslot_limit = rslots - 1;
	}

	if (tslots > 0) {
		comp->tstate = kmalloc(tslots * sizeof(struct dl_cstate), GFP_KERNEL);
		if (!comp->tstate) {
			kfree(comp->rstate);
			return ERR_PTR(-ENOMEM);
		}
		comp->tslot_limit = tslots - 1;
	}

	comp->xmit_oldest = 0;
	comp->xmit_current = 255;
	comp->recv_current = 255;
	comp->flags |= SLF_TOSS | flags;

	if (tslots > 0) {
		ts = comp->tstate;
		for (i = comp->tslot_limit; i > 0; --i) {
			ts[i].cs_this = i;
			ts[i].next = &(ts[i - 1]);
		}
		ts[0].next = &(ts[comp->tslot_limit]);
		ts[0].cs_this = 0;
	}
	return comp;
}

struct dlhc *dlhc_init(int rslots, int tslots)
{
	struct dlhc *comp;

	if ((unsigned int)(rslots | tslots) > 255)
		return ERR_PTR(-EINVAL);

	comp = kzalloc(sizeof(*comp), GFP_KERNEL);
	if (!comp)
		return ERR_PTR(-ENOMEM);

	if (IS_ERR(__dlhc_init(&comp->tcp, rslots, tslots, 0x80))) {
		kfree(comp);
		return ERR_PTR(-ENOMEM);
	}
	if (IS_ERR(__dlhc_init(&comp->udp, rslots, tslots, 0x40))) {
		dlhc_free(comp);
		return ERR_PTR(-ENOMEM);
	}
	return comp;
}

void dlhc_free(struct dlhc *comp)
{
	if (comp == NULL)
		return;

	if (!IS_ERR(comp)) {
		if (comp->tcp.tstate)
			kfree(comp->tcp.tstate);
		if (comp->tcp.rstate)
			kfree(comp->tcp.rstate);
		if (comp->udp.tstate)
			kfree(comp->udp.tstate);
		if (comp->udp.rstate)
			kfree(comp->udp.rstate);
		kfree(comp);
	}
}

static __always_inline void dlhc_reset_one(struct dl_slcompress *comp)
{
	struct dl_cstate *ts = comp->tstate;
	struct dl_cstate *rs;
	int i;

	comp->xmit_oldest = 0;
	comp->xmit_current = 255;
	memset(ts, 0, (comp->tslot_limit + 1) * sizeof(struct dl_cstate));
	for (i = comp->tslot_limit; i > 0; --i) {
		ts[i].cs_this = i;
		ts[i].next = &ts[i - 1];
	}
	ts[0].next = &ts[comp->tslot_limit];
	comp->recv_current = 255;
	comp->flags |= SLF_TOSS;
	rs = comp->rstate;
	for (i = comp->rslot_limit; i > 0; --i)
		rs[i].initialized = false;
}

int dlhc_reset(struct dlhc *comp)
{
	if (!comp || IS_ERR(comp))
		return -1;

	dlhc_reset_one(&comp->tcp);
	dlhc_reset_one(&comp->udp);
	return 0;
}

static int dlhc_toss_one(struct dl_slcompress *comp)
{
	if (comp == NULL)
		return 0;

	comp->flags |= SLF_TOSS;
	return 0;
}

int dlhc_toss(struct dlhc *dc)
{
	if (dc == NULL)
		return 0;

	dc->tcp.flags |= SLF_TOSS;
	return 0;
}

/* ------------------------------------------------------------------ compress */

static int dlhc_compress_tcp(struct dl_slcompress *comp, unsigned char *icp, int isize,
			     unsigned char *ocp, unsigned char **cpp, int compress_cid)
{
	struct dl_cstate *ocs = &(comp->tstate[comp->xmit_oldest]);
	struct dl_cstate *lcs = ocs;
	struct dl_cstate *cs = lcs->next;
	struct iphdr *ip = (struct iphdr *)icp;
	struct tcphdr *th;
	int nlen, hlen;
	unsigned char *cp;

	nlen = ip->ihl * 4;
	if (isize < nlen + sizeof(*th))
		return isize;

	th = (struct tcphdr *)(icp + nlen);
	if (th->doff < sizeof(struct tcphdr) / 4)
		return isize;
	hlen = nlen + th->doff * 4;

	/* only plain ACK segments are compressible */
	if (th->syn || th->fin || th->rst || th->urg || !th->ack || hlen > isize) {
		comp->sls_o_tcp++;
		return isize;
	}

	for ( ; ; ) {
		if (ip->saddr == cs->cs_ip.saddr &&
		    ip->daddr == cs->cs_ip.daddr &&
		    th->source == cs->cs_tcp.source &&
		    th->dest == cs->cs_tcp.dest)
			goto found;

		/* if current equal oldest, at end of list */
		if (cs == ocs)
			break;
		lcs = cs;
		cs = cs->next;
		comp->sls_o_searches++;
	}
	comp->sls_o_misses++;
	comp->xmit_oldest = lcs->cs_this;
	goto uncompressed;

found:
	if (lcs == ocs) {
		/* found at most recently used */
	} else if (cs == ocs) {
		/* found at least recently used */
		comp->xmit_oldest = lcs->cs_this;
	} else {
		/* more than 2 elements */
		lcs->next = cs->next;
		cs->next = ocs->next;
		ocs->next = cs;
	}

	/* anything that must not change between the packets changed: send the packet uncompressed */
	if (ip->version != cs->cs_ip.version || ip->ihl != cs->cs_ip.ihl ||
	    ip->tos != cs->cs_ip.tos ||
	    (ip->frag_off & htons(0x4000)) != (cs->cs_ip.frag_off & htons(0x4000)) ||
	    ip->ttl != cs->cs_ip.ttl ||
	    th->doff != cs->cs_tcp.doff ||
	    (ip->ihl > 5 && memcmp(ip + 1, cs->cs_ipopt, ((ip->ihl) - 5) * 4) != 0) ||
	    (th->doff > 5 && memcmp(th + 1, cs->cs_tcpopt, ((th->doff) - 5) * 4) != 0) ||
	    (!th->urg && th->urg_ptr != cs->cs_tcp.urg_ptr))
		goto uncompressed;

	cp = ocp;
	if (compress_cid == 0 || comp->xmit_current != cs->cs_this) {
		*cpp = ocp;
		*cp++ = DL_NEW_C;
		*cp++ = cs->cs_this;
		comp->xmit_current = cs->cs_this;
	} else {
		*cpp = ocp;
		*cp++ = 0;
	}
	put_unaligned(ip->id, (__be16 *)cp);
	put_unaligned(th->seq, (__be32 *)(cp + 2));
	put_unaligned(th->ack_seq, (__be32 *)(cp + 6));
	put_unaligned(th->window, (__be16 *)(cp + 10));
	put_unaligned(th->check, (__be16 *)(cp + 12));
	cp += 14;
	if (th->psh)
		ocp[0] |= DL_PUSH;
	memcpy(cp, icp + hlen, isize - hlen);
	ocp[0] = DL_TYPE_COMPRESSED_TCP | (ocp[0] & 0xf);
	comp->sls_o_compressed++;
	return isize - hlen + (cp - ocp);

uncompressed:
	memcpy(&cs->cs_ip, ip, 20);
	memcpy(&cs->cs_tcp, th, 20);
	if (ip->ihl > 5)
		memcpy(cs->cs_ipopt, ip + 1, ((ip->ihl) - 5) * 4);
	if (th->doff > 5)
		memcpy(cs->cs_tcpopt, th + 1, ((th->doff) - 5) * 4);
	comp->xmit_current = cs->cs_this;
	comp->sls_o_uncompressed++;
	memcpy(ocp, icp, isize);
	*cpp = ocp;
	ocp[9] = cs->cs_this;
	ocp[0] = DL_TYPE_UNCOMPRESSED_TCP | (ocp[0] & 0xf);
	return isize;
}

static int dlhc_compress_udp(struct dl_slcompress *comp, unsigned char *icp, int isize,
			     unsigned char *ocp, unsigned char **cpp, int compress_cid)
{
	struct dl_cstate *ocs = &(comp->tstate[comp->xmit_oldest]);
	struct dl_cstate *lcs = ocs;
	struct dl_cstate *cs = lcs->next;
	struct iphdr *ip = (struct iphdr *)icp;
	struct udphdr *uh;
	int nlen, hlen;
	unsigned char *cp;

	nlen = ip->ihl * 4;
	hlen = nlen + sizeof(*uh);
	if (isize < hlen)
		return isize;

	uh = (struct udphdr *)(icp + nlen);

	for ( ; ; ) {
		if (ip->saddr == cs->cs_ip.saddr &&
		    ip->daddr == cs->cs_ip.daddr &&
		    uh->source == cs->cs_udp.source &&
		    uh->dest == cs->cs_udp.dest)
			goto found;

		if (cs == ocs)
			break;
		lcs = cs;
		cs = cs->next;
		comp->sls_o_searches++;
	}
	comp->sls_o_misses++;
	comp->xmit_oldest = lcs->cs_this;
	goto uncompressed;

found:
	if (lcs == ocs) {
		/* found at most recently used */
	} else if (cs == ocs) {
		/* found at least recently used */
		comp->xmit_oldest = lcs->cs_this;
	} else {
		/* more than 2 elements */
		lcs->next = cs->next;
		cs->next = ocs->next;
		ocs->next = cs;
	}

	if (ip->version != cs->cs_ip.version || ip->ihl != cs->cs_ip.ihl ||
	    ip->tos != cs->cs_ip.tos ||
	    (ip->frag_off & htons(0x4000)) != (cs->cs_ip.frag_off & htons(0x4000)) ||
	    ip->ttl != cs->cs_ip.ttl ||
	    (ip->ihl > 5 && memcmp(ip + 1, cs->cs_ipopt, ((ip->ihl) - 5) * 4) != 0))
		goto uncompressed;

	cp = ocp;
	if (compress_cid == 0 || comp->xmit_current != cs->cs_this) {
		*cpp = ocp;
		*cp++ = DL_NEW_C;
		*cp++ = cs->cs_this;
		comp->xmit_current = cs->cs_this;
	} else {
		*cpp = ocp;
		*cp++ = 0;
	}
	put_unaligned(ip->id, (__be16 *)cp);
	put_unaligned(uh->len, (__be16 *)(cp + 2));
	put_unaligned(uh->check, (__be16 *)(cp + 4));
	cp += 6;
	memcpy(cp, icp + hlen, isize - hlen);
	ocp[0] = DL_TYPE_COMPRESSED_UDP | (ocp[0] & 0xf);
	comp->sls_o_compressed++;
	return isize - hlen + (cp - ocp);

uncompressed:
	memcpy(&cs->cs_ip, ip, 20);
	memcpy(&cs->cs_udp, uh, sizeof(*uh));
	if (ip->ihl > 5)
		memcpy(cs->cs_ipopt, ip + 1, ((ip->ihl) - 5) * 4);
	comp->xmit_current = cs->cs_this;
	comp->sls_o_uncompressed++;
	memcpy(ocp, icp, isize);
	*cpp = ocp;
	ocp[9] = cs->cs_this;
	ocp[0] = DL_TYPE_UNCOMPRESSED_UDP | (ocp[0] & 0xf);
	return isize;
}

int dlhc_compress(struct dlhc *dc, unsigned char *icp, int isize, unsigned char *ocp,
		  unsigned char **cpp, int compress_cid)
{
	struct iphdr *ip;

	/* Don't play with runt packets. */
	if (isize < sizeof(struct iphdr))
		return isize;

	ip = (struct iphdr *)icp;
	if (ip->version != 4 || ip->ihl < 5 || (ntohs(ip->frag_off) & 0x3fff))
		return isize;

	if (ip->protocol == IPPROTO_TCP)
		return dlhc_compress_tcp(&dc->tcp, icp, isize, ocp, cpp, compress_cid);
	if (ip->protocol == IPPROTO_UDP)
		return dlhc_compress_udp(&dc->udp, icp, isize, ocp, cpp, compress_cid);
	return isize;
}

/* ---------------------------------------------------------------- uncompress */

static int dlhc_uncompress_tcp(struct dl_slcompress *comp, unsigned char *icp, int isize)
{
	struct dl_cstate *cs;
	unsigned char changes;
	unsigned char *cp;
	int x, len, hdrlen;

	comp->sls_i_compressed++;
	if (isize < 15) {
		comp->sls_i_error++;
		return 0;
	}
	changes = icp[0];
	cp = icp + 1;
	if (changes & DL_NEW_C) {
		x = *cp++;
		if (x > comp->rslot_limit)
			goto bad;
		if (!comp->rstate[x].initialized)
			goto bad;
		comp->flags &= ~SLF_TOSS;
		comp->recv_current = x;
	} else {
		if (comp->flags & SLF_TOSS) {
			comp->sls_i_tossed++;
			return 0;
		}
		x = comp->recv_current;
	}
	cs = &comp->rstate[x];

	cs->cs_ip.id = get_unaligned((__be16 *)cp);
	cs->cs_tcp.seq = get_unaligned((__be32 *)(cp + 2));
	cs->cs_tcp.ack_seq = get_unaligned((__be32 *)(cp + 6));
	cs->cs_tcp.psh = (changes & DL_PUSH) ? 1 : 0;
	cs->cs_tcp.window = get_unaligned((__be16 *)(cp + 10));
	cs->cs_tcp.check = get_unaligned((__be16 *)(cp + 12));
	cp += 14;

	len = isize - (cp - icp);
	if (len < 0)
		goto bad;

	hdrlen = cs->cs_ip.ihl * 4 + cs->cs_tcp.doff * 4;
	len += hdrlen;
	cs->cs_ip.tot_len = htons(len);
	cs->cs_ip.check = 0;

	memmove(icp + hdrlen, cp, len - hdrlen);

	cp = icp;
	memcpy(cp, &cs->cs_ip, 20);
	cp += 20;
	if (cs->cs_ip.ihl > 5) {
		memcpy(cp, cs->cs_ipopt, (cs->cs_ip.ihl - 5) * 4);
		cp += (cs->cs_ip.ihl - 5) * 4;
	}

	put_unaligned(ip_fast_csum(icp, cs->cs_ip.ihl), &((struct iphdr *)icp)->check);

	memcpy(cp, &cs->cs_tcp, 20);
	cp += 20;
	if (cs->cs_tcp.doff > 5)
		memcpy(cp, cs->cs_tcpopt, ((cs->cs_tcp.doff) - 5) * 4);

	return len;
bad:
	comp->sls_i_error++;
	return dlhc_toss_one(comp);
}

static int dlhc_uncompress_udp(struct dl_slcompress *comp, unsigned char *icp, int isize)
{
	struct dl_cstate *cs;
	unsigned char changes;
	unsigned char *cp;
	int x, len, hdrlen;

	comp->sls_i_compressed++;
	if (isize < 7) {
		comp->sls_i_error++;
		return 0;
	}
	changes = icp[0];
	cp = icp + 1;
	if (changes & DL_NEW_C) {
		x = *cp++;
		if (x > comp->rslot_limit)
			goto bad;
		if (!comp->rstate[x].initialized)
			goto bad;
		comp->flags &= ~SLF_TOSS;
		comp->recv_current = x;
	} else {
		if (comp->flags & SLF_TOSS) {
			comp->sls_i_tossed++;
			return 0;
		}
		x = comp->recv_current;
	}
	cs = &comp->rstate[x];

	cs->cs_ip.id = get_unaligned((__be16 *)cp);
	cs->cs_udp.len = get_unaligned((__be16 *)(cp + 2));
	cs->cs_udp.check = get_unaligned((__be16 *)(cp + 4));
	cp += 6;

	len = isize - (cp - icp);
	if (len < 0)
		goto bad;

	hdrlen = cs->cs_ip.ihl * 4 + sizeof(struct udphdr);
	len += hdrlen;
	cs->cs_ip.tot_len = htons(len);
	cs->cs_ip.check = 0;

	memmove(icp + hdrlen, cp, len - hdrlen);

	cp = icp;
	memcpy(cp, &cs->cs_ip, 20);
	cp += 20;
	if (cs->cs_ip.ihl > 5) {
		memcpy(cp, cs->cs_ipopt, (cs->cs_ip.ihl - 5) * 4);
		cp += (cs->cs_ip.ihl - 5) * 4;
	}

	put_unaligned(ip_fast_csum(icp, cs->cs_ip.ihl), &((struct iphdr *)icp)->check);

	memcpy(cp, &cs->cs_udp, sizeof(struct udphdr));

	return len;
bad:
	comp->sls_i_error++;
	return dlhc_toss_one(comp);
}

int dlhc_uncompress(struct dlhc *dc, unsigned char *icp, int isize)
{
	unsigned char type = icp[0] & 0xf0;

	if (type == DL_TYPE_COMPRESSED_TCP)
		return dlhc_uncompress_tcp(&dc->tcp, icp, isize);
	if (type == DL_TYPE_COMPRESSED_UDP)
		return dlhc_uncompress_udp(&dc->udp, icp, isize);
	return 0;
}

/* ------------------------------------------------------------------ remember */

static int __dlhc_remember_tcp(struct dl_slcompress *comp, unsigned char *icp, int isize)
{
	struct iphdr *iph = (struct iphdr *)icp;
	struct tcphdr *th;
	struct dl_cstate *cs;
	unsigned char index;
	unsigned int ihl = icp[0] & 0xf;

	if (isize < (int)sizeof(struct iphdr) || ihl < 5) {
		comp->sls_i_runt++;
		return dlhc_toss_one(comp);
	}
	if (ip_fast_csum(icp, ihl)) {
		/* Bad IP header checksum; discard */
		comp->sls_i_badcheck++;
		return dlhc_toss_one(comp);
	}
	index = iph->protocol;
	if (index > comp->rslot_limit) {
		comp->sls_i_error++;
		return dlhc_toss_one(comp);
	}
	iph->version = 4;
	iph->protocol = IPPROTO_TCP;
	ip_send_check(iph);

	th = (struct tcphdr *)(icp + ihl * 4);
	cs = &comp->rstate[comp->recv_current = index];
	comp->flags &= ~SLF_TOSS;
	memcpy(&cs->cs_ip, iph, sizeof(*iph));
	memcpy(&cs->cs_tcp, th, sizeof(*th));
	if (ihl > 5)
		memcpy(cs->cs_ipopt, &iph[1], (ihl - 5) * 4);
	if (cs->cs_tcp.doff > 5)
		memcpy(cs->cs_tcpopt, &th[1], (cs->cs_tcp.doff - 5) * 4);
	cs->initialized = true;
	comp->sls_i_uncompressed++;
	return isize;
}

static int __dlhc_remember_udp(struct dl_slcompress *comp, unsigned char *icp, int isize)
{
	struct iphdr *iph = (struct iphdr *)icp;
	struct dl_cstate *cs;
	unsigned char index;
	unsigned int ihl = icp[0] & 0xf;

	if (isize < (int)sizeof(struct iphdr) || ihl < 5) {
		comp->sls_i_runt++;
		return dlhc_toss_one(comp);
	}
	if (ip_fast_csum(icp, ihl)) {
		comp->sls_i_badcheck++;
		return dlhc_toss_one(comp);
	}
	index = iph->protocol;
	if (index > comp->rslot_limit) {
		comp->sls_i_error++;
		return dlhc_toss_one(comp);
	}
	iph->version = 4;
	iph->protocol = IPPROTO_UDP;
	ip_send_check(iph);

	cs = &comp->rstate[comp->recv_current = index];
	comp->flags &= ~SLF_TOSS;
	memcpy(&cs->cs_ip, iph, sizeof(*iph));
	memcpy(&cs->cs_udp, icp + ihl * 4, sizeof(struct udphdr));
	if (ihl > 5)
		memcpy(cs->cs_ipopt, &iph[1], (ihl - 5) * 4);
	cs->initialized = true;
	comp->sls_i_uncompressed++;
	return isize;
}

int dlhc_remember(struct dlhc *dc, unsigned char *icp, int isize)
{
	unsigned char type = icp[0] & 0xf0;

	if (type == DL_TYPE_UNCOMPRESSED_TCP)
		return __dlhc_remember_tcp(&dc->tcp, icp, isize);
	if (type == DL_TYPE_UNCOMPRESSED_UDP)
		return __dlhc_remember_udp(&dc->udp, icp, isize);
	return 0;
}

#endif /* CONFIG_INET */
