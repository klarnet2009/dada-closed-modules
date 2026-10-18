// SPDX-License-Identifier: GPL-2.0
/* matt management-packet builders (stock: mp_tx.c): ping / pong / rst / report. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"



/* common prologue: room for the lower headers + one management header, header pushed, cb prepared */
static inline struct sk_buff *matt_mgmt_skb_alloc(unsigned int len)
{
	return alloc_skb(len, 0x820);
}

struct sk_buff *matt_make_ping(struct sock *sk)
{
	union matt_sockaddr *da = &MATT_SK(sk)->dst.addr.addr;
	struct matt_ping *ping;
	struct matt_skb_cb *cb;
	struct sk_buff *skb;
	u64 now = ktime_get();
	u32 ts, len;

	if (!da) {
#line 426
		MATT_TRACE(0, 2, "invalid da!");
		return NULL;
	}
	len = matt_encap_lower_header_len2(da) + sizeof(*ping);
	skb = matt_mgmt_skb_alloc(len);
	if (!skb) {
#line 435
		MATT_TRACE(0, 2, "skb allocate fail");
		return NULL;
	}
	skb_reserve(skb, len);
	ts = div_u64(now, NSEC_PER_USEC);
	ping = skb_push(skb, sizeof(*ping));
	cb = MATT_SKB_CB(skb);
	cb->offset_start = 0;
	cb->offset_end = 1;
	cb->flags = 0xc0;
	cb->types = MATT_MGMT_TYPE_PING;
	cb->gso_segs = 1;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_reset_transport_header(skb);
	ping->timestamp = htonl(ts);
#line 460
	MATT_TRACE(0, 9, "timestamp_us: %08x", ts);
	cb->offset_end = ping->hdr.offset + ping->hdr.len;
	matt_dump_skb(__func__, skb);
	return skb;
}

struct sk_buff *matt_make_pong(struct sock *sk, u32 timestamp)
{
	union matt_sockaddr *da = &MATT_SK(sk)->dst.addr.addr;
	struct matt_pong *pong;
	struct matt_skb_cb *cb;
	struct sk_buff *skb;
	u32 len;

	if (!da) {
#line 484
		MATT_TRACE(0, 2, "invalid da!");
		return NULL;
	}
	len = matt_encap_lower_header_len2(da) + sizeof(*pong);
	skb = matt_mgmt_skb_alloc(len);
	if (!skb) {
#line 493
		MATT_TRACE(0, 2, "skb allocate fail");
		return NULL;
	}
	skb_reserve(skb, len);
	pong = skb_push(skb, sizeof(*pong));
	cb = MATT_SKB_CB(skb);
	cb->offset_start = 0;
	cb->offset_end = 1;
	cb->flags = 0xc0;
	cb->types = MATT_MGMT_TYPE_PONG;
	cb->gso_segs = 1;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_reset_transport_header(skb);
	timestamp = htonl(timestamp);
#line 517
	MATT_TRACE(0, 9, "timestamp: %08x", timestamp);
	pong->timestamp = timestamp;
	cb->offset_end = pong->hdr.offset + pong->hdr.len;
	matt_dump_skb(__func__, skb);
	return skb;
}

struct sk_buff *matt_make_rst(struct sock *sk, u32 reasoncode)
{
	union matt_sockaddr *da = &MATT_SK(sk)->dst.addr.addr;
	struct matt_rst *rst;
	struct matt_skb_cb *cb;
	struct sk_buff *skb;
	u32 len;

	if (!da) {
#line 542
		MATT_TRACE(0, 2, "invalid da!");
		return NULL;
	}
	len = matt_encap_lower_header_len2(da) + sizeof(*rst);
	skb = matt_mgmt_skb_alloc(len);
	if (!skb) {
#line 551
		MATT_TRACE(0, 2, "skb allocate fail");
		return NULL;
	}
	skb_reserve(skb, len);
	rst = skb_push(skb, sizeof(*rst));
	cb = MATT_SKB_CB(skb);
	cb->offset_start = 0;
	cb->offset_end = 1;
	cb->flags = 0xc0;
	cb->types = MATT_MGMT_TYPE_RESET;
	cb->gso_segs = 1;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_reset_transport_header(skb);
	rst->reasoncode = reasoncode;	/* G1ad: the stock stores the reason raw [str w20,[x23,#0x10]]; G1ac had htonl */
#line 576
	MATT_TRACE(0, 4, "sk:%p shutdown, reason code:%d", sk, reasoncode);
	cb->offset_end = rst->hdr.offset + rst->hdr.len;
	matt_dump_skb(__func__, skb);
	return skb;
}

struct sk_buff *matt_make_report(struct sock *sk)
{
	union matt_sockaddr *da = &MATT_SK(sk)->dst.addr.addr;
	struct matt_output_ctx *oq = &MATT_SK(sk)->output_ctx;
	struct matt_report *rep;
	struct matt_skb_cb *cb;
	struct sk_buff *skb;
	u32 limit = sysctl_limit_output;
	u32 len;

	if (!da) {
#line 597
		MATT_TRACE(0, 2, "invalid da!");
		return NULL;
	}
	len = matt_encap_lower_header_len2(da) + sizeof(*rep);
	skb = matt_mgmt_skb_alloc(len);
	if (!skb) {
#line 606
		MATT_TRACE(0, 2, "skb allocate fail");
		return NULL;
	}
	skb_reserve(skb, len);
	rep = skb_push(skb, sizeof(*rep));
	cb = MATT_SKB_CB(skb);
	cb->offset_start = 0;
	cb->offset_end = 1;
	cb->flags = 0xc0;
	cb->types = MATT_MGMT_TYPE_REPORT;
	skb_reset_transport_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	cb->gso_segs = 1;
#line 630
	MATT_TRACE(0, 4, "sk:%p report rtt to peer: %u, limit: %u, cwnd: %u", sk, oq->srtt, limit, oq->snd_cwnd);
	rep->rtt = htonl(oq->srtt);
	rep->limit = htonl(limit);
	rep->cwnd = htonl(oq->snd_cwnd);
	cb->offset_end = rep->hdr.offset + rep->hdr.len;
	matt_dump_skb(__func__, skb);
	return skb;
}
