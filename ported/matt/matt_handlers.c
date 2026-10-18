// SPDX-License-Identifier: GPL-2.0
/* matt timer handlers (stock: mp_timer.c): pack / nack / reo. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

void matt_rack_detect_loss(struct sock *sk, u32 *reo_timeout);
void matt_rearm_rto(struct sock *sk);
void matt_xmit_retransmit_queue(struct sock *sk);

static inline u32 matt_packets_in_flight(struct matt_output_ctx *oq)
{
	return oq->packets_out - (oq->sacked_out + oq->lost_out) + oq->retrans_out;
}

void matt_pack_timer_handler(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_input_ctx *ic = &ms->input_ctx;
	u32 timeout;

	if (sk->sk_state == TCP_CLOSE)
		return;
	matt_send_pack(sk);
	if (!(before(ic->rcv_wup, ic->rcv_nxt) || !RB_EMPTY_ROOT(&ic->out_of_order_queue)))
		return;
	timeout = ic->rcv_period ?: 20000;
	if (!mod_timer(&ms->timer_ctx.ptimer.timer, jiffies + usecs_to_jiffies(timeout)))
		sock_hold(sk);
#line 94
	MATT_TRACE(0, 8, "rearm pack rcv_nxt=%u rcv_wup=%u timeout=%u", ic->rcv_nxt, timeout);	/* G1ad: the stock passes only 2 values for the 3 conversions */
}

void matt_nack_timer_handler(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);

	if (sk->sk_state == TCP_CLOSE)
		return;
	if (!ms->input_ctx.num_sacks)
		return;
	matt_send_dup_nack(sk);
	if (!mod_timer(&ms->timer_ctx.ntimer.timer, jiffies + usecs_to_jiffies(20000)))
		sock_hold(sk);
}

void matt_reo_timer_handler(struct sock *sk)
{
	struct matt_output_ctx *oq = &MATT_SK(sk)->output_ctx;
	u32 reo_timeout = 0;
	u32 prior_inflight = matt_packets_in_flight(oq);

	matt_rack_detect_loss(sk, &reo_timeout);
	if (prior_inflight != matt_packets_in_flight(oq)) {
#line 119
		MATT_TRACE(0, 8, "timeout: sk: %p prior_inflight:%u matt_packets_in_flight: %u", sk, prior_inflight,
			   matt_packets_in_flight(oq));
		matt_xmit_retransmit_queue(sk);
	}
	matt_rearm_rto(sk);
}
