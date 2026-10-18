/* SPDX-License-Identifier: GPL-2.0 */
/* mibbr_main: bbr_main of tcp_bbr.c with the Xiaomi additions, everything inlined like in the stock.
 * Included at the end of matt_bbr.c (needs its static helpers). Reconstructed from the OS4 stock disassembly (goal-G1y). */
extern u32 (*xm_minmax_running_max)(struct minmax *m, u32 win, u32 t, u32 meas);
extern int bbr_enable_debug, bbr_min_rtt_win_factor, bbr_probe_rtt_cwnd_factor;

static const int bbr_cwnd_gain = BBR_UNIT * 2;
static const int bbr_drain_gain = BBR_UNIT * 1000 / 2885;
static const int bbr_pacing_gain[CYCLE_LEN] = {
	BBR_UNIT * 5 / 4, BBR_UNIT * 3 / 4, BBR_UNIT, BBR_UNIT, BBR_UNIT, BBR_UNIT, BBR_UNIT, BBR_UNIT
};
static const u32 bbr_cwnd_min_target = 4;

static inline u32 matt_packets_in_flight(struct matt_output_ctx *outq)
{
	return outq->packets_out - (outq->sacked_out + outq->lost_out) + outq->retrans_out;
}

/* mibbr_main keeps its own copy of bbr_set_pacing_rate with the inlined bbr_init_pacing_rate_from_rtt (stock) */
static void bbr_set_pacing_rate_main(struct sock *sk, u32 bw, int gain)
{
	struct bbr *bbr = BBR(sk);
	u64 rate = bbr_bw_to_pacing_rate(sk, bw, gain);

	if (unlikely(!bbr->has_seen_rtt && OUTQ(sk)->srtt))
		__bbr_init_pacing_rate_from_rtt(sk);
	if (bbr->full_bw_reached || rate > READ_ONCE(sk->sk_pacing_rate))
		WRITE_ONCE(sk->sk_pacing_rate, rate);
}

static void bbr_update_bw(struct sock *sk, const struct rate_sample *rs)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);
	u64 bw;

	bbr->round_start = 0;
	if (rs->delivered < 0 || rs->interval_us <= 0)
		return;
	if (!before(rs->prior_delivered, bbr->next_rtt_delivered)) {
		bbr->next_rtt_delivered = outq->delivered;
		bbr->rtt_cnt++;
		bbr->round_start = 1;
		bbr->packet_conservation = 0;
	}
	bbr_lt_bw_sampling(sk, rs);
	bw = div64_long((u64)rs->delivered * BW_UNIT, rs->interval_us);
	if (bbr_enable_debug) {
#line 907
		MATT_TRACE(0, 4, "sk=%p cnt=%u start=%u max_bw=%u bw=%llu pdeli=%u deli=%u tp_deli=%u inter_us=%ld", sk, bbr->rtt_cnt, bbr->round_start, bbr_max_bw(sk), bw, rs->prior_delivered, rs->delivered, outq->delivered, rs->interval_us);
	}
	if (!rs->is_app_limited || bw >= bbr_max_bw(sk))
		xm_minmax_running_max(&bbr->bw, CYCLE_LEN + 2, bbr->rtt_cnt, bw);
}

static void bbr_update_ack_aggregation(struct sock *sk, const struct rate_sample *rs)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);
	u32 epoch_us, expected_acked, extra_acked;

	if (!rs->acked_sacked || rs->delivered < 0 || rs->interval_us <= 0)
		return;
	if (bbr->round_start) {
		bbr->extra_acked_win_rtts = min(0x1FU, bbr->extra_acked_win_rtts + 1);
		if (bbr->extra_acked_win_rtts >= 5) {
			bbr->extra_acked_win_rtts = 0;
			bbr->extra_acked_win_idx = bbr->extra_acked_win_idx ? 0 : 1;
			bbr->extra_acked[bbr->extra_acked_win_idx] = 0;
		}
	}
	epoch_us = max_t(s64, (s64)outq->delivered_mstamp - (s64)bbr->ack_epoch_mstamp, 0);
	expected_acked = ((u64)bbr_bw(sk) * epoch_us) >> BW_SCALE;
	if (bbr->ack_epoch_acked <= expected_acked || (bbr->ack_epoch_acked + rs->acked_sacked >= (1U << 20))) {
		bbr->ack_epoch_acked = 0;
		bbr->ack_epoch_mstamp = outq->delivered_mstamp;
		expected_acked = 0;
	}
	bbr->ack_epoch_acked = min_t(u32, 0xFFFFF, bbr->ack_epoch_acked + rs->acked_sacked);
	extra_acked = bbr->ack_epoch_acked - expected_acked;
	extra_acked = min(extra_acked, outq->snd_cwnd);
	if (extra_acked > bbr->extra_acked[bbr->extra_acked_win_idx])
		bbr->extra_acked[bbr->extra_acked_win_idx] = extra_acked;
}

static u32 bbr_packets_in_net_at_edt(struct sock *sk, u32 inflight_now)
{
	struct bbr *bbr = BBR(sk);
	struct matt_mgmt *mgmt = &MATT_SK(sk)->pkt_mgmt_ctx;
	u64 now_ns, edt_ns, interval_us;
	u32 interval_delivered, inflight_at_edt;

	now_ns = mgmt->matt_clock_cache;
	edt_ns = max(mgmt->matt_wstamp_ns, now_ns);
	interval_us = div_u64(edt_ns - now_ns, NSEC_PER_USEC);
	interval_delivered = (u64)bbr_bw(sk) * interval_us >> BW_SCALE;
	inflight_at_edt = inflight_now;
	if (bbr->pacing_gain > BBR_UNIT)
		inflight_at_edt += bbr_tso_segs_goal(sk);
	if (interval_delivered >= inflight_at_edt)
		return 0;
	return inflight_at_edt - interval_delivered;
}

static bool bbr_is_next_cycle_phase(struct sock *sk, const struct rate_sample *rs)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);
	u32 delta = max_t(s64, (s64)outq->delivered_mstamp - (s64)bbr->cycle_mstamp, 0);
	bool is_full_length = delta > bbr->min_rtt_us;
	u32 inflight, bw;

	if (bbr->pacing_gain == BBR_UNIT)
		return is_full_length;
	inflight = bbr_packets_in_net_at_edt(sk, rs->prior_in_flight);
	bw = bbr_max_bw(sk);
	if (bbr->pacing_gain > BBR_UNIT)
		return is_full_length && (rs->losses || inflight >= bbr_inflight(sk, bw, bbr->pacing_gain));
	return is_full_length || inflight <= bbr_inflight(sk, bw, BBR_UNIT);
}

static void bbr_check_full_bw_reached(struct sock *sk, const struct rate_sample *rs)
{
	struct bbr *bbr = BBR(sk);

	if (bbr->full_bw_reached || !bbr->round_start || rs->is_app_limited)
		return;
	if (bbr_max_bw(sk) >= bbr->full_bw * 2) {
		bbr->full_bw = bbr_max_bw(sk);
		bbr->full_bw_cnt = 0;
		return;
	}
	++bbr->full_bw_cnt;
	bbr->full_bw_reached = bbr->full_bw_cnt >= 3;
}

static void bbr_check_drain(struct sock *sk, const struct rate_sample *rs)
{
	struct bbr *bbr = BBR(sk);

	if (bbr->mode == BBR_STARTUP && bbr->full_bw_reached) {
		bbr->mode = BBR_DRAIN;
		OUTQ(sk)->snd_ssthresh = bbr_inflight(sk, bbr_max_bw(sk), BBR_UNIT);
	}
	if (bbr->mode == BBR_DRAIN &&
	    bbr_packets_in_net_at_edt(sk, matt_packets_in_flight(OUTQ(sk))) <= bbr_inflight(sk, bbr_max_bw(sk), BBR_UNIT))
		bbr_reset_probe_bw_mode(sk);
}

static void bbr_update_min_rtt(struct sock *sk, const struct rate_sample *rs)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);
	bool filter_expired;

	filter_expired = after((u32)jiffies, bbr->min_rtt_stamp +
			       ((bbr_enable_opt && bbr_min_rtt_win_factor) ? 2500U * (u32)bbr_min_rtt_win_factor / 10 : 2500));
	if (rs->rtt_us >= 0 && (rs->rtt_us < bbr->min_rtt_us || !bbr->min_rtt_us || (filter_expired && !rs->is_ack_delayed))) {
		bbr->min_rtt_us = rs->rtt_us;
		bbr->min_rtt_stamp = jiffies;
	}
	if (filter_expired && !bbr->idle_restart && bbr->mode != BBR_PROBE_RTT) {
		bbr->mode = BBR_PROBE_RTT;
		bbr_save_cwnd(sk);
		bbr->probe_rtt_done_stamp = 0;
	}
	if (bbr->mode == BBR_PROBE_RTT) {
		outq->app_limited = (outq->delivered + matt_packets_in_flight(outq)) ? : 1;
		if (!bbr->probe_rtt_done_stamp && matt_packets_in_flight(outq) <= bbr_cwnd_min_target) {
			bbr->probe_rtt_done_stamp = (u32)jiffies + 50;
			bbr->probe_rtt_round_done = 0;
			bbr->next_rtt_delivered = outq->delivered;
		} else if (bbr->probe_rtt_done_stamp) {
			if (bbr->round_start)
				bbr->probe_rtt_round_done = 1;
			if (bbr->probe_rtt_round_done) {
				if (bbr->probe_rtt_done_stamp && after((u32)jiffies, bbr->probe_rtt_done_stamp)) {
					bbr->min_rtt_stamp = jiffies;
					matt_snd_cwnd_set(outq, max(outq->snd_cwnd, bbr->prior_cwnd));
					bbr_reset_mode(sk);
				}
			}
		}
	}
	if (rs->delivered > 0)
		bbr->idle_restart = 0;
}

static void bbr_update_gains(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);

	switch (bbr->mode) {
	case BBR_STARTUP:
		bbr->pacing_gain = bbr_high_gain;
		bbr->cwnd_gain = bbr_high_gain;
		break;
	case BBR_DRAIN:
		bbr->pacing_gain = bbr_drain_gain;
		bbr->cwnd_gain = bbr_high_gain;
		break;
	case BBR_PROBE_BW:
		bbr->pacing_gain = (bbr->lt_use_bw ? BBR_UNIT : bbr_pacing_gain[bbr->cycle_idx]);
		bbr->cwnd_gain = bbr_cwnd_gain;
		break;
	case BBR_PROBE_RTT:
		bbr->pacing_gain = BBR_UNIT;
		bbr->cwnd_gain = BBR_UNIT;
		break;
	default:
		WARN_ONCE(1, "BBR bad mode: %u\n", bbr->mode);
		break;
	}
}

static u32 bbr_quantization_budget(struct sock *sk, u32 cwnd)
{
	struct bbr *bbr = BBR(sk);

	cwnd += 3 * bbr_tso_segs_goal(sk);
	cwnd = (cwnd + 1) & ~1U;
	if (bbr->mode == BBR_PROBE_BW && bbr->cycle_idx == 0)
		cwnd += 2;
	return cwnd;
}

static u32 bbr_ack_aggregation_cwnd(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);
	u32 max_aggr_cwnd, aggr_cwnd = 0;

	if (bbr->full_bw_reached) {
		max_aggr_cwnd = ((u64)bbr_bw(sk) * 100000) / BW_UNIT;
		aggr_cwnd = (BBR_UNIT * max(bbr->extra_acked[0], bbr->extra_acked[1])) >> BBR_SCALE;
		aggr_cwnd = min(aggr_cwnd, max_aggr_cwnd);
	}
	return aggr_cwnd;
}

static void bbr_set_cwnd(struct sock *sk, const struct rate_sample *rs, u32 acked, u32 bw, int gain)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);
	u32 cwnd = outq->snd_cwnd, target_cwnd = 0;

	if (!acked)
		goto done;
	{
		u8 prev_state = bbr->prev_ca_state, state = MATT_SK(sk)->cc_ctx.ca_state;

		if (rs->losses > 0)
			cwnd = max_t(s32, cwnd - rs->losses, 1);
		if (state == MATT_CA_Recovery && prev_state != MATT_CA_Recovery) {
			bbr->packet_conservation = 1;
			bbr->next_rtt_delivered = outq->delivered;
			cwnd = matt_packets_in_flight(outq) + acked;
		} else if (prev_state >= MATT_CA_Recovery && state < MATT_CA_Recovery) {
			cwnd = max(cwnd, bbr->prior_cwnd);
			bbr->packet_conservation = 0;
		}
		bbr->prev_ca_state = state;
		if (bbr->packet_conservation) {
			cwnd = max(cwnd, matt_packets_in_flight(outq) + acked);
			goto done;
		}
	}
	target_cwnd = bbr_bdp(sk, bw, gain);
	target_cwnd += bbr_ack_aggregation_cwnd(sk);
	target_cwnd = bbr_quantization_budget(sk, target_cwnd);
	if (bbr->full_bw_reached) {
		cwnd = min(cwnd + acked, target_cwnd);
#line 651
		MATT_TRACE(0, 9, "sk=%p full_bw_reached, target_cwnd=%d cwnd=%d bw=%u una=%u nxt=%u", sk, target_cwnd, cwnd, bw, outq->snd_una, outq->snd_nxt);
	} else if (cwnd < target_cwnd || outq->delivered < 10) {
#line 654
		MATT_TRACE(0, 9, "sk=%p, target_cwnd=%d cwnd=%d bw=%u una=%u nxt=%u", sk, target_cwnd, cwnd, bw, outq->snd_una, outq->snd_nxt);
		cwnd = cwnd + acked;
	}
	cwnd = max(cwnd, bbr_cwnd_min_target);
done:
	matt_snd_cwnd_set(outq, min(cwnd, outq->snd_cwnd_clamp));
	if (bbr->mode == BBR_PROBE_RTT) {
		if (bbr_enable_opt && bbr_probe_rtt_cwnd_factor)
			matt_snd_cwnd_set(outq, max(outq->snd_cwnd * (u32)bbr_probe_rtt_cwnd_factor / 10, bbr_cwnd_min_target));
		else
			matt_snd_cwnd_set(outq, min(outq->snd_cwnd, bbr_cwnd_min_target));
	}
}

void mibbr_main(struct sock *sk, const struct rate_sample *rs)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);
	u32 bw;

	bbr_update_bw(sk, rs);
	bbr_update_ack_aggregation(sk, rs);
	if (bbr->mode == BBR_PROBE_BW && bbr_is_next_cycle_phase(sk, rs))
		bbr_advance_cycle_phase(sk);
	bbr_check_full_bw_reached(sk, rs);
	bbr_check_drain(sk, rs);
	bbr_update_min_rtt(sk, rs);
	bbr_update_gains(sk);
	bw = bbr_bw(sk);
	bbr_set_pacing_rate_main(sk, bw, bbr->pacing_gain);
	bbr_set_cwnd(sk, rs, rs->acked_sacked, bw, bbr->cwnd_gain);
	if (bbr_enable_debug) {
#line 1172
		MATT_TRACE(0, 4, "sk=%p bw=%u pacing_gain=%u acked_sacked=%u cwnd_gain=%u wmalc=%u", sk, bw, bbr->pacing_gain, rs->acked_sacked, bbr->cwnd_gain, refcount_read(&sk->sk_wmem_alloc));
#line 1175
		MATT_TRACE(0, 4, "sk=%p mode=%u una=%u nxt=%u wseq=%u cwnd=%u inflight=%u srtt=%u mrtt=%u pacing=%lu", sk, bbr->mode, outq->snd_una, outq->snd_nxt, outq->write_seq, outq->snd_cwnd, matt_packets_in_flight(outq), outq->srtt, bbr->min_rtt_us, READ_ONCE(sk->sk_pacing_rate));
	}
}
