// SPDX-License-Identifier: GPL-2.0
/* matt management-packet builders (stock: mp_tx.c): connect request / connect response / pack / nack. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

/* free receive space, as the stock computes it for the pack/nack/creq: (rcvbuf - (rmem + backlog)) / 4, never negative */
static inline int matt_rcv_space(struct sock *sk)
{
	int space = READ_ONCE(sk->sk_rcvbuf) - (READ_ONCE(sk->sk_backlog.len) + atomic_read(&sk->sk_backlog.rmem_alloc));

	return max(space >> 2, 0);
}

struct sk_buff *matt_make_creq(struct sock *sk)
{
	union matt_sockaddr *da = &MATT_SK(sk)->dst.addr.addr;
	struct matt_creq *creq;
	struct matt_skb_cb *cb;
	struct sk_buff *skb;
	u32 len;

	if (!da) {
#line 200
		MATT_TRACE(0, 2, "invalid da!");
		return NULL;
	}
	len = matt_encap_lower_header_len2(da) + sizeof(*creq);
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
#line 209
		MATT_TRACE(0, 2, "skb allocate fail");
		return NULL;
	}
	skb_reserve(skb, len);
	cb = MATT_SKB_CB(skb);
	cb->offset_start = 0;
	cb->offset_end = 1;
	cb->flags = 0xc0;
	cb->types = MATT_MGMT_TYPE_CONN_REQ;
	cb->gso_segs = 1;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_push(skb, sizeof(*creq));
	creq = (struct matt_creq *)skb->data;
	skb_reset_transport_header(skb);
	creq->version = htons(MATT_VERSION_1);
	creq->mss = htons(MATT_SK(sk)->mss);
	creq->ts = htonl(div_u64(ktime_get(), NSEC_PER_USEC));
	creq->snd_buf = htonl(READ_ONCE(sk->sk_sndbuf));
	creq->rcv_buf = htonl(matt_rcv_space(sk));
	matt_dump_creq(creq);
	return skb;
}

struct sk_buff *matt_make_crsp(struct sock *sk, u32 timestamp)
{
	union matt_sockaddr *da = MATT_SK(sk)->pkt_mgmt_ctx.da;
	struct matt_crsp *crsp;
	struct matt_skb_cb *cb;
	struct sk_buff *skb;
	u32 len;

	if (!da) {
#line 310
		MATT_TRACE(0, 2, "invalid da!");
		return NULL;
	}
	len = matt_encap_lower_header_len2(da) + sizeof(*crsp);
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
#line 319
		MATT_TRACE(0, 2, "skb allocate fail");
		return NULL;
	}
	skb_reserve(skb, len);
	skb_push(skb, sizeof(*crsp));
	crsp = (struct matt_crsp *)skb->data;
	cb = MATT_SKB_CB(skb);
	cb->offset_start = 0;
	cb->offset_end = 1;
	cb->flags = 0xc0;
	cb->types = MATT_MGMT_TYPE_CONN_RSP;
	skb_reset_transport_header(skb);
	cb->gso_segs = 1;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	crsp->status = 0;
	crsp->version = htons(MATT_VERSION_MAX);
	crsp->ts = htonl(timestamp);
	crsp->mss = htons(MATT_SK(sk)->mss);
	crsp->snd_buf = htonl(READ_ONCE(sk->sk_sndbuf));
	crsp->rcv_buf = 0x200;
	cb->offset_end = crsp->hdr.offset + crsp->hdr.len;
	matt_dump_crsp(crsp);
	matt_dump_skb(__func__, skb);
	return skb;
}

struct sk_buff *matt_make_pack(struct sock *sk)
{
	union matt_sockaddr *da = &MATT_SK(sk)->dst.addr.addr;
	struct matt_input_ctx *iq = &MATT_SK(sk)->input_ctx;
	struct matt_pack *pack;
	struct matt_skb_cb *cb;
	struct sk_buff *skb;
	u32 i, sacks_len, len, delay, cur_ts;

	if (!da) {
#line 683
		MATT_TRACE(0, 2, "invalid da!");
		return NULL;
	}
	len = matt_encap_lower_header_len2(da);
	sacks_len = iq->num_sacks << 3;
	len += sizeof(*pack) + sacks_len;
	skb = alloc_skb(len, (sk->sk_allocation & __GFP_MEMALLOC) | GFP_ATOMIC | __GFP_NOWARN);
	if (!skb) {
#line 697
		MATT_TRACE(0, 2, "skb allocate fail");
		return NULL;
	}
	skb_reserve(skb, len);
	skb_push(skb, sizeof(*pack) + sacks_len);
	pack = (struct matt_pack *)skb->data;
	cb = MATT_SKB_CB(skb);
	cb->flags = 0xc0;
	cb->types = MATT_MGMT_TYPE_PACK;
	skb_reset_transport_header(skb);
	pack->timestamp = htonl(iq->min_owd_tx_ts);
	cur_ts = MATT_SK(sk)->pkt_mgmt_ctx.matt_mstamp;
	delay = cur_ts < iq->min_owd_rx_ts ? 0 : cur_ts - iq->min_owd_rx_ts;
	delay &= ~0xffu;
	pack->delay_ack = htonl(delay);
#line 719
	MATT_TRACE(0, 9, "cur_ts:%u min_owd_rx_ts:%u delay_ack:%u", (u32)MATT_SK(sk)->pkt_mgmt_ctx.matt_mstamp, iq->min_owd_rx_ts, delay);
	pack->recv_rate = htonl(3);
	iq->rcv_wnd = matt_rcv_space(sk);
	pack->recv_buffer = htonl(iq->rcv_wnd);
	pack->hol_offset = htonl(iq->rcv_nxt);
	pack->left_offset = htonl(iq->last_rcv_high_seq);
	pack->right_offset = htonl(iq->rcv_high_seq);
	iq->min_owd = 0;
	iq->min_owd_tx_ts = 0;
	iq->min_owd_rx_ts = 0;
	iq->last_rcv_high_seq = iq->rcv_high_seq;
	pack->sacks_size = htonl(sacks_len);
	for (i = 0; i < iq->num_sacks; i++) {
		struct matt_sack_block *sb = &iq->selective_acks[(iq->sack_first_index + i) & 0x7f];

		/* G1ad: via a pointer, NOT pack->sacks[i]: sacks[0] + -fstrict-flex-arrays=3 + array-bounds trap = brk #0x5512 on the first SACK block */
		((struct matt_sack_block *)(pack + 1))[i].offset_start = htonl(sb->offset_start);
		((struct matt_sack_block *)(pack + 1))[i].offset_end = htonl(sb->offset_end);
	}
	matt_dump_skb(__func__, skb);
	return skb;
}

struct sk_buff *matt_make_nack(struct sock *sk)
{
	union matt_sockaddr *da = &MATT_SK(sk)->dst.addr.addr;
	struct matt_input_ctx *iq = &MATT_SK(sk)->input_ctx;
	struct matt_nack *nack;
	struct matt_skb_cb *cb;
	struct sk_buff *skb;
	u32 i, n = 0, sacks_len, len;

	if (!da) {
#line 769
		MATT_TRACE(0, 2, "invalid da!");
		return NULL;
	}
	n = iq->num_sacks;
	sacks_len = n << 3;
	len = matt_encap_lower_header_len2(da) + sizeof(*nack) + sacks_len;
	skb = alloc_skb(len, (sk->sk_allocation & __GFP_MEMALLOC) | GFP_ATOMIC | __GFP_NOWARN);
	if (!skb) {
#line 780
		MATT_TRACE(0, 2, "skb allocate fail");
		return NULL;
	}
	skb_reserve(skb, len);
	skb_push(skb, sizeof(*nack) + sacks_len);
	nack = (struct matt_nack *)skb->data;
	cb = MATT_SKB_CB(skb);
	cb->flags = 0xc0;
	cb->types = MATT_MGMT_TYPE_NACK;
	skb_reset_transport_header(skb);
	iq->rcv_wnd = matt_rcv_space(sk);
	nack->recv_buffer = htonl(iq->rcv_wnd);
	nack->sacks_size = htonl(sacks_len);
	for (i = 0; i < iq->num_sacks; i++) {
		struct matt_sack_block *sb = &iq->selective_acks[(iq->sack_first_index + i) & 0x7f];

		((struct matt_sack_block *)(nack + 1))[i].offset_start = htonl(sb->offset_start);	/* G1ad: see matt_make_pack */
		((struct matt_sack_block *)(nack + 1))[i].offset_end = htonl(sb->offset_end);
	}
	return skb;
}
