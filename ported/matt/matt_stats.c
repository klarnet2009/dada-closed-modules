// SPDX-License-Identifier: GPL-2.0
/* matt periodic statistics (stock: mp_stats.c): matt_stats_timer_handler with the inlined matt_tx_dump_stats / matt_rx_dump_stats.
 * Reconstructed from the OS4 stock disassembly: bandwidth bookkeeping and the timer re-arm are exact; the ARGUMENT ORDER of the
 * long Rx traces (data pkt / over flow) was decoded from the stack stores (see STATUS.md), not compared word by word. DO NOT LOAD before review. */
#include "matt.h"

#define PCT(cnt, total) ((u32)(((u64)(cnt) * 10000) / (total)))

static __always_inline void matt_tx_dump_stats(struct sock *sk)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	char cur_rate[50] = {};
	char max_rate[50] = {};
	u32 bw;

	if (outq->send[STATS_FACKED].data_num) {
		u32 delta = outq->bytes_acked - outq->last_bytes_acked;

		bw = delta << 3;
		outq->last_bw = delta;
		matt_minmax_running_max(&outq->max_bw, 125000, jiffies, bw);
		outq->snd_max_bw = outq->max_bw.s[0].v;
		adaptive_bit_unit_to_str(cur_rate, 50, bw);
		adaptive_bit_unit_to_str(max_rate, 50, outq->snd_max_bw);
#line 217
		MATT_TRACE(0, 4, "[Tx:%p] deliver to remote: send bw: %u bytes, cur_rate: %s, last_max_rate: %s, %u fack cnt (%llu bytes), %u sack cnt (%llu bytes)", sk, bw, cur_rate, max_rate, outq->send[STATS_FACKED].data_num, outq->send[STATS_FACKED].data_bytes, outq->send[STATS_SACKED].data_num, outq->send[STATS_SACKED].data_bytes);
		outq->last_bytes_acked = outq->bytes_acked;
	}
#line 226
	MATT_TRACE(0, 4, "[Tx:%p] to driver: %u cnt (%llu bytes), buffered: %d bytes, swnd: %u bytes, cwnd: %u, srtt: %u, rto: %u", sk, outq->tsend.total_num, outq->tsend.total_bytes, READ_ONCE(sk->sk_wmem_queued), outq->snd_wnd, outq->snd_cwnd, outq->srtt, outq->rto);
	if (outq->tsend.data_num) {
		u32 n = outq->tsend.data_num;
		u32 lost = PCT(outq->send[STATS_LOST].data_num, n), nack = PCT(outq->send[STATS_NACK_LOST].data_num, n);
		u32 rack = PCT(outq->send[STATS_RACK_LOST].data_num, n), relost = PCT(outq->send[STATS_RELOST].data_num, n);
		u32 rto = PCT(outq->send[STATS_RTO].data_num, n), retry = PCT(outq->send[STATS_RETRY].data_num, n);

#line 288
		MATT_TRACE(0, 4, "[Tx:%p] data: %u cnt (%llu bytes), lost: %u cnt [%u.%02u %%|%llu bytes], nack-lost: %u cnt [%u.%02u %%|%llu bytes], rack-lost: %u cnt [%u.%02u %%|%llu bytes], relost: %u cnt [%u.%02u %%|%llu bytes], rto: %u cnt [%u.%02u %%|%llu bytes], retry: %u cnt [%u.%02u %%|%llu bytes]", sk, n, outq->tsend.data_bytes, outq->send[STATS_LOST].data_num, lost / 100, lost % 100, outq->send[STATS_LOST].data_bytes, outq->send[STATS_NACK_LOST].data_num, nack / 100, nack % 100, outq->send[STATS_NACK_LOST].data_bytes, outq->send[STATS_RACK_LOST].data_num, rack / 100, rack % 100, outq->send[STATS_RACK_LOST].data_bytes, outq->send[STATS_RELOST].data_num, relost / 100, relost % 100, outq->send[STATS_RELOST].data_bytes, outq->send[STATS_RTO].data_num, rto / 100, rto % 100, outq->send[STATS_RTO].data_bytes, outq->send[STATS_RETRY].data_num, retry / 100, retry % 100, outq->send[STATS_RETRY].data_bytes);
	}
#line 298
	MATT_TRACE(0, 4, "[Tx:%p] pack: %u, nack: %u, req: %u, rsp: %u, ping: %u, pong: %u, rst: %u", sk, outq->tsend.pack_num, outq->tsend.nack_num, outq->tsend.req_num, outq->tsend.rsp_num, outq->tsend.ping_num, outq->tsend.pong_num, outq->tsend.rst_num);
}

static __always_inline void matt_rx_dump_stats(struct sock *sk)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	char cur_rate[50] = {};
	char max_rate[50] = {};
	u32 bw;

	if (in->copied) {
		u32 delta = in->copied - in->last_copied;

		bw = delta << 3;
		in->last_bw = delta;
		matt_minmax_running_max(&in->max_bw, 125000, jiffies, bw);
		in->rcv_max_bw = in->max_bw.s[0].v;
		adaptive_bit_unit_to_str(cur_rate, 50, bw);
		adaptive_bit_unit_to_str(max_rate, 50, in->rcv_max_bw);
#line 322
		MATT_TRACE(0, 4, "[Rx:%p] deliver to user: %u bytes, last_bw: %u bytes, cur_rate: %s, max_rate: %s, rwnd: %llu bytes", sk, in->copied, in->last_bw, cur_rate, max_rate, (u64)(u32)max_t(int, (READ_ONCE(sk->sk_rcvbuf) - (atomic_read(&sk->sk_rmem_alloc) + READ_ONCE(sk->sk_backlog.len))) >> 2, 0));
		in->last_copied = in->copied;
	}
#line 330
	MATT_TRACE(0, 4, "[Rx:%p] from driver: %u cnt (%llu bytes), dropped: %u cnt (%llu bytes)", sk, in->trecv.total_num, in->trecv.total_bytes, in->dropped[STATS_DROP_ALL].data_num, in->dropped[STATS_DROP_ALL].data_bytes);
	if (in->trecv.data_num) {
		u32 all = in->dropped[STATS_DROP_ALL].data_num;
		u32 dr_pct = PCT(all, in->trecv.data_num);
		u32 ovf = PCT(in->dropped[STATS_DROP_OVERFLOW].data_num, all), ovw = PCT(in->dropped[STATS_DROP_OVERWINDOW].data_num, all);
		u32 old = PCT(in->dropped[STATS_DROP_OLD_DATA].data_num, all), inv = PCT(in->dropped[STATS_DROP_INVAL_DATA].data_num, all);
		u32 oack = PCT(in->dropped[STATS_DROP_OLD_ACK].data_num, all), part = PCT(in->dropped[STATS_DROP_OVERWINDOW_PARTIAL].data_num, all);

#line 378
		MATT_TRACE(0, 4, "[Rx:%p] data pkt: %u cnt (%llu bytes), dropped: %u cnt [%u.%02u %%|%llu bytes], total ofo: %u cnt [%u skbcnt|%llu bytes], cur ofo: %u skbcnt", sk, in->trecv.data_num, in->trecv.data_bytes, all, dr_pct / 100, dr_pct % 100, in->dropped[STATS_DROP_ALL].data_bytes, in->dropped[STATS_OFO].data_num, (u32)in->rcv_total_num_ofo, in->dropped[STATS_OFO].data_bytes, in->rcv_cur_num_ofo);
#line 396
		MATT_TRACE(0, 4, "[Rx:%p] over flow: %u cnt (%u.%02u %%), over window: %u cnt (%u.%02u %%), partial over window: %u cnt (%u.%02u %%), old data: %u cnt (%u.%02u %%), invalid data: %u cnt (%u.%02u %%), old ack: %u cnt (%u.%02u %%)", sk, in->dropped[STATS_DROP_OVERFLOW].data_num, ovf / 100, ovf % 100, in->dropped[STATS_DROP_OVERWINDOW].data_num, ovw / 100, ovw % 100, in->dropped[STATS_DROP_OVERWINDOW_PARTIAL].data_num, part / 100, part % 100, in->dropped[STATS_DROP_OLD_DATA].data_num, old / 100, old % 100, in->dropped[STATS_DROP_INVAL_DATA].data_num, inv / 100, inv % 100, in->dropped[STATS_DROP_OLD_ACK].data_num, oack / 100, oack % 100);
	}
	if (in->trecv.pack_num) {
#line 408
		MATT_TRACE(0, 4, "[Rx:%p] pack pkt: %u cnt, nack pkt: %u cnt, creq pkt: %u cnt, resp pkt: %u cnt, ping pkt: %u cnt, pong pkt: %u cnt", sk, in->trecv.pack_num, in->trecv.nack_num, in->trecv.req_num, in->trecv.rsp_num, in->trecv.ping_num, in->trecv.pong_num);
	}
}

void matt_stats_timer_handler(struct sock *sk)
{
	struct matt_sock *msk = MATT_SK(sk);

	matt_tx_dump_stats(sk);
	matt_rx_dump_stats(sk);
	if (!mod_timer(&msk->timer_ctx.stimer.timer, jiffies + usecs_to_jiffies(500000)))
		sock_hold(sk);
}
