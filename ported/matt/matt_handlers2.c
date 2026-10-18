// SPDX-License-Identifier: GPL-2.0
/* matt timer handlers (stock: mp_timer.c): write (creq retry), rto, ping. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

int matt_retransmit_skb(struct sock *sk, struct sk_buff *skb, int segs);
int matt_send_ping(struct sock *sk);

void matt_write_timer_handler(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct sk_buff *skb;

	if (sk->sk_state == MATT_SS_CLOSED)
		return;
	if (ms->pkt_mgmt_ctx.creq_retry_cnt == 5) {
#line 168
		MATT_TRACE(0, 2, "sk:%p idle timeout", sk);
		matt_set_state(sk, MATT_SS_CLOSED);
		if (timer_delete(&ms->timer_ctx.rtimer.timer))
			sock_put(sk);
		return;
	}
	skb = matt_make_creq(sk);
	if (skb) {
#line 180
		MATT_TRACE(0, 4, "sk:%p retry creq", sk);
		matt_transmit_mgmt_skb(sk, skb, 0, &ms->fl);
		ms->pkt_mgmt_ctx.creq_retry_cnt++;
	} else {
#line 176
		MATT_TRACE(0, 2, "no mem");
	}
	if (!mod_timer(&ms->timer_ctx.rtimer.timer, jiffies + usecs_to_jiffies(200000)))
		sock_hold(sk);
}

void matt_rto_timer_handler(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_output_ctx *out = &ms->output_ctx;
	struct rb_node *p;
	struct sk_buff *skb;

	if (sk->sk_state == MATT_SS_CLOSED)
		return;
	if (!out->packets_out)
		return;
	p = rb_first(&out->rb_rtx_queue);
	if (WARN_ON_ONCE(!p))	/* G1ad: brk #0x800 in the stock */
		return;
	skb = rb_to_skb(p);
	if (before(MATT_SKB_CB(skb)->offset_start, out->snd_una)) {
#line 145
		MATT_TRACE(0, 2, "why? sk: %p retrans seq: %u num: %u snd_una: %u", sk, MATT_SKB_CB(skb)->offset_start,
			   ms->pkt_mgmt_ctx.next_pn, out->snd_una);
	} else {
		matt_mark_skb_lost(sk, skb);
		out->send[STATS_RTO].data_num += max_t(u16, MATT_SKB_CB(skb)->gso_segs, 1);
		out->send[STATS_RTO].data_bytes += skb->len - 16;
		matt_retransmit_skb(sk, skb, 1);
#line 151
		MATT_TRACE(0, 9, "timeout: sk: %p retrans seq: %u num: %u snd_una: %u", sk, MATT_SKB_CB(skb)->offset_start,
			   ms->pkt_mgmt_ctx.next_pn, out->snd_una);
	}
	if (out->rto) {
		if (!mod_timer(&ms->timer_ctx.rtotimer.timer, jiffies + usecs_to_jiffies(out->rto)))
			sock_hold(sk);
	}
}

void matt_ping_timer_handler(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);

	if (sk->sk_state == MATT_SS_CLOSED)
		return;
	if (judgement_for_rst(sk)) {
#line 69
		MATT_TRACE(0, 8, "idle timeout");
		matt_set_state(sk, MATT_SS_CLOSED);
		if (timer_delete(&ms->timer_ctx.pintimer.timer))
			sock_put(sk);
		return;
	}
	matt_send_ping(sk);
	if (!mod_timer(&ms->timer_ctx.pintimer.timer, jiffies + usecs_to_jiffies(500000)))
		sock_hold(sk);
}
