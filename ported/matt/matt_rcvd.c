// SPDX-License-Identifier: GPL-2.0
/* matt data receive (stock: mp_input.c): matt_rcv_data with the inlined validate/min_owd/queue_rcv/dequeue_ofo/sack_remove/ack_send_check_opt/
 * queue_ofo/sack_new_ofo_skb. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

extern const char matt_str_old_data[] __asm__(".str.llvm.17829671557986578456") __attribute__((visibility("hidden")));
extern const char matt_str_overwin[] __asm__(".str.1.llvm.17829671557986578456") __attribute__((visibility("hidden")));
extern const char matt_str_invalid[] __asm__(".str.2.llvm.17829671557986578456") __attribute__((visibility("hidden")));

#define MATT_SACKS 128

static __always_inline u32 matt_gso_segs(struct sk_buff *skb)
{
	return max_t(u16, skb_shinfo(skb)->gso_segs, 1);
}

static __always_inline bool matt_validate_incoming(struct sock *sk, struct sk_buff *skb, struct matthdr *mh)
{
	if ((u64)ntohs(mh->len) + sizeof(struct matthdr) != skb->len) {
#line 165
		MATT_TRACE(0, 4, "invalid skb, skb_len=%u mh_len=%u", skb->len, ntohs(mh->len));
		return false;
	}
	return true;
}

static __always_inline void matt_rcv_update_min_owd(struct sock *sk, struct sk_buff *skb)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);
	u32 tx = cb->rx.peer_tx_mstamp, rx = cb->rx.local_rx_mstamp;
	u32 cur_owd = tx > rx ? tx - rx : rx - tx;

	if (before(in->last_rcv_high_seq, cb->offset_start) && !in->min_owd) {
		in->min_owd = cur_owd;
		in->min_owd_tx_ts = tx;
		in->min_owd_rx_ts = rx;
#line 213
		MATT_TRACE(0, 9, "seq:%u righ_seq:%u tx_ts:%u rcv_ts:%u cur_owd:%u min_owd:%u", cb->offset_start, in->last_rcv_high_seq, tx, rx,
			   cur_owd, in->min_owd);
	}
}

/* in-order accounting of one skb (same code for the arriving skb and for skbs dequeued from the ofo tree) */
static __always_inline void matt_rcv_advance(struct sock *sk, struct sk_buff *skb)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_input_ctx *in = &msk->input_ctx;
	u8 *th = skb_transport_header(skb);

	skb_pull(skb, sizeof(struct matthdr));
	in->data_bytes_received += MATT_SKB_CB(skb)->offset_end - in->rcv_nxt;
	in->data_segs_received++;
	in->data_mstamp = msk->pkt_mgmt_ctx.matt_mstamp;
	in->rcv_nxt = MATT_SKB_CB(skb)->offset_end;
	in->rcv_nxt_num = ntohl(*(__be32 *)th);
}

static __always_inline void matt_data_dequeue_ofo(struct sock *sk)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	struct rb_root *root = &in->out_of_order_queue;
	struct rb_node *p;

	if (!READ_ONCE(root->rb_node))
		return;
	p = rb_first(root);
	while (p) {
		struct sk_buff *skb1 = rb_entry(p, struct sk_buff, rbnode);
		struct rb_node *next;

		if (before(in->rcv_nxt, MATT_SKB_CB(skb1)->offset_start))
			break;
		next = rb_next(p);
		rb_erase(p, root);
#line 237
		MATT_TRACE(0, 9, "dequeue skb:%u->%u", MATT_SKB_CB(skb1)->offset_start, MATT_SKB_CB(skb1)->offset_end);
		matt_rcv_advance(sk, skb1);
		__skb_queue_tail(&sk->sk_receive_queue, skb1);
		p = next;
		in->rcv_cur_num_ofo--;
	}
	if (!READ_ONCE(root->rb_node)) {
		in->ooo_last_skb = NULL;
		in->rcv_cur_num_ofo = 0;
	}
}

static __always_inline void matt_sack_remove(struct sock *sk)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	u32 n = in->num_sacks;
	u32 first = in->sack_first_index;
	int idx;
	u32 i;

	if (!n)
		return;
	if (!READ_ONCE(in->out_of_order_queue.rb_node)) {
		in->num_sacks = 0;
		in->sack_first_index = 0;
		return;
	}
	idx = 0;
	for (i = 0; i < in->num_sacks; i++) {
		idx = (first + i) & (MATT_SACKS - 1);	/* G1ad: was a signed %, which the array-bounds sanitizer turns into a trap check; the stock masks with 0x7f */
		if (before(in->rcv_nxt, in->selective_acks[idx].offset_end))
			break;
#line 271
		MATT_TRACE(0, 9, "remove_sacks: %u-->%u rcv_nxt=%u num_sacks=%u", in->selective_acks[idx].offset_start,
			   in->selective_acks[idx].offset_end, in->rcv_nxt, n);
		in->selective_acks[idx].offset_start = 0;
		in->selective_acks[idx].offset_end = 0;
		n--;
	}
	in->num_sacks = n;
	in->sack_first_index = idx;
#line 280
	MATT_TRACE(0, 9, "num_sacks=%u fisrt_index=%u rcv_nxt=%u", n, idx, in->rcv_nxt);
}

static __always_inline void matt_ack_send_check_opt(struct sock *sk, struct matthdr *mh, u32 now, u32 pack_time)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	u32 to = 0, pto, delta, queue_bytes, trigger;
	s32 grow;

	if (READ_ONCE(in->out_of_order_queue.rb_node)) {
		u32 srtt = in->rcv_srtt;

		to = srtt ? srtt + (srtt >> 2) : 20000;
		matt_timer_start(sk, 2, to);
	}
	pto = in->rcv_period ?: 2000;
	if ((mh->flags & 0x20) && in->rcv_max_bw <= 199999999)
		goto send;
	delta = max_t(s64, (s64)now - (s64)pack_time, 0);
	if (pto <= delta) {
#line 134
		MATT_TRACE(0, 9, "pack delta event: delta:%u, timeout:%u us", delta, to);
		goto send;
	}
	grow = in->rcv_nxt - in->rcv_wup;
	queue_bytes = max_t(s32, grow, 0);
	trigger = in->queue_bytes_trigger;
	if (queue_bytes >= trigger) {
#line 138
		MATT_TRACE(0, 9, "pack queue bytes event: queue_bytes:%d, inq->queue_bytes_trigger:%d, delta:%d us", queue_bytes, trigger, delta);
		goto send;
	}
#line 143
	MATT_TRACE(0, 9, "pack start timer: grow:%u, delta:%u us, timeout:%u us, pto:%u us, queue_bytes:%u, queue_bytes_trigger:%u", grow,
		   delta, to, pto, queue_bytes, trigger);
	matt_timer_start(sk, 1, 20000);
	return;
send:
	matt_send_pack(sk);
}

static __always_inline void matt_drop_stat(struct matt_data_stats *ds, struct sk_buff *skb, u32 segs)
{
	ds->data_num += segs;
	ds->data_bytes += skb->len - sizeof(struct matthdr);
}

static __always_inline void matt_sack_new_ofo_skb(struct sock *sk, struct sk_buff *skb)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	struct matt_sack_block *acks = in->selective_acks;
	u32 start = MATT_SKB_CB(skb)->offset_start, end = MATT_SKB_CB(skb)->offset_end;
	u32 n, first, cur, i, count;
	u32 segs = matt_gso_segs(skb);

	matt_drop_stat(&in->dropped[STATS_OFO], skb, segs);
	n = in->num_sacks;
	if (!n) {
		first = 0;
		cur = 0;
		in->sack_first_index = 0;
		goto insert;
	}
	if (n >= MATT_SACKS) {
#line 333
		MATT_TRACE(0, 2, "sacks block reach max, seq(%u->%u)", start, end);
		return;
	}
	first = in->sack_first_index;
#line 341
	MATT_TRACE(0, 9, "0:sack seq(%u->%u) first=%u num_sacks=%u this=%u", start, end, first, n, first);
	if (before(end, acks[first].offset_start)) {
		cur = (first - 1) & (MATT_SACKS - 1);
		in->sack_first_index = cur;
		goto insert;
	}
	if (end == acks[first].offset_start) {
#line 348
		MATT_TRACE(0, 9, "1:sack seq(%u->%u) first=%u num_sacks=%u this=%u", start, end, first, n, first);
		acks[first].offset_start = start;
		return;
	}
	cur = (first + n - 1) & (MATT_SACKS - 1);
	if (before(acks[cur].offset_end, start)) {
		cur = (first + n) & (MATT_SACKS - 1);
		goto insert;
	}
	if (acks[cur].offset_end == start) {
		acks[cur].offset_end = end;
#line 360
		MATT_TRACE(0, 9, "2:sack seq(%u->%u) first=%u num_sacks=%u this=%u", start, end, first, n, first);
		return;
	}
	cur = first;
	for (i = 0; i < n - 1; i++) {
		u32 nxt;

		cur = (first + i) & (MATT_SACKS - 1);
		nxt = (first + i + 1) & (MATT_SACKS - 1);
		if (acks[cur].offset_end == start) {
			acks[cur].offset_end = end;
#line 372
			MATT_TRACE(0, 9, "3:sack seq(%u->%u) i=%d first=%u num_sacks=%u this=%u", start, end, i, first, n, cur);
			if (acks[nxt].offset_start == end) {
#line 375
				MATT_TRACE(0, 9, "merge_sack seq(%u->%u) this.end=%u next.start=%u i=%d first=%d num_sacks=%u this=%u", start, end,
					   acks[cur].offset_end, acks[nxt].offset_start, i, first, n, cur);
				acks[cur].offset_end = acks[nxt].offset_end;
				in->num_sacks--;
				count = (n - i + 126) & (MATT_SACKS - 1);
				for (; count; count--) {
					acks[nxt] = acks[(nxt + 1) & (MATT_SACKS - 1)];
					nxt = (nxt + 1) & (MATT_SACKS - 1);
				}
			}
			return;
		}
		if (acks[nxt].offset_start == end) {
			acks[nxt].offset_start = start;
#line 386
			MATT_TRACE(0, 9, "4:sack seq(%u->%u) i=%d first=%u num_sacks=%u this=%u", start, end, i, first, n, cur);
			return;
		}
		if (!before(acks[nxt].offset_start, start))
			break;
	}
	count = (first + n + 127 - cur) & (MATT_SACKS - 1);
	{
		u32 dst = first + n;

		while (count--) {
			acks[dst & (MATT_SACKS - 1)] = acks[(dst - 1) & (MATT_SACKS - 1)];
			dst--;
		}
	}
	cur = (cur + 1) & (MATT_SACKS - 1);
insert:
	acks[cur].offset_start = start;
	acks[cur].offset_end = end;
	in->num_sacks = in->num_sacks + 1;
#line 410
	MATT_TRACE(0, 9, "new_sack seq(%u->%u) num=%u first=%u cur=%u", start, end, in->num_sacks, first, cur);
}

static __always_inline void matt_queue_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);

	matt_rcv_advance(sk, skb);
	matt_rcv_update_min_owd(sk, skb);
	__skb_queue_tail(&sk->sk_receive_queue, skb);
	skb_set_owner_r(skb, sk);
#line 299
	MATT_TRACE(0, 9, "sk=%p num=%u offset=%u len=%d skb_truesize=%u sk_rmem_alloc=%d backlog=%u", sk,
		   ntohl(*(__be32 *)skb_transport_header(skb)), cb->offset_start,
		   ntohs(((struct matthdr *)skb_transport_header(skb))->len), skb->truesize, atomic_read(&sk->sk_rmem_alloc),
		   sk->sk_backlog.len);
}

/* out-of-order insert; false = the skb overlaps a queued one and was freed (no owner charge, goes straight to the ack part) */
static __always_inline bool matt_data_queue_ofo(struct sock *sk, struct sk_buff *skb)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);
	struct rb_root *root = &in->out_of_order_queue;
	u32 start = cb->offset_start, end = cb->offset_end;
	u32 segs;

#line 435
	MATT_TRACE(0, 9, "ofo: num=%u offset=%u end=%u rcv_nxt=%u", ntohl(((struct matthdr *)skb->data)->num), start, end, in->rcv_nxt);
	if (!READ_ONCE(root->rb_node)) {
		in->num_sacks = 1;
		in->sack_first_index = 0;
		in->selective_acks[0].offset_start = cb->offset_start;
		in->selective_acks[0].offset_end = cb->offset_end;
		skb->rbnode.__rb_parent_color = 0;
		skb->rbnode.rb_right = NULL;
		skb->rbnode.rb_left = NULL;
		root->rb_node = &skb->rbnode;
		rb_insert_color(&skb->rbnode, root);
		in->ooo_last_skb = skb;
#line 446
		MATT_TRACE(0, 9, "first sack:%u->%u", start, end);
	} else {
		struct rb_node *parent, **link;
		struct sk_buff *last = in->ooo_last_skb;
		bool is_last;

		if (before(start, MATT_SKB_CB(last)->offset_end)) {
			link = &root->rb_node;
			parent = NULL;
			is_last = true;
			while (*link) {
				struct sk_buff *skb1;

				parent = *link;
				skb1 = rb_entry(parent, struct sk_buff, rbnode);
				if (before(start, MATT_SKB_CB(skb1)->offset_start)) {
					link = &parent->rb_left;
					is_last = false;
				} else if (before(start, MATT_SKB_CB(skb1)->offset_end)) {
#line 468
					MATT_TRACE(0, 9, "skb:%u->%u, skb1:%u->%u", start, end, MATT_SKB_CB(skb1)->offset_start,
						   MATT_SKB_CB(skb1)->offset_end);
					segs = matt_gso_segs(skb);
					matt_drop_stat(&in->dropped[STATS_DROP_OVERWINDOW], skb, segs);
					matt_drop_stat(&in->dropped[STATS_DROP_ALL], skb, segs);
					kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
					return false;
				} else {
					link = &parent->rb_right;
					is_last = false;
				}
			}
		} else {
			parent = &last->rbnode;
			link = &parent->rb_right;
			is_last = true;
		}
		in->rcv_total_num_ofo += (u8)matt_gso_segs(skb);
		skb->rbnode.__rb_parent_color = (unsigned long)parent;
		skb->rbnode.rb_right = NULL;
		skb->rbnode.rb_left = NULL;
		*link = &skb->rbnode;
		rb_insert_color(&skb->rbnode, root);
		if (is_last)
			in->ooo_last_skb = skb;
		matt_rcv_update_min_owd(sk, skb);
		matt_sack_new_ofo_skb(sk, skb);
	}
	return true;
}

void matt_rcv_data(struct sock *sk, struct sk_buff *skb)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_input_ctx *in = &msk->input_ctx;
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);
	struct matthdr *mh = (struct matthdr *)skb->data;
	struct matt_data_stats *ds;
	const char *why;
	u32 segs, old_rcv_nxt, start, now, pack_time, nack_time;
	struct rb_root *root = &in->out_of_order_queue;

	if (!matt_validate_incoming(sk, skb, mh)) {
		segs = matt_gso_segs(skb);
		ds = &in->dropped[STATS_DROP_INVAL_DATA];
		why = matt_str_invalid;
		goto drop;
	}
#line 520
	MATT_TRACE(0, 9, "rcv data to stack num:%u offset:%u rcv_nxt=%u copied=%u", ntohl(mh->num), ntohl(mh->offset), in->rcv_nxt, in->copied);
	if (!in->rcv_high_seq || before(in->rcv_high_seq, cb->offset_start))
		in->rcv_high_seq = cb->offset_start;
	start = cb->offset_start;
	segs = matt_gso_segs(skb);
	old_rcv_nxt = in->rcv_nxt;
	if (__builtin_expect_with_probability(start == old_rcv_nxt, 1, 0.3)) {
		matt_queue_rcv(sk, skb);
		matt_data_dequeue_ofo(sk);
		matt_sack_remove(sk);
		sk->sk_data_ready(sk);
		goto ack;
	}
	if (!before(old_rcv_nxt, cb->offset_end)) {
		ds = &in->dropped[STATS_DROP_OLD_DATA];
		why = matt_str_old_data;
		goto drop;
	}
	{
		s32 space = READ_ONCE(sk->sk_rcvbuf) - (atomic_read(&sk->sk_rmem_alloc) + READ_ONCE(sk->sk_backlog.len));
		s32 wnd = max_t(s32, space >> 2, 0);

		if (!before(start - old_rcv_nxt, wnd)) {
			ds = &in->dropped[STATS_DROP_OVERWINDOW];
			why = matt_str_overwin;
			goto drop;
		}
	}
	if (before(start, in->rcv_nxt)) {
		ds = &in->dropped[STATS_DROP_OVERWINDOW_PARTIAL];
		why = matt_str_invalid;
		goto drop;
	}
	if (!matt_data_queue_ofo(sk, skb))
		goto ack;
	skb_set_owner_r(skb, sk);
ack:
	now = msk->pkt_mgmt_ctx.matt_mstamp;
	pack_time = in->pack_time;
	nack_time = in->nack_time;
	in->rcv_nxt_num += (u8)segs;
	if (before(in->rcv_wup, in->rcv_nxt) || start != old_rcv_nxt || READ_ONCE(root->rb_node))
		matt_ack_send_check_opt(sk, mh, now, pack_time);
	if ((s32)(READ_ONCE(sk->sk_rcvbuf) - (atomic_read(&sk->sk_rmem_alloc) + READ_ONCE(sk->sk_backlog.len))) > 3)
		return;
	{
		u32 srtt = in->rcv_srtt;
		u32 to = srtt ? srtt + (srtt >> 2) : 20000;
		u32 delta = max_t(s64, (s64)now - (s64)nack_time, 0);

		if (to <= delta)
			matt_send_nack(sk);
		matt_timer_start(sk, 2, 20000);
	}
	return;
drop:
	matt_drop_stat(ds, skb, segs);
	matt_drop_stat(&in->dropped[STATS_DROP_ALL], skb, segs);
#line 582
	MATT_TRACE(0, 9, "%s num:%u offset(%u, %u) rcv_nxt:%u", why, ntohl(mh->num), cb->offset_start, cb->offset_end, in->rcv_nxt);
	consume_skb(skb);
}
