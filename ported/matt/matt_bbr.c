// SPDX-License-Identifier: GPL-2.0
/* matt BBR + const congestion control (stock: mp_cc.c; derived from net/ipv4/tcp_bbr.c). Reconstructed from the OS4 stock disassembly. mibbr_main is NOT written yet. DO NOT LOAD before review. */
#include "matt.h"

#define BW_SCALE 24
#define BW_UNIT (1 << BW_SCALE)
#define BBR_SCALE 8
#define BBR_UNIT (1 << BBR_SCALE)
#define CYCLE_LEN 8

enum bbr_mode { BBR_STARTUP, BBR_DRAIN, BBR_PROBE_BW, BBR_PROBE_RTT };

/* net/ipv4/tcp_bbr.c layout; state lives in matt_sock.cc_ctx.ca_priv (offsets verified against the stock: 0x540..0x5a8) */
struct bbr {
	u32 min_rtt_us;
	u32 min_rtt_stamp;
	u32 probe_rtt_done_stamp;
	struct minmax bw;
	u32 rtt_cnt;
	u32 next_rtt_delivered;
	u64 cycle_mstamp;
	u32 mode : 3, prev_ca_state : 3, packet_conservation : 1, round_start : 1, idle_restart : 1,
	    probe_rtt_round_done : 1, unused : 13, lt_is_sampling : 1, lt_rtt_cnt : 7, lt_use_bw : 1;
	u32 lt_bw;
	u32 lt_last_delivered;
	u32 lt_last_stamp;
	u32 lt_last_lost;
	u32 pacing_gain : 10, cwnd_gain : 10, full_bw_reached : 1, full_bw_cnt : 2, cycle_idx : 3, has_seen_rtt : 1,
	    unused_b : 5;
	u32 prior_cwnd;
	u32 full_bw;
	u64 ack_epoch_mstamp;
	u16 extra_acked[2];
	u32 ack_epoch_acked : 20, extra_acked_win_rtts : 5, extra_acked_win_idx : 1, unused_c : 6;
};

#define BBR(sk) ((struct bbr *)MATT_SK(sk)->cc_ctx.ca_priv)
#define OUTQ(sk) (&MATT_SK(sk)->output_ctx)

extern int bbr_enable_opt, bbr_srtt_factor, bbr_mrtt_factor, bbr_pace_factor, bbr_init_cwnd_opt, bbr_no_pacing;

static const int bbr_high_gain = BBR_UNIT * 2885 / 1000 + 1;
static const int bbr_cycle_rand = 7;
static const int bbr_lt_intvl_min_rtts = 4;
static const int bbr_lt_loss_thresh = 50;
static const int bbr_lt_bw_ratio = BBR_UNIT / 8;
static const int bbr_lt_bw_diff = 4000 / 8;
static const int bbr_lt_bw_max_rtts = 48;
static const u32 bbr_min_tso_rate = 1200000;

static u32 bbr_max_bw(struct sock *sk)
{
	return minmax_get(&BBR(sk)->bw);
}

static u32 bbr_bw(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);

	return bbr->lt_use_bw ? bbr->lt_bw : bbr_max_bw(sk);
}

static u64 bbr_rate_bytes_per_sec(struct sock *sk, u64 rate, int gain)
{
	unsigned int mss = MATT_SK(sk)->mss;

	rate *= mss;
	rate *= gain;
	rate >>= BBR_SCALE;
	rate *= USEC_PER_SEC / 100 * (100 - 1);
	return rate >> BW_SCALE;
}

static u64 bbr_bw_to_pacing_rate(struct sock *sk, u32 bw, int gain)
{
	u64 rate = bw;

	rate = bbr_rate_bytes_per_sec(sk, rate, gain);
	rate *= (bbr_pace_factor && bbr_enable_opt) ? bbr_pace_factor : 1;
	rate = min_t(u64, rate, READ_ONCE(sk->sk_max_pacing_rate));
	return rate;
}

static __always_inline void __bbr_init_pacing_rate_from_rtt(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);
	u32 rtt_us;
	u64 bw;

	if (OUTQ(sk)->srtt) {
		rtt_us = max(OUTQ(sk)->srtt >> 3, 1U);
		bbr->has_seen_rtt = 1;
	} else {
		rtt_us = USEC_PER_MSEC;
	}
	bw = (u64)OUTQ(sk)->snd_cwnd * BW_UNIT;
	do_div(bw, rtt_us);
	WRITE_ONCE(sk->sk_pacing_rate, bbr_bw_to_pacing_rate(sk, bw, bbr_high_gain));
}

static noinline void bbr_init_pacing_rate_from_rtt(struct sock *sk)
{
	__bbr_init_pacing_rate_from_rtt(sk);
}

static void bbr_set_pacing_rate(struct sock *sk, u32 bw, int gain)
{
	struct bbr *bbr = BBR(sk);
	u64 rate = bbr_bw_to_pacing_rate(sk, bw, gain);

	if (unlikely(!bbr->has_seen_rtt && OUTQ(sk)->srtt))
		bbr_init_pacing_rate_from_rtt(sk);
	if (bbr->full_bw_reached || rate > READ_ONCE(sk->sk_pacing_rate))
		WRITE_ONCE(sk->sk_pacing_rate, rate);
}

u32 bbr_min_tso_segs(struct sock *sk)
{
	return READ_ONCE(sk->sk_pacing_rate) < (bbr_min_tso_rate >> 3) ? 1 : 2;
}

static u32 bbr_tso_segs_goal(struct sock *sk)
{
	u32 segs;
	unsigned long bytes;

	bytes = min_t(unsigned long, READ_ONCE(sk->sk_pacing_rate) >> READ_ONCE(sk->sk_pacing_shift), 65307);
	segs = max_t(u32, bbr_min_tso_segs(sk), bytes / MATT_SK(sk)->mss);
	return min(segs, 0x7FU);
}

static void bbr_save_cwnd(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);

	if (bbr->prev_ca_state < MATT_CA_Recovery && bbr->mode != BBR_PROBE_RTT)
		bbr->prior_cwnd = OUTQ(sk)->snd_cwnd;
	else
		bbr->prior_cwnd = max(bbr->prior_cwnd, OUTQ(sk)->snd_cwnd);
}

static void bbr_reset_lt_bw_sampling_interval(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);

	bbr->lt_last_stamp = div_u64(outq->delivered_mstamp, USEC_PER_MSEC);
	bbr->lt_last_delivered = outq->delivered;
	bbr->lt_last_lost = outq->lost;
	bbr->lt_rtt_cnt = 0;
}

static void bbr_reset_lt_bw_sampling(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);

	bbr->lt_bw = 0;
	bbr->lt_use_bw = 0;
	bbr->lt_is_sampling = false;
	bbr_reset_lt_bw_sampling_interval(sk);
}

static void bbr_reset_startup_mode(struct sock *sk)
{
	BBR(sk)->mode = BBR_STARTUP;
}

static void bbr_advance_cycle_phase(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);

	bbr->cycle_idx = (bbr->cycle_idx + 1) & (CYCLE_LEN - 1);
	bbr->cycle_mstamp = OUTQ(sk)->delivered_mstamp;
}

static void bbr_reset_probe_bw_mode(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);

	bbr->mode = BBR_PROBE_BW;
	bbr->cycle_idx = CYCLE_LEN - 1 - get_random_u32_below(bbr_cycle_rand);
	bbr_advance_cycle_phase(sk);
}

static void bbr_reset_mode(struct sock *sk)
{
	if (!BBR(sk)->full_bw_reached)
		bbr_reset_startup_mode(sk);
	else
		bbr_reset_probe_bw_mode(sk);
}

static inline void matt_snd_cwnd_set(struct matt_output_ctx *outq, u32 val)
{
	WARN_ON_ONCE((int)val <= 0);
	outq->snd_cwnd = val;
}

static void bbr_check_probe_rtt_done(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);

	if (!(bbr->probe_rtt_done_stamp && after(jiffies, bbr->probe_rtt_done_stamp)))
		return;
	bbr->min_rtt_stamp = jiffies;
	matt_snd_cwnd_set(OUTQ(sk), max(OUTQ(sk)->snd_cwnd, bbr->prior_cwnd));	/* G1ad: tcp_snd_cwnd_set WARN_ON_ONCE [brk #0x800] is in the stock */
	bbr_reset_mode(sk);
}

void bbr_cwnd_event(struct sock *sk, enum matt_ca_event event)
{
	struct bbr *bbr = BBR(sk);

	if (event == CA_EVENT_TX_START && OUTQ(sk)->app_limited) {
		bbr->idle_restart = 1;
		bbr->ack_epoch_mstamp = MATT_SK(sk)->pkt_mgmt_ctx.matt_mstamp;
		bbr->ack_epoch_acked = 0;
		if (bbr->mode == BBR_PROBE_BW)
			bbr_set_pacing_rate(sk, bbr_bw(sk), BBR_UNIT);
		else if (bbr->mode == BBR_PROBE_RTT)
			bbr_check_probe_rtt_done(sk);
	}
}

u32 bbr_ssthresh(struct sock *sk)
{
	bbr_save_cwnd(sk);
	return OUTQ(sk)->snd_ssthresh;
}

u32 bbr_undo_cwnd(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);

	bbr->full_bw = 0;
	bbr->full_bw_cnt = 0;
	bbr_reset_lt_bw_sampling(sk);
	return OUTQ(sk)->snd_cwnd;
}

u32 bbr_sndbuf_expand(struct sock *sk)
{
	return 3;
}

size_t bbr_get_info(struct sock *sk, u32 ext, int *attr, union matt_cc_info *info)
{
	struct bbr *bbr = BBR(sk);

	if (ext & (1 << (16 - 1)) || ext & (1 << (3 - 1))) {
		u64 bw = bbr_bw(sk);

		bw = bw * MATT_SK(sk)->mss * USEC_PER_SEC >> BW_SCALE;
		memset(&info->bbr, 0, sizeof(info->bbr));
		info->bbr.bbr_bw_lo = (u32)bw;
		info->bbr.bbr_bw_hi = (u32)(bw >> 32);
		info->bbr.bbr_min_rtt = bbr->min_rtt_us;
		info->bbr.bbr_pacing_gain = bbr->pacing_gain;
		info->bbr.bbr_cwnd_gain = bbr->cwnd_gain;
		*attr = 16;
		return sizeof(info->bbr);
	}
	return 0;
}

static noinline void bbr_lt_bw_interval_done(struct sock *sk, u32 bw)
{
	struct bbr *bbr = BBR(sk);
	u32 diff;

	if (bbr->lt_bw) {
		diff = abs(bw - bbr->lt_bw);
		if ((diff * BBR_UNIT <= bbr_lt_bw_ratio * bbr->lt_bw) ||
		    (bbr_rate_bytes_per_sec(sk, diff, BBR_UNIT) <= bbr_lt_bw_diff)) {
			bbr->lt_bw = (bw + bbr->lt_bw) >> 1;
			bbr->lt_use_bw = 1;
			bbr->pacing_gain = BBR_UNIT;
			bbr->lt_rtt_cnt = 0;
			return;
		}
	}
	bbr->lt_bw = bw;
	bbr_reset_lt_bw_sampling_interval(sk);
}

static noinline void bbr_lt_bw_sampling(struct sock *sk, const struct rate_sample *rs)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);
	u32 lost, delivered;
	u64 bw;
	u32 t;

	if (bbr->lt_use_bw) {
		if (bbr->mode == BBR_PROBE_BW && bbr->round_start && ++bbr->lt_rtt_cnt >= bbr_lt_bw_max_rtts) {
			bbr_reset_lt_bw_sampling(sk);
			bbr_reset_probe_bw_mode(sk);
		}
		return;
	}
	if (!bbr->lt_is_sampling) {
		if (!rs->losses)
			return;
		bbr_reset_lt_bw_sampling_interval(sk);
		bbr->lt_is_sampling = true;
	}
	if (rs->is_app_limited) {
		bbr_reset_lt_bw_sampling(sk);
		return;
	}
	if (bbr->round_start)
		bbr->lt_rtt_cnt++;
	if (bbr->lt_rtt_cnt < bbr_lt_intvl_min_rtts)
		return;
	if (bbr->lt_rtt_cnt > 4 * bbr_lt_intvl_min_rtts) {
		bbr_reset_lt_bw_sampling(sk);
		return;
	}
	if (!rs->losses)
		return;
	lost = outq->lost - bbr->lt_last_lost;
	delivered = outq->delivered - bbr->lt_last_delivered;
	if (!delivered || (lost << BBR_SCALE) < bbr_lt_loss_thresh * delivered)
		return;
	t = div_u64(outq->delivered_mstamp, USEC_PER_MSEC) - bbr->lt_last_stamp;
	if ((s32)t < 1)
		return;
	if (t >= ~0U / USEC_PER_MSEC) {
		bbr_reset_lt_bw_sampling(sk);
		return;
	}
	t *= USEC_PER_MSEC;
	bw = (u64)delivered * BW_UNIT;
	do_div(bw, t);
	bbr_lt_bw_interval_done(sk, bw);
}

void bbr_set_state(struct sock *sk, u8 new_state)
{
	struct bbr *bbr = BBR(sk);

	if (new_state == MATT_CA_Loss) {
		struct rate_sample rs = { .losses = 1 };

		bbr->prev_ca_state = MATT_CA_Loss;
		bbr->full_bw = 0;
		bbr->round_start = 1;
		bbr_lt_bw_sampling(sk, &rs);
	}
}

static u32 bbr_bdp(struct sock *sk, u32 bw, int gain)
{
	struct bbr *bbr = BBR(sk);
	u32 bdp, min_rtt = bbr->min_rtt_us;
	u64 w;

	if (bbr->min_rtt_us == ~0U) {
		bdp = 10;
	} else {
		if (bbr_enable_opt && bbr_srtt_factor && bbr_mrtt_factor)
			min_rtt = OUTQ(sk)->srtt;
		w = (u64)bw * min_rtt;
		bdp = (((w * gain) >> BBR_SCALE) + BW_UNIT - 1) / BW_UNIT;
#line 442
		MATT_TRACE(0, 9, "bdp: %d, bw: %d, rtt: %d", bdp, bw, min_rtt);	/* G1ad: the stock skips the trace on the ~0U path */
	}
	return bdp;
}

static noinline u32 bbr_inflight(struct sock *sk, u32 bw, int gain)
{
	struct bbr *bbr = BBR(sk);
	u32 inflight = bbr_bdp(sk, bw, gain);

	inflight += 3 * bbr_tso_segs_goal(sk);
	inflight = (inflight + 1) & ~1U;
	if (bbr->mode == BBR_PROBE_BW && bbr->cycle_idx == 0)
		inflight += 2;
	return inflight;
}

void bbr_init(struct sock *sk)
{
	struct bbr *bbr = BBR(sk);
	struct matt_output_ctx *outq = OUTQ(sk);

	outq->snd_ssthresh = 0x7fffffff;
	bbr->prior_cwnd = 0;
	bbr->rtt_cnt = 0;
	bbr->next_rtt_delivered = outq->delivered;
	bbr->prev_ca_state = MATT_CA_Open;
	bbr->packet_conservation = 0;
	bbr->probe_rtt_round_done = 0;
	bbr->probe_rtt_done_stamp = 0;
	bbr->min_rtt_us = outq->min_rtt;
	bbr->min_rtt_stamp = jiffies;
	memset(&bbr->bw, 0, sizeof(bbr->bw));
	bbr->has_seen_rtt = 0;
	bbr_init_pacing_rate_from_rtt(sk);
	bbr->round_start = 0;
	bbr->idle_restart = 0;
	bbr->full_bw_reached = 0;
	bbr->full_bw = 0;
	bbr->full_bw_cnt = 0;
	bbr->cycle_mstamp = 0;
	bbr->cycle_idx = 0;
	bbr_reset_lt_bw_sampling(sk);
	bbr_reset_startup_mode(sk);
	bbr->ack_epoch_mstamp = MATT_SK(sk)->pkt_mgmt_ctx.matt_mstamp;
	bbr->ack_epoch_acked = 0;
	bbr->extra_acked_win_rtts = 0;
	bbr->extra_acked_win_idx = 0;
	bbr->extra_acked[0] = bbr->extra_acked[1] = 0;
	if (bbr_enable_opt && bbr_init_cwnd_opt && !(outq->srtt && outq->srtt < 8000))
		outq->snd_cwnd = 44;
	if (!(bbr_enable_opt && bbr_no_pacing))
		cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);
}

/* ---- "const" congestion control: fixed cwnd of 1000 ---- */
u32 const_ssthresh(struct sock *sk)
{
	return 1000;
}

void const_set_state(struct sock *sk, u8 new_state)
{
	if (new_state == MATT_CA_Loss)
		OUTQ(sk)->snd_cwnd = 1000;
}

void const_main(struct sock *sk, const struct rate_sample *rs)
{
	OUTQ(sk)->snd_cwnd = 1000;
}

u32 const_undo(struct sock *sk)
{
	return OUTQ(sk)->snd_cwnd;
}

void const_init(struct sock *sk)
{
	OUTQ(sk)->snd_cwnd = 1000;
}

/* ops table [0] bbr, [1] const (ThinLTO-promoted global name in the stock) */
struct matt_cc_ops matt_cc_group[2] __asm__("matt_cc_group.llvm.17795135818864888057") __attribute__((visibility("hidden"))) = {
	[MATT_CC_ALG_BBR] = {
		.ssthresh = bbr_ssthresh,
		.set_state = bbr_set_state,
		.cwnd_event = bbr_cwnd_event,
		.min_tso_segs = bbr_min_tso_segs,
		.cong_control = mibbr_main,
		.undo_cwnd = bbr_undo_cwnd,
		.sndbuf_expand = bbr_sndbuf_expand,
		.get_info = bbr_get_info,
		.name = "bbr",
		.init = bbr_init,
	},
	[1] = {
		.ssthresh = const_ssthresh,
		.set_state = const_set_state,
		.cong_control = const_main,
		.undo_cwnd = const_undo,
		.name = "const",
		.init = const_init,
	},
};

void matt_set_cc_config(struct sock *sk)
{
	MATT_SK(sk)->cc_ctx.ops = &matt_cc_group[0];
	matt_cc_group[0].init(sk);
}

#include "matt_bbr_main.h"
