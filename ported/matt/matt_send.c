// SPDX-License-Identifier: GPL-2.0
/* matt send side (stock: mp_tx.c): management-packet transmit path and the pack/nack/crsp senders. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

void matt_wmem_free_skb(struct sock *sk, struct sk_buff *skb)
{
	sk->sk_wmem_queued -= skb->truesize;
	sk_mem_uncharge(sk, skb->truesize);
	kfree_skb(skb);
#line 830
	MATT_TRACE(0, 9, "sk:%p sk_wmem_alloc:%u sk_sndbuf:%u sk_wmem_queued:%u sk_stream_wspace:%u", sk,
		   refcount_read(&sk->sk_wmem_alloc), READ_ONCE(sk->sk_sndbuf), READ_ONCE(sk->sk_wmem_queued),
		   sk_stream_wspace(sk));
}

void matt_skb_entail(struct sock *sk, struct sk_buff *skb)
{
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);
	struct matt_output_ctx *oq = &MATT_SK(sk)->output_ctx;

	cb->offset_start = oq->write_seq;
	skb_header_release(skb);
	cb->offset_end = oq->write_seq;
	__skb_queue_tail(&sk->sk_write_queue, skb);
	sk->sk_wmem_queued += skb->truesize;
	sk_mem_charge(sk, skb->truesize);
#line 866
	MATT_TRACE(0, 9, "sk:%p offset(%u, %u) skb->truesize:%u sk_wmem_alloc:%u sk_sndbuf:%u sk_wmem_queued:%u sk_stream_wspace:%u",
		   sk, cb->offset_start, cb->offset_end, skb->truesize, refcount_read(&sk->sk_wmem_alloc),
		   READ_ONCE(sk->sk_sndbuf), READ_ONCE(sk->sk_wmem_queued), sk_stream_wspace(sk));
}

noinline void matt_mgmt_queue_skb(struct sock *sk, struct sk_buff *skb)
{
	refcount_add(skb->truesize, &sk->sk_wmem_alloc);
	if (MATT_SKB_CB(skb)->types == MATT_MGMT_TYPE_CONN_REQ)
		MATT_SK(sk)->output_ctx.write_seq = MATT_SKB_CB(skb)->offset_end;
}

void matt_update_skb_after_send(struct sock *sk, struct sk_buff *skb)
{
	list_move_tail(&skb->tcp_tsorted_anchor, &MATT_SK(sk)->output_ctx.tsorted_tx_queue);
}

noinline void matt_update_mss(struct sock *sk, union matt_sockaddr *da)
{
	struct dst_entry *dst = rcu_dereference_raw(sk->sk_dst_cache);
	u32 mtu;
	int mss;

	if (dst && (mtu = min_t(u32, dst_mtu(dst), 0xffff)))
		mss = mtu - matt_encap_len2(da) - sizeof(struct tcphdr);
	else
		mss = 1484 - matt_encap_len2(da);
	MATT_SK(sk)->mss = mss;
}

int matt_transmit_mgmt_skb(struct sock *sk, struct sk_buff *skb, int clone_it, struct flowi *fl)
{
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_output_ctx *oq = &ms->output_ctx;
	struct matt_input_ctx *iq = &ms->input_ctx;
	struct sk_buff *oskb = NULL, *nskb = skb;
	union matt_sockaddr *da;
	struct matthdr *hdr;
	u32 segs;

	if (!(cb->flags & 0x80)) {
#line 144
		MATT_TRACE(0, 2, "skb types mismatch! skb flags:%s mcb->types:%d", "data", cb->types);
		return -1;
	}
	if (!fl) {
#line 149
		MATT_TRACE(0, 2, "invalid fl!");
		return -1;
	}
	da = &ms->dst.addr.addr;
	if (sk->sk_state == TCP_LISTEN && cb->types == MATT_MGMT_TYPE_CONN_RSP)
		da = ms->pkt_mgmt_ctx.da;
	skb_header_release(skb);
	if (clone_it) {
		oskb = skb;
		tcp_skb_tsorted_save(oskb) {
			if (unlikely(skb_cloned(oskb)))
				nskb = pskb_copy(oskb, GFP_ATOMIC);
			else
				nskb = skb_clone(oskb, GFP_ATOMIC);
		} tcp_skb_tsorted_restore(oskb);
		if (unlikely(!nskb))
			return -ENOBUFS;
		nskb->dev = NULL;
	}
	nskb->pfmemalloc = 0;
	hdr = (struct matthdr *)nskb->data;
	hdr->num = htonl(iq->rcv_nxt_num);
	hdr->offset = htonl(iq->rcv_nxt);
	hdr->reserved = 0;
	hdr->flags = cb->flags;
	hdr->type = cb->types;
	hdr->len = htons(nskb->len - sizeof(*hdr));
	skb_orphan(nskb);
	nskb->sk = sk;
	nskb->destructor = sock_wfree;
	matt_mgmt_queue_skb(sk, nskb);
	matt_dump_skb(__func__, nskb);
	if (oskb)
		matt_dump_skb(__func__, oskb);
	segs = max_t(u16, MATT_SKB_CB(nskb)->gso_segs, 1);
	oq->tsend.total_num += segs;
	oq->tsend.total_bytes += nskb->len;
	if (MATT_SKB_CB(nskb)->flags & 0x80) {
		switch (MATT_SKB_CB(nskb)->types) {
		case MATT_MGMT_TYPE_CONN_REQ:
			oq->tsend.req_num++;
			break;
		case MATT_MGMT_TYPE_CONN_RSP:
			oq->tsend.rsp_num++;
			break;
		case MATT_MGMT_TYPE_RESET:
			oq->tsend.rst_num++;
			break;
		case MATT_MGMT_TYPE_PACK:
			oq->tsend.pack_num++;
			break;
		case MATT_MGMT_TYPE_NACK:
			oq->tsend.nack_num++;
			break;
		case MATT_MGMT_TYPE_PING:
			oq->tsend.ping_num++;
			break;
		case MATT_MGMT_TYPE_PONG:
			oq->tsend.pong_num++;
			break;
		default:
			break;
		}
	} else {
		oq->tsend.data_num += segs;
		oq->tsend.data_bytes += nskb->len - 16u;
	}
	return matt_lower_xmit2(sk, nskb, da, fl, true);
}

int matt_send_crsp(struct sock *sk, u32 timestamp)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct flowi fl = {};
	struct flowi *flp = &ms->fl;
	struct sk_buff *skb;

	if (sk->sk_state == TCP_LISTEN) {
		__sk_dst_reset(sk);
		if (matt_flow_route2(sk, ms->pkt_mgmt_ctx.da, ms->pkt_mgmt_ctx.sa, &fl)) {
#line 703
			MATT_TRACE(0, 2, "can't get route flow");
			return -EINVAL;
		}
		matt_update_mss(sk, ms->pkt_mgmt_ctx.da);
		flp = &fl;
	}
	skb = matt_make_crsp(sk, timestamp);
	if (!skb) {
#line 712
		MATT_TRACE(0, 2, "no mem");
		return -ENOMEM;
	}
	return matt_transmit_mgmt_skb(sk, skb, 0, flp);
}

int matt_send_pack(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_input_ctx *iq = &ms->input_ctx;
	struct sk_buff *skb;

	matt_mstamp_refresh(sk);
	skb = matt_make_pack(sk);
	if (!skb) {
#line 981
		MATT_TRACE(0, 2, "no mem");
		return -ENOMEM;
	}
	iq->rcv_wup = iq->rcv_nxt;
	iq->pack_time = ms->pkt_mgmt_ctx.matt_mstamp;
	iq->pack_send++;
	return matt_transmit_mgmt_skb(sk, skb, 0, &ms->fl);
}

int matt_send_nack(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_input_ctx *iq = &ms->input_ctx;
	struct sk_buff *skb = matt_make_nack(sk);

	if (!skb) {
#line 1010
		MATT_TRACE(0, 2, "no mem");
		return -ENOMEM;
	}
	iq->rcv_wup = iq->rcv_nxt;
	iq->nack_time = ms->pkt_mgmt_ctx.matt_mstamp;
	iq->nack_send++;
	return matt_transmit_mgmt_skb(sk, skb, 0, &ms->fl);
}

int matt_send_eack(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_input_ctx *iq = &ms->input_ctx;
	struct sk_buff *skb = matt_make_nack(sk);

	if (!skb) {
#line 1025
		MATT_TRACE(0, 2, "no mem");
		return -ENOMEM;
	}
	iq->rcv_wup = iq->rcv_nxt;
	iq->eack_time = ms->pkt_mgmt_ctx.matt_mstamp;
	iq->eack_send++;
	return matt_transmit_mgmt_skb(sk, skb, 0, &ms->fl);
}

void matt_send_dup_pack(struct sock *sk)
{
	int i;

	if (sk->sk_state == TCP_CLOSE)
		return;
	matt_mstamp_refresh(sk);
	for (i = 0; i < 5; i++)
		matt_send_pack(sk);
}

void matt_send_dup_nack(struct sock *sk)
{
	int i;

	if (sk->sk_state == TCP_CLOSE)
		return;
	matt_mstamp_refresh(sk);
	for (i = 0; i < 3; i++)
		matt_send_nack(sk);
}
