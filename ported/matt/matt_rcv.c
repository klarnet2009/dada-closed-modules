// SPDX-License-Identifier: GPL-2.0
/* matt receive path (stock: mp_input.c, derived from net/ipv4/tcp_input.c): rcv_established with the inlined pack/nack/report/update_window.
 * Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

void matt_rcv_data(struct sock *sk, struct sk_buff *skb);
static noinline void matt_sacktag_write_queue(struct sock *sk, struct sk_buff *skb, struct matt_sacktag_state *state);
static noinline void matt_clean_rtx_queue(struct sock *sk, u32 ack_offset, struct matt_sacktag_state *state);
void matt_update_connect_rtt(struct sock *sk, u32 srtt_us);
int matt_send_pong(struct sock *sk, u32 timesatmp);
int matt_send_report(struct sock *sk);

#include "matt_sack.h"

static __always_inline void matt_update_window(struct sock *sk, u32 ack_offset, u32 nwin)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	u32 delta = ack_offset - outq->snd_una;

	if ((s32)delta >= 0) {
#line 886
		MATT_TRACE(0, 9, "una=%u owin=%u ack_offset=%u nwin=%u", outq->snd_una, outq->snd_wnd, ack_offset, nwin);
		outq->snd_wnd = nwin;
		delta = ack_offset - outq->snd_una;
	}
	outq->bytes_acked += delta;
	outq->snd_una = ack_offset;
}

static __always_inline void matt_drop_old_ack(struct sock *sk, struct sk_buff *skb)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	u32 segs = max_t(u16, skb_shinfo(skb)->gso_segs, 1);

	in->dropped[STATS_DROP_OLD_ACK].data_num += segs;
	in->dropped[STATS_DROP_ALL].data_num += segs;
	in->dropped[STATS_DROP_OLD_ACK].data_bytes += skb->len - 16;
	in->dropped[STATS_DROP_ALL].data_bytes += skb->len - 16;
}

static __always_inline void matt_cong_control(struct sock *sk, struct rate_sample *rs)
{
	struct matt_cc_ctx *cc = &MATT_SK(sk)->cc_ctx;

	if (cc->ops && cc->ops->cong_control)
		cc->ops->cong_control(sk, rs);
}

static __always_inline u32 matt_packets_in_flight(struct matt_output_ctx *outq)
{
	return outq->packets_out - (outq->sacked_out + outq->lost_out) + outq->retrans_out;
}

static __always_inline void matt_rcv_pack(struct sock *sk, struct sk_buff *skb)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_output_ctx *outq = &msk->output_ctx;
	struct matt_pack *pack = (struct matt_pack *)skb->data;
	u32 prior_snd_una = outq->snd_una;
	u32 ack_offset = ntohl(pack->hdr.offset);
	u32 sacks_size = pack->sacks_size;
	u32 nwin = ntohl(pack->recv_buffer);
	u32 ts = ntohl(pack->timestamp);
	u32 ack_delay = ntohl(pack->delay_ack);
	struct matt_sacktag_state state = {};
	struct rate_sample rs = {};
	u32 prior_delivered, prior_lost, delivered, lost;

	if (before(ack_offset, prior_snd_una)) {
		matt_drop_old_ack(sk, skb);
	} else {
		prior_delivered = outq->delivered;
		prior_lost = outq->lost;
		rs.prior_in_flight = matt_packets_in_flight(outq);
		state.rate = &rs;
#line 1303
		MATT_TRACE(0, 9, "prior inflight: %d, pout: %d, leftout: %d, sack: %d, lost: %d, retrans: %d", rs.prior_in_flight,
			   outq->packets_out, outq->sacked_out + outq->lost_out, outq->sacked_out, outq->lost_out, outq->retrans_out);
		matt_dump_skb("matt_rcv_pack", skb);
		matt_update_window(sk, ack_offset, nwin);
		matt_sacktag_write_queue(sk, skb, &state);
		if (state.sack_delivered)
			outq->delivered += state.sack_delivered;
		matt_update_cur_rtt(sk, ts, ack_delay);
		matt_clean_rtx_queue(sk, ack_offset, &state);
		matt_mark_lost(sk, ack_offset);
		if (before(prior_snd_una, outq->snd_una))
			matt_rearm_rto(sk);
#line 1330
		MATT_TRACE(0, 9, "cur inflight: %d, pout: %d, leftout: %d, sack: %d, lost: %d, retrans: %d", matt_packets_in_flight(outq),
			   outq->packets_out, outq->sacked_out + outq->lost_out, outq->sacked_out, outq->lost_out, outq->retrans_out);
		delivered = outq->delivered - prior_delivered;
		lost = outq->lost - prior_lost;
#line 1333
		MATT_TRACE(0, 9, "rcv pack: num: %d, ack_seq: %d, cur_rtt: %d, delivered: %d, prior mstamp: %u, prior inflight: %u",
			   ntohl(pack->hdr.num), ack_offset, outq->cur_rtt, delivered, rs.prior_mstamp, rs.prior_in_flight);
		matt_rate_gen(sk, delivered, lost, &rs);
		matt_cong_control(sk, &rs);
		if (sacks_size)
			matt_xmit_retransmit_queue(sk);
		if (outq->write_seq != outq->snd_nxt && before(outq->snd_nxt, outq->snd_una + outq->snd_wnd))
			__matt_push(sk);
	}
#line 1341
	MATT_TRACE(0, 9, "drop old pack, sk=%p num=%u ack_offset=%u snd_una=%u", sk, ntohl(pack->hdr.num), ack_offset, prior_snd_una);
	consume_skb(skb);
	smp_mb();
}

static __always_inline void matt_rcv_nack(struct sock *sk, struct sk_buff *skb)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_output_ctx *outq = &msk->output_ctx;
	struct matt_nack *nack = (struct matt_nack *)skb->data;
	u32 prior_snd_una = outq->snd_una;
	u32 ack_offset = ntohl(nack->hdr.offset);
	u32 nwin = ntohl(nack->recv_buffer);
	u32 sacks_size = nack->sacks_size;
	struct matt_sacktag_state state = {};
	struct rate_sample rs = {};
	u32 prior_delivered, prior_lost;

	if (before(ack_offset, prior_snd_una)) {
		matt_drop_old_ack(sk, skb);
	} else {
		prior_delivered = outq->delivered;
		prior_lost = outq->lost;
		rs.prior_in_flight = matt_packets_in_flight(outq);
		state.rate = &rs;
#line 1369
		MATT_TRACE(0, 9, "prior inflight=%u", rs.prior_in_flight);
		matt_dump_skb("matt_rcv_nack", skb);
		matt_update_window(sk, ack_offset, nwin);
		matt_sacktag_write_queue(sk, skb, &state);
		if (state.sack_delivered)
			outq->delivered += state.sack_delivered;
		outq->cur_rtt = 0;
		outq->ack_delay = 0;
		matt_clean_rtx_queue(sk, ack_offset, &state);
		matt_mark_lost_now(sk, ack_offset);
		if (before(prior_snd_una, outq->snd_una))
			matt_rearm_rto(sk);
		matt_rate_gen(sk, outq->delivered - prior_delivered, outq->lost - prior_lost, &rs);
		matt_cong_control(sk, &rs);
		if (sacks_size)
			matt_xmit_retransmit_queue(sk);
		if (outq->write_seq != outq->snd_nxt && before(outq->snd_nxt, outq->snd_una + outq->snd_wnd))
			__matt_push(sk);
	}
#line 1402
	MATT_TRACE(0, 9, "drop old nack sk=%p num=%u ack_offset=%u snd_una=%u nwin=%u", sk, ntohl(nack->hdr.num), ack_offset,
		   prior_snd_una, nwin);
	consume_skb(skb);
	smp_mb();
}

static __always_inline void matt_report_update(struct sock *sk, struct sk_buff *skb)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	struct matt_report *rp = (struct matt_report *)skb->data;
	u32 rtt = ntohl(rp->rtt);
	u32 limit = ntohl(rp->limit);
	u32 srtt = in->rcv_srtt;
	u32 minrtt = in->rcv_history_minrtt;
	u32 period, qbytes;

	if (srtt)
		srtt = (rtt + srtt * 7) >> 3;
	else
		srtt = rtt;
	in->rcv_srtt = srtt;
	/* G1ad: the stock disassembly [mov w11,w8 on the update path; period and the trace use w11] uses the NEW minimum; G1ac had "fixed" this to the old one */
	if (!minrtt || rtt < minrtt) {
		minrtt = rtt;
		in->rcv_history_minrtt = rtt;
	}
	period = in->rcv_period < minrtt ? srtt >> 2 : minrtt;
	qbytes = limit >> 2;
	in->rcv_period = period;
	in->queue_bytes_trigger = qbytes;
#line 91
	MATT_TRACE(0, 4, "sk:%p rcv report, limit = %u, cwnd = %u, queue_bytes = %d, update srtt = %d, period = %d us, recent min srtt = %d us, history min rtt = %d us",
		   sk, qbytes, (ntohl(rp->cwnd) * MATT_SK(sk)->rcv_mss) >> 2, qbytes, rtt, period, srtt, minrtt);
}

void matt_rcv_established(struct sock *sk, struct sk_buff *skb)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matthdr *mh = (struct matthdr *)skb_transport_header(skb);

	matt_dump_skb("matt_rcv_established", skb);
	matt_mstamp_refresh(sk);
#line 1493
	MATT_TRACE(0, 9, "sk:%p rcv num:%u offset:%u reserved:%u type:%u len:%u rcvmss:%u mss:%u", sk, ntohl(mh->num), ntohl(mh->offset),
		   ntohl(mh->reserved), mh->type, ntohs(mh->len), msk->rcv_mss, msk->mss);
	if ((s8)mh->flags >= 0) {
		MATT_SKB_CB(skb)->rx.peer_tx_mstamp = ntohl(mh->reserved);
		MATT_SKB_CB(skb)->rx.local_rx_mstamp = msk->pkt_mgmt_ctx.matt_mstamp;
		matt_rcv_data(sk, skb);
		return;
	}
	switch (mh->type) {
	case MATT_MGMT_TYPE_CONN_REQ:
		if (matt_established_process_creq(sk, skb)) {
#line 1546
			MATT_TRACE(0, 2, "sk:%p fail to process creq", sk);
		}
		break;
	case MATT_MGMT_TYPE_RESET:
#line 1538
		MATT_TRACE(0, 4, "sk:%p rcv reset, reason code = %d", sk, ntohl(*(__be32 *)((u8 *)mh + 16)));
		if (sk->sk_state != TCP_CLOSE) {
			sk->sk_err = ECONNRESET;
			smp_wmb();
			matt_outq_free(sk);
			matt_reset(sk);
			if (!sock_flag(sk, SOCK_DEAD))
				sk_error_report(sk);
		}
		break;
	case MATT_MGMT_TYPE_PONG: {
		u32 snd_ts = ntohl(*(__be32 *)((u8 *)mh + 16));
		u32 cur_ts;

		msk->pkt_mgmt_ctx.piong_flag = 1;
		cur_ts = (u64)ktime_get() / 1000;
#line 1530
		MATT_TRACE(0, 9, "snd_ts_us=%u cur_ts_us=%u", snd_ts, cur_ts);
		matt_update_connect_rtt(sk, cur_ts - snd_ts);
		break;
	}
	case MATT_MGMT_TYPE_PACK:
		matt_rcv_pack(sk, skb);
		goto check_space;
	case MATT_MGMT_TYPE_NACK:
		matt_rcv_nack(sk, skb);
		goto check_space;
	case MATT_MGMT_TYPE_PING:
		matt_send_pong(sk, ntohl(*(__be32 *)((u8 *)mh + 16)));
		break;
	case MATT_MGMT_TYPE_REPORT:
		matt_report_update(sk, skb);
		break;
	default:
		break;
	}
	consume_skb(skb);
	return;
check_space:
	if (sk->sk_socket && test_bit(SOCK_NOSPACE, &sk->sk_socket->flags))
		sk->sk_write_space(sk);
}
