// SPDX-License-Identifier: GPL-2.0
/* matt loss marking / RACK / RTO re-arm (stock: mp_recovery.c; modelled on tcp_recovery.c and tcp_input.c) */
#include "matt.h"

s32 matt_rack_skb_timeout(struct sock *sk, struct sk_buff *skb, u32 reo_wnd);
extern int (*xm_pskb_expand_head)(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp_mask);

#define STATS_ADD(out, idx, skb, segs)					\
	do {								\
		(out)->send[idx].data_num += (segs);			\
		(out)->send[idx].data_bytes += (skb)->len - 16;		\
	} while (0)

void matt_mark_skb_lost(struct sock *sk, struct sk_buff *skb)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);
	u8 sacked = cb->sacked;

	if (sacked & 0x01) {
#line 1158
		MATT_TRACE(0, 9, "skb:[%u-->%u]", cb->offset_start, cb->offset_end);
		return;
	}
	if (!(sacked & 0x04)) {
		out->lost_out += cb->gso_segs;
		cb->sacked |= 0x04;
		STATS_ADD(out, STATS_LOST, skb, max_t(u16, cb->gso_segs, 1));
#line 1178
		MATT_TRACE(0, 9, "lost:[%u-->%u]", cb->offset_start, cb->offset_end);
	} else if (sacked & 0x02) {
		cb->sacked &= ~0x02;
		out->retrans_out -= cb->gso_segs;
		STATS_ADD(out, STATS_RELOST, skb, max_t(u16, cb->gso_segs, 1));
#line 1170
		MATT_TRACE(0, 9, "lost again:[%u-->%u]", cb->offset_start, cb->offset_end);
	} else {
		goto out;
	}
	out->lost += cb->gso_segs;
out:
#line 1184
	MATT_TRACE(0, 8, "seq: %d num: %d", cb->offset_start, cb->num);
}

bool matt_rack_mark_lost(struct sock *sk);

/* tcp_skb_sent_after() */
static inline bool matt_skb_sent_after(u64 t1, u64 t2, u32 seq1, u32 seq2)
{
	return t1 > t2 || (t1 == t2 && after(seq1, seq2));
}

void matt_rack_detect_loss(struct sock *sk, u32 *reo_timeout)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	struct matt_mgmt *mgmt = &MATT_SK(sk)->pkt_mgmt_ctx;
	struct sk_buff *skb, *n;
	u32 reo_wnd;

	*reo_timeout = 0;
	reo_wnd = out->srtt * out->rack.reo_wnd_steps;
	list_for_each_entry_safe(skb, n, &out->tsorted_tx_queue, tcp_tsorted_anchor) {
		struct matt_skb_cb *cb = MATT_SKB_CB(skb);
		u64 skb_us = skb->skb_mstamp_ns / NSEC_PER_USEC;
		s64 delta;
		s32 remaining;

		if ((cb->sacked & 0x06) == 0x04)
			continue;
		if (!matt_skb_sent_after(out->rack.mstamp, skb_us, out->rack.end_seq, cb->offset_end))
			break;
		delta = mgmt->matt_mstamp - skb_us;
		if (delta < 0)
			delta = 0;
		remaining = (u32)(out->rack.rtt_us + reo_wnd) - (u32)delta;
		if (remaining <= 0) {
			STATS_ADD(out, STATS_RACK_LOST, skb, max_t(u16, cb->gso_segs, 1));
			matt_mark_skb_lost(sk, skb);
			list_del_init(&skb->tcp_tsorted_anchor);
		} else {
			*reo_timeout = max_t(u32, *reo_timeout, remaining);
#line 1250
			MATT_TRACE(0, 9, "seq: %u num: %u reo_timeout: %d delta: %d", cb->offset_start, cb->num, *reo_timeout,
				   (int)max_t(s64, mgmt->matt_mstamp - skb->skb_mstamp_ns / NSEC_PER_USEC, 0));
		}
	}
}

bool matt_rack_mark_lost(struct sock *sk)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	u32 reo_timeout = 0, timeout;

	if (!out->rack.advanced)
		return false;
	out->rack.advanced = 0;
	matt_rack_detect_loss(sk, &reo_timeout);
	if (!reo_timeout)
		return false;
	timeout = min(reo_timeout + 2000, out->rto);
	matt_timer_reset(sk, 4, timeout);
	return timeout != 0;
}

void matt_mark_lost(struct sock *sk, u32 ack_offset)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	u32 reo_timeout = 0;

	if (out->rack.advanced) {
		out->rack.advanced = 0;
		matt_rack_detect_loss(sk, &reo_timeout);
		if (reo_timeout)
			matt_timer_reset(sk, 4, min(reo_timeout + 2000, out->rto));
	}
	WARN_ON(out->sacked_out + out->lost_out > out->packets_out);
}

void matt_mark_lost_now(struct sock *sk, u32 ack_offset)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	struct rb_node *p;

	if (!READ_ONCE(out->rb_rtx_queue.rb_node) || !out->sack_used)
		return;
	for (p = rb_first(&out->rb_rtx_queue); p; p = rb_next(p)) {
		struct sk_buff *skb = rb_to_skb(p);
		struct matt_skb_cb *cb = MATT_SKB_CB(skb);

		if (before(out->first_sackoffset, cb->offset_start))
			break;
		STATS_ADD(out, STATS_NACK_LOST, skb, max_t(u16, cb->gso_segs, 1));
		matt_mark_skb_lost(sk, skb);
	}
	WARN_ON(out->sacked_out + out->lost_out > out->packets_out);
}

static inline int matt_rto_delta_us(struct sock *sk)	/* G1ad: 32-bit int in the stock [add w9,w21,w0; sub w0,w9,w8; cmp w0,#1; csinc gt] */
{
	struct matt_sock *msk = MATT_SK(sk);
	struct rb_node *p = rb_first(&msk->output_ctx.rb_rtx_queue);
	u32 rto = msk->output_ctx.rto;

	if (unlikely(!p)) {
#line 650
		MATT_TRACE(0, 2, "rtx queue empty!");
		return jiffies_to_usecs(rto);
	}
	return (u32)(rb_to_skb(p)->skb_mstamp_ns / NSEC_PER_USEC) + (u32)jiffies_to_usecs(rto) - (u32)msk->pkt_mgmt_ctx.matt_mstamp;
}

void matt_rearm_rto(struct sock *sk)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;

	if (unlikely(!out->packets_out)) {
#line 664
		MATT_TRACE(0, 9, "timer stop sk=%p snd_una=%u", sk, out->snd_una);
		matt_timer_stop(sk, 5);
	} else {
		unsigned long rto = usecs_to_jiffies(max_t(int, matt_rto_delta_us(sk), 1));

#line 670
		MATT_TRACE(0, 9, "timer_reset sk=%p snd_una=%u rto=%u", sk, out->snd_una, (u32)rto);
		matt_timer_reset(sk, 5, (u32)rto);
	}
}

