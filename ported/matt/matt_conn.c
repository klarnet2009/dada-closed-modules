// SPDX-License-Identifier: GPL-2.0
/* matt connection set-up / demux (stock: mp_rcv.c, mp_conn.c): matt_rcv, state process, creq/crsp processing, backlog */
#include "matt.h"

struct sock *matt_sock_lookup(struct sk_buff *skb, union matt_sockaddr *sa, union matt_sockaddr *da);
void matt_packet_get_addrs(struct sock *sk, struct sk_buff *skb);
void matt_update_connect_rtt(struct sock *sk, u32 rtt);
int matt_request_sock_enqueue(struct sock *sk, const struct matt_creq *creq);
extern void (*xm_skb_condense)(struct sk_buff *skb);

bool matt_add_backlog(struct sock *sk, struct sk_buff *skb)
{
	u32 limit;

	xm_skb_condense(skb);
	limit = (u32)READ_ONCE(sk->sk_rcvbuf) + (u32)(READ_ONCE(sk->sk_sndbuf) >> 1);
	limit += 64 * 1024;
	if (sk_add_backlog(sk, skb, limit))
		return true;
	return false;
}

bool judgement_for_rst(struct sock *sk)
{
	struct matt_mgmt *m = &MATT_SK(sk)->pkt_mgmt_ctx;
	u8 fail;

	if (!m->idle_check)
		return 0;
	if (m->piong_flag) {
		m->piong_flag = 0;
		fail = 0;
	} else {
		fail = m->fail_cnt + 1;
	}
	m->fail_cnt = fail;
#line 53
	MATT_TRACE(0, 8, "fail_cnt:%u", fail);
	if (m->fail_cnt >= 6) {
		m->fail_cnt = 0;
		return 1;
	}
	return 0;
}

int matt_established_process_creq(struct sock *sk, struct sk_buff *skb)
{
	struct matt_creq *creq = (struct matt_creq *)skb_transport_header(skb);
	u32 ts = ntohl(creq->ts);
	int err;

#line 257
	MATT_TRACE(0, 9, "sk:%p rcv creq creq->timestamp = %8X", sk, ts);
	err = matt_send_crsp(sk, ts);
	if (err) {
#line 262
		MATT_TRACE(0, 2, "sk:%p fail to send crsp", sk);
	} else {
#line 266
		MATT_TRACE(0, 4, "sk:%p sent crsp", sk);
	}
	return err;
}

int matt_process_connect_request(struct sock *sk, struct sk_buff *skb)
{
	struct matt_creq *creq = (struct matt_creq *)skb_transport_header(skb);
	u32 ts = ntohl(creq->ts);
	int err;

#line 280
	MATT_TRACE(0, 9, "sk:%p rcv creq creq->timestamp = %8X", sk, ts);
	err = matt_send_crsp(sk, ts);
	if (err) {
#line 285
		MATT_TRACE(0, 2, "sk:%p fail to send crsp", sk);
		return err;
	}
	err = matt_request_sock_enqueue(sk, creq);
	if (err) {
#line 291
		MATT_TRACE(0, 2, "sk:%p fail to enque creq", sk);
		return err;
	}
#line 294
	MATT_TRACE(0, 4, "sk:%p sent crsp", sk);
	return err;
}

void matt_process_connect_respond(struct sock *sk, struct sk_buff *skb)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_crsp *crsp = (struct matt_crsp *)skb_transport_header(skb);
	u32 snd = msk->output_ctx.write_seq;
	u32 rcv_nxt, swnd, mss, ts, now;

	msk->output_ctx.snd_nxt = snd;
	msk->output_ctx.snd_una = snd;
	rcv_nxt = crsp->hdr.offset + 1;	/* G1ad: stock reads the field raw [no rev]; it is 0 for a crsp */
	msk->input_ctx.rcv_nxt = rcv_nxt;
	msk->input_ctx.copied = rcv_nxt;
	swnd = ntohl(crsp->rcv_buf);
	msk->output_ctx.snd_wnd = swnd;
	msk->output_ctx.max_window = swnd;
	mss = ntohs(crsp->mss);
	msk->rcv_mss = mss;
	msk->input_ctx.ack_bdp_th = mss << 1;
#line 378
	MATT_TRACE(0, 9, "snd_nxt=%u rcv_nxt=%u swnd=%u max_win=%u inq_copied=%u rcv_mss=%d", snd, rcv_nxt, swnd,
		   msk->output_ctx.max_window, msk->input_ctx.copied, msk->rcv_mss);
	matt_timer_stop(sk, 0);
	ts = ntohl(crsp->ts);
	now = (u64)ktime_get() / NSEC_PER_USEC;	/* G1ad: unsigned divide [umulh] in the stock */
#line 388
	MATT_TRACE(0, 9, "snd_ts_us: %u cur_ts_us: %u", ts, now);
	matt_update_connect_rtt(sk, now - ts);
	matt_set_state(sk, MATT_SS_ESTABLISHED);
#line 401
	MATT_TRACE(0, 4, "sk:%p state=%d", sk, sk->sk_state);
	matt_timer_start(sk, 6, 500000);
}

int matt_rcv_state_process(struct sock *sk, struct sk_buff *skb)
{
	struct matthdr *mh = (struct matthdr *)skb_transport_header(skb);
	int err;
	u8 state;

	matt_dump_skb("matt_rcv_state_process", skb);
	matt_mstamp_refresh(sk);
	matt_packet_get_addrs(sk, skb);
#line 1572
	MATT_TRACE(0, 9, "sk:%p rcv num:%u offset:%u reserved:%u type:%u len:%u", sk, ntohl(mh->num), ntohl(mh->offset),
		   ntohl(mh->reserved), mh->type, ntohs(mh->len));
	state = sk->sk_state;
	if ((s8)mh->flags >= 0) {
#line 1608
		MATT_TRACE(0, 9, ">>>sk:%p fail to process data, sk_state:%d !!!", sk, state);
		return -EINVAL;
	}
	if (state == MATT_SS_ESTABLISHING) {
		if (mh->type != MATT_MGMT_TYPE_CONN_RSP)
			return -EINVAL;
		matt_process_connect_respond(sk, skb);
		consume_skb(skb);
		return 0;
	}
	if (state != MATT_SS_LISTENING || mh->type != MATT_MGMT_TYPE_CONN_REQ)
		return -EINVAL;
	err = matt_process_connect_request(sk, skb);
	if (err) {
#line 1585
		MATT_TRACE(0, 2, "sk:%p fail to process creq", sk);
		return err;
	}
	consume_skb(skb);
	sk->sk_data_ready(sk);
	return 0;
}

int matt_rcv(struct sk_buff *skb)
{
	union matt_sockaddr da = {}, sa = {};	/* g61a: stock frame: sa below da */
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);
	struct matt_input_ctx *in;
	struct matthdr *mh;
	struct sock *sk;
	u32 segs;

	skb_pull(skb, skb_transport_header(skb) - skb->data);
	if (skb->len < sizeof(struct matthdr))
		goto drop_nosk;
	mh = (struct matthdr *)skb_transport_header(skb);
	matt_get_skb_addrs2(skb, &sa, &da);
	sk = matt_sock_lookup(skb, &sa, &da);
	if (!sk)
		goto drop_nosk;
	in = &MATT_SK(sk)->input_ctx;
	cb->num = ntohl(mh->num);
	cb->offset_start = ntohl(mh->offset);
	cb->offset_end = cb->offset_start + ntohs(mh->len);
	cb->flags = mh->flags;
	cb->types = mh->type;
	cb->sacked = 0;
	sk_incoming_cpu_update(sk);
	bh_lock_sock(sk);
	segs = max_t(u16, skb_shinfo(skb)->gso_segs, 1);
	in->trecv.total_num += segs;
	in->trecv.total_bytes += skb->len;
	if ((s8)cb->flags < 0) {
		switch (cb->types) {
		case MATT_MGMT_TYPE_CONN_REQ: in->trecv.req_num++; break;
		case MATT_MGMT_TYPE_CONN_RSP: in->trecv.rsp_num++; break;
		case MATT_MGMT_TYPE_RESET: in->trecv.rst_num++; break;
		case MATT_MGMT_TYPE_PACK: in->trecv.pack_num++; break;
		case MATT_MGMT_TYPE_NACK: in->trecv.nack_num++; break;
		case MATT_MGMT_TYPE_PING: in->trecv.ping_num++; break;
		case MATT_MGMT_TYPE_PONG: in->trecv.pong_num++; break;
		}
	} else {
		in->trecv.data_num += segs;
		in->trecv.data_bytes += skb->len - 16;
	}
	if (!sock_owned_by_user(sk)) {
		sk->sk_backlog_rcv(sk, skb);
	} else {
		matt_dump_skb("matt_rcv", skb);
		if (matt_add_backlog(sk, skb)) {
			segs = max_t(u16, skb_shinfo(skb)->gso_segs, 1);	/* g61a: the stock re-reads gso_segs here */
			if ((s8)mh->flags >= 0) {
				in->dropped[STATS_DROP_OVERFLOW].data_num += segs;
				in->dropped[STATS_DROP_OVERFLOW].data_bytes += skb->len - 16;
			}
			in->dropped[STATS_DROP_ALL].data_num += segs;
			in->dropped[STATS_DROP_ALL].data_bytes += skb->len - 16;
			bh_unlock_sock(sk);
			matt_dump_skb("matt_rcv", skb);
#line 1707
			MATT_TRACE(0, 9, "drop: sk=%p num=%u offset=%u len=%u backlog=%u rmem_alloc=%u limit=%u rcv_nxt=%u nxt_num=%u", sk,
				   ntohl(mh->num), ntohl(mh->offset), ntohs(mh->len), sk->sk_backlog.len,
				   atomic_read(&sk->sk_rmem_alloc), READ_ONCE(sk->sk_rcvbuf), in->rcv_nxt, in->rcv_nxt_num);
			goto drop;
		}
#line 1691
		MATT_TRACE(0, 9, "add_backlog: sk=%p num=%u offset=%u len=%u truesize=%u backlog=%u rmem_alloc=%u rcv_nxt=%u num=%u", sk,
			   ntohl(mh->num), ntohl(mh->offset), ntohs(mh->len), skb->truesize, sk->sk_backlog.len,
			   atomic_read(&sk->sk_rmem_alloc), in->rcv_nxt, in->rcv_nxt_num);
	}
	bh_unlock_sock(sk);
	return 0;
drop_nosk:
	matt_dump_skb("matt_rcv", skb);
drop:
	kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
	return -EINVAL;
}
