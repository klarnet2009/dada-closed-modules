// SPDX-License-Identifier: GPL-2.0
/* matt small state / recovery helpers (stock: mp_rx.c, mp_tx.c area; rack from net/ipv4/tcp_recovery.c). Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

extern void matt_write_xmit(struct sock *sk, int push_one);
void matt_xmit_retransmit_queue(struct sock *sk);
void matt_outq_free(struct sock *sk);
void matt_reset(struct sock *sk);
int matt_send_nack(struct sock *sk);

void reset_input_params(struct matt_input_ctx *input_ctx)
{
	memset(&input_ctx->trecv, 0, sizeof(input_ctx->trecv));
	memset(input_ctx->dropped, 0, sizeof(input_ctx->dropped));
}

void reset_output_params(struct matt_output_ctx *outq)
{
	memset(&outq->tsend, 0, sizeof(outq->tsend));
	memset(outq->send, 0, sizeof(outq->send));
}

s32 matt_rack_skb_timeout(struct sock *sk, struct sk_buff *skb, u32 reo_wnd)
{
	struct matt_sock *ms = MATT_SK(sk);

	return ms->output_ctx.rack.rtt_us + reo_wnd -
	       max_t(s64, ms->pkt_mgmt_ctx.matt_mstamp - div_u64(skb->skb_mstamp_ns, NSEC_PER_USEC), 0);
}

void matt_update_connect_rtt(struct sock *sk, u32 srtt_us)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;

	outq->srtt = srtt_us;
	outq->min_rtt = srtt_us;
	outq->rto = srtt_us + (srtt_us >> 2);
#line 1321
	MATT_TRACE(0, 8, "update srtt:%u minrtt:%u rto:%u", srtt_us, srtt_us, outq->rto);
}

void matt_packet_get_addrs(struct sock *sk, struct sk_buff *skb)
{
	struct matt_mgmt *m = &MATT_SK(sk)->pkt_mgmt_ctx;

	m->sa = &m->saddr;
	m->da = &m->daddr;
	matt_get_skb_addrs2(skb, m->sa, m->da);
}

void matt_init_nondata_skb(struct sk_buff *skb, u32 seq, MATT_MGMT_TYPE type)
{
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);

	cb->types = type;
	cb->gso_segs = 1;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	cb->offset_start = seq;
	cb->offset_end = seq + (type < 2 ? 1 : 0);
}

void matt_rack_advance(struct sock *sk, u8 sacked, u32 end_seq, u64 xmit_time)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	u32 rtt_us;

	rtt_us = max_t(s64, MATT_SK(sk)->pkt_mgmt_ctx.matt_mstamp - xmit_time, 0);
	if (rtt_us < outq->srtt && (sacked & (0x02 | 0x80)))
		return;
	outq->rack.advanced = 1;
	outq->rack.rtt_us = rtt_us;
	if (xmit_time > outq->rack.mstamp || (xmit_time == outq->rack.mstamp && after(end_seq, outq->rack.end_seq))) {
		outq->rack.mstamp = xmit_time;
		outq->rack.end_seq = end_seq;
	}
}

void matt_reset_with_error(struct sock *sk, int err)
{
	sk->sk_err = err;
	smp_wmb();
	matt_outq_free(sk);
	matt_reset(sk);
	if (!sock_flag(sk, SOCK_DEAD))
		sk_error_report(sk);
}

void matt_rcv_reset(struct sock *sk, struct sk_buff *skb)
{
	if (sk->sk_state == TCP_CLOSE)
		return;
	sk->sk_err = ECONNRESET;
	smp_wmb();
	matt_outq_free(sk);
	matt_reset(sk);
	if (!sock_flag(sk, SOCK_DEAD))
		sk_error_report(sk);
}

void matt_send_ack(struct sock *sk)
{
	matt_send_nack(sk);
}
