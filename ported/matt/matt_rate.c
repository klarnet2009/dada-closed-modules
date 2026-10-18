// SPDX-License-Identifier: GPL-2.0
/* matt delivery-rate sampling (stock: mp_rx.c/mp_tx.c area; derived from net/ipv4/tcp_rate.c) and small state helpers. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

void matt_rate_skb_sent(struct sock *sk, struct sk_buff *skb)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;

	if (!outq->packets_out) {
		u64 tstamp_us = div_u64(skb->skb_mstamp_ns, NSEC_PER_USEC);

		outq->first_tx_mstamp = tstamp_us;
		outq->delivered_mstamp = tstamp_us;
	}
	MATT_SKB_CB(skb)->tx.first_tx_mstamp = outq->first_tx_mstamp;
	MATT_SKB_CB(skb)->tx.delivered_mstamp = outq->delivered_mstamp;
	MATT_SKB_CB(skb)->tx.delivered = outq->delivered;
	MATT_SKB_CB(skb)->tx.is_app_limited = outq->app_limited ? 1 : 0;
}

void matt_rate_skb_delivered(struct sock *sk, struct sk_buff *skb, struct rate_sample *rs)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	struct matt_skb_cb *scb = MATT_SKB_CB(skb);
	u64 tx_tstamp;

	if (!scb->tx.delivered_mstamp)
		return;
	tx_tstamp = div_u64(skb->skb_mstamp_ns, NSEC_PER_USEC);
	if (!rs->prior_delivered || tx_tstamp > outq->first_tx_mstamp ||
	    (tx_tstamp == outq->first_tx_mstamp && after(scb->offset_end, rs->last_end_seq))) {
		rs->prior_delivered_ce = scb->tx.delivered_ce;
		rs->prior_delivered = scb->tx.delivered;
		rs->prior_mstamp = scb->tx.delivered_mstamp;
		rs->is_app_limited = scb->tx.is_app_limited;
		rs->is_retrans = scb->sacked & (0x02 | 0x80);
		rs->last_end_seq = scb->offset_end;
		outq->first_tx_mstamp = tx_tstamp;
		rs->interval_us = max_t(s64, (s64)(u32)tx_tstamp - (s64)scb->tx.first_tx_mstamp, 0);	/* G1ad: 64-bit difference clamped at 0 [and x8,#0xffffffff; sub x8; bic asr #63]; G1ac had a wrapping u32 difference */
#line 106
		MATT_TRACE(0, 9, "Prior delivered: %u cnt, Prior delivered mstamp: %u skb(%u, %u)", rs->prior_delivered,
			   rs->prior_mstamp, scb->offset_start, scb->offset_end);
	}
	if (scb->sacked & 0x01)
		scb->tx.delivered_mstamp = 0;
}

void matt_rate_gen(struct sock *sk, u32 delivered, u32 lost, struct rate_sample *rs)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_output_ctx *outq = &ms->output_ctx;
	u32 snd_us, ack_us;

	if (outq->app_limited && after(outq->delivered, outq->app_limited))
		outq->app_limited = 0;
	if (delivered)
		outq->delivered_mstamp = ms->pkt_mgmt_ctx.matt_mstamp;
	rs->acked_sacked = delivered;
	rs->losses = lost;
	if (!rs->prior_mstamp) {
		rs->delivered = -1;
		rs->interval_us = -1;
		return;
	}
	rs->delivered = outq->delivered - rs->prior_delivered;
	rs->delivered_ce = (outq->delivered_ce - rs->prior_delivered_ce) & 0xfffff;
	snd_us = rs->interval_us;
	ack_us = max_t(s64, ms->pkt_mgmt_ctx.matt_mstamp - rs->prior_mstamp, 0);
	rs->snd_interval_us = snd_us;
	rs->rcv_interval_us = ack_us;
	rs->interval_us = max(snd_us, ack_us);
	if (rs->interval_us < outq->min_rtt) {
#line 187
		MATT_TRACE(0, 9, "Interval Too Small, Deliver Interval: %u us, MinRtt: %u", (u32)rs->interval_us, outq->min_rtt);
		rs->interval_us = -1;
		return;
	}
#line 194
	MATT_TRACE(0, 9, "ack: %u cnt, lost: %u cnt, delivered: %u cnt(%u, %u), interval: %u us(%u, %u)", delivered, lost,
		   rs->delivered, outq->delivered, rs->prior_delivered, (u32)rs->interval_us, snd_us, ack_us);
	if (!rs->is_app_limited ||
	    (s64)rs->delivered * outq->rate_interval_us >= (u64)rs->interval_us * outq->rate_delivered) {
		outq->rate_delivered = rs->delivered;
		outq->rate_interval_us = rs->interval_us;
		outq->rate_app_limited = rs->is_app_limited;
#line 204
		MATT_TRACE(0, 9, "Delivered rate: %u, Interval: %u us, App limited: %u", rs->delivered,
			   (u32)rs->interval_us, rs->is_app_limited);
	}
}

void matt_rate_check_app_limited(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_output_ctx *outq = &ms->output_ctx;
	u32 in_flight;

	if (outq->write_seq - outq->snd_nxt < ms->mss &&
	    refcount_read(&sk->sk_wmem_alloc) - 1 < SKB_TRUESIZE(1) &&
	    outq->lost_out <= outq->retrans_out) {
		in_flight = outq->packets_out - (outq->sacked_out + outq->lost_out) + outq->retrans_out;
		if (in_flight < outq->snd_cwnd) {
			outq->app_limited = (outq->delivered + in_flight) ?: 1;
#line 224
			MATT_TRACE(0, 9, "App limited: %u", outq->rate_app_limited);
		}
	}
}

/* ---- small helpers ---- */
void matt_mgmt_init(struct sock *sk)
{
	struct matt_mgmt *m = &MATT_SK(sk)->pkt_mgmt_ctx;

	m->next_pn = 1;
	m->local_packet_num = 996;
	m->local_start_offset = 7;
	m->peer_packet_num = 0;
	m->peer_start_offset = 0;
	m->creq_retry_cnt = 0;
	m->idle_check = 0;
}

void matt_mstamp_refresh(struct sock *sk)
{
	struct matt_mgmt *m = &MATT_SK(sk)->pkt_mgmt_ctx;
	u64 now = ktime_get();

	m->matt_clock_cache = now;
	m->matt_mstamp = div_u64(now, NSEC_PER_USEC);
}

void matt_reset_cur_rtt(struct sock *sk)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;

	outq->cur_rtt = 0;
	outq->ack_delay = 0;
}

void matt_update_cur_rtt(struct sock *sk, u32 ts, u32 ack_delay)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_output_ctx *outq = &ms->output_ctx;
	u32 now = ms->pkt_mgmt_ctx.matt_mstamp;
	u32 rtt;

	if (ts) {
		rtt = now - (ts + ack_delay);
		/* G1ad: the stock compares UNSIGNED now > ts + ack_delay [subs + b.ls], not rtt != 0 */
		if (now > ts + ack_delay) {
			outq->cur_rtt = rtt;
			outq->ack_delay = ack_delay;
#line 124
			MATT_TRACE(0, 9, "cur_ts:%u snd_ts:%u delay:%u rtt:%u", now, ts, ack_delay, rtt);
			return;
		}
	}
	outq->cur_rtt = 0;
	outq->ack_delay = 0;
}

void matt_check_space(struct sock *sk)
{
	smp_mb();
	if (sk->sk_socket && test_bit(SOCK_NOSPACE, &sk->sk_socket->flags))
		sk->sk_write_space(sk);
}
