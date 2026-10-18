// SPDX-License-Identifier: GPL-2.0
/* matt send path (stock: mp_tx.c; modelled on net/ipv4/tcp_output.c of 6.6): write_xmit, fragment, trim_head, retransmit */
#include "matt.h"

void matt_rbtree_insert(struct rb_root *root, struct sk_buff *skb);

extern int (*xm_pskb_expand_head)(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp_mask);
extern bool (*xm_napi_pp_put_page)(struct page *page, bool napi_safe);

static inline u32 matt_cwnd_test(struct sock *sk)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	u32 in_flight = out->packets_out - (out->sacked_out + out->lost_out) + out->retrans_out;
	u32 cwnd_quota, half;

	if (out->snd_cwnd <= in_flight)
		return 0;
	cwnd_quota = out->snd_cwnd - in_flight;
	half = max_t(u32, out->snd_cwnd >> 1, 1);
#line 807
	MATT_TRACE(0, 9, "cwnd:%d inflight:%d half cwnd:%d, cwnd quota:%d", out->snd_cwnd, in_flight, half, cwnd_quota);
	return min(half, cwnd_quota);
}

static inline bool matt_small_queue_check(struct sock *sk, struct sk_buff *skb, int factor)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	unsigned long limit = max_t(unsigned long, 2 * skb->truesize, READ_ONCE(sysctl_limit_output));
	struct rb_node *node;

	limit <<= factor;
#line 65
	MATT_TRACE(0, 9, "limit_output=%lu, sk_wmem_alloc=%u\n", limit, sk->sk_wmem_alloc.refs.counter);
	if (refcount_read(&sk->sk_wmem_alloc) > limit) {
		node = out->rb_rtx_queue.rb_node;
		if (!node || (!node->rb_left && !node->rb_right))
			return false;
		set_bit(0, &sk->sk_tsq_flags);
		smp_mb__after_atomic();
		if (refcount_read(&sk->sk_wmem_alloc) > limit)
			return true;
	}
	return false;
}

void matt_write_xmit(struct sock *sk, int push_one)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_output_ctx *out = &ms->output_ctx;
	unsigned int mss_now = ms->mss;
	struct sk_buff *skb;
	u32 cwnd_quota, prior;

	matt_mstamp_refresh(sk);
	while ((skb = skb_peek(&sk->sk_write_queue))) {
		struct matt_skb_cb *cb = MATT_SKB_CB(skb);

		cwnd_quota = matt_cwnd_test(sk);
		if (__builtin_expect_with_probability(!cwnd_quota, 1, 0.2)) {
#line 844
			MATT_TRACE(0, 9, "without cwnd_quota! cwnd_quota:%d", cwnd_quota);
			break;
		}
		if (!cb->gso_segs || (cb->gso_segs != 1 && cb->gso_size != mss_now)) {
			if (skb->len <= mss_now) {
				cb->gso_segs = 1;
				cb->gso_size = 0;
			} else {
				cb->gso_segs = DIV_ROUND_UP(skb->len, mss_now);
				cb->gso_size = mss_now;
			}
		}
		if (__builtin_expect_with_probability(after(cb->offset_end, out->snd_una + out->snd_wnd), 1, 0.1)) {
#line 852
			MATT_TRACE(0, 9, "without snd_wnd!");
			break;
		}
		if (__builtin_expect_with_probability(matt_small_queue_check(sk, skb, 0), 1, 0.3)) {
#line 869
			MATT_TRACE(0, 9, "without small_queue!");
			break;
		}
		if (matt_transmit_skb(sk, skb, 1))
			break;
		out->snd_nxt = cb->offset_end;
		prior = out->packets_out;
		__skb_unlink(skb, &sk->sk_write_queue);
		matt_rbtree_insert(&out->rb_rtx_queue, skb);
		out->packets_out += cb->gso_segs;
		if (!prior)
			matt_rearm_rto(sk);
		matt_check_space(sk);
	}
}

int _xm_skb_unclone_keeptruesize(struct sk_buff *skb, gfp_t pri)
{
	unsigned int saved_end_offset = skb_end_offset(skb);
	unsigned int saved_truesize = skb->truesize;
	struct skb_shared_info *shinfo;
	int res;

	res = xm_pskb_expand_head(skb, 0, 0, pri);
	if (res)
		return res;
	skb->truesize = saved_truesize;
	if (likely(skb_end_offset(skb) == saved_end_offset))
		return 0;
	if (saved_end_offset == SKB_SMALL_HEAD_HEADROOM || skb_end_offset(skb) == SKB_SMALL_HEAD_HEADROOM) {
#line 428
		MATT_TRACE(0, 2, "__skb_unclone_keeptruesize() skb_end_offset() %u -> %u", saved_end_offset, skb_end_offset(skb));
		WARN_ON_ONCE(1);
		return 0;
	}
	shinfo = skb_shinfo(skb);
	memmove(skb->head + saved_end_offset, shinfo, offsetof(struct skb_shared_info, frags[shinfo->nr_frags]));
	skb->end = saved_end_offset;
	return 0;
}

static inline void matt_skb_frag_unref(struct sk_buff *skb, int f)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);

	if (!skb_zcopy_managed(skb)) {
		struct page *page = skb_frag_page(&shinfo->frags[f]);

		if (!(skb->pp_recycle && xm_napi_pp_put_page(page, false)))
			put_page(page);
	}
}

static int matt_pskb_trim_head(struct sk_buff *skb, int len)
{
	struct skb_shared_info *shinfo;
	int i, k, eat = len;

	k = 0;
	shinfo = skb_shinfo(skb);
	for (i = 0; i < shinfo->nr_frags; i++) {
		int size = skb_frag_size(&shinfo->frags[i]);

		if (size <= eat) {
			matt_skb_frag_unref(skb, i);
			eat -= size;
		} else {
			shinfo->frags[k] = shinfo->frags[i];
			if (eat) {
				skb_frag_off_add(&shinfo->frags[k], eat);
				skb_frag_size_sub(&shinfo->frags[k], eat);
				eat = 0;
			}
			k++;
		}
	}
	shinfo->nr_frags = k;
	skb->data_len -= len;
	skb->len = skb->data_len;
	return len;
}

int matt_trim_head(struct sock *sk, struct sk_buff *skb, u32 len)
{
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);
	u32 delta_truesize;

	if (skb_cloned(skb) && _xm_skb_unclone_keeptruesize(skb, GFP_ATOMIC))
		return -ENOMEM;
	delta_truesize = matt_pskb_trim_head(skb, len);
	cb->offset_start += len;
	skb->truesize -= delta_truesize;
	sk_wmem_queued_add(sk, -delta_truesize);
	sk_mem_uncharge(sk, delta_truesize);
	if (cb->gso_segs > 1) {
		unsigned int mss = cb->gso_size;

		if (skb->len <= mss) {
			cb->gso_segs = 1;
			cb->gso_size = 0;
		} else {
			cb->gso_segs = DIV_ROUND_UP(skb->len, mss);
			cb->gso_size = mss;
		}
	}
	return 0;
}

extern void (*xm_skb_split)(struct sk_buff *skb, struct sk_buff *skb1, const u32 len);

static inline void matt_set_skb_tso_segs(struct sk_buff *skb, unsigned int mss_now)
{
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);

	if (skb->len <= mss_now) {
		cb->gso_segs = 1;
		cb->gso_size = 0;
	} else {
		cb->gso_segs = DIV_ROUND_UP(skb->len, mss_now);
		cb->gso_size = mss_now;
	}
}

int matt_fragment(struct sock *sk, enum matt_queue tcp_queue, struct sk_buff *skb, u32 len, unsigned int mss_now, gfp_t gfp)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	struct sk_buff *buff;
	int old_factor, nlen;
	long limit;
	u8 flags;

	if (WARN_ON(len > skb->len))
		return -EINVAL;
	limit = sk->sk_sndbuf + 2 * SKB_TRUESIZE(GSO_LEGACY_MAX_SIZE);
	if (unlikely((sk->sk_wmem_queued >> 1) > limit && tcp_queue != MATT_FRAG_IN_WRITE_QUEUE &&
		     skb != skb_rb_first(&out->rb_rtx_queue) && skb != skb_rb_last(&out->rb_rtx_queue))) {
#line 474
		MATT_TRACE(0, 2, "sk_wmem_queued over float, wait for space");
		return -ENOMEM;
	}
	if (skb_cloned(skb) && _xm_skb_unclone_keeptruesize(skb, gfp))
		return -ENOMEM;
	buff = matt_stream_alloc_skb(sk, gfp, true);
	if (!buff) {
#line 484
		MATT_TRACE(0, 2, "alloc_skb fail, wait for space");
		return -ENOMEM;
	}
	sk_wmem_queued_add(sk, buff->truesize);
	sk_mem_charge(sk, buff->truesize);
	nlen = skb->len - len;
	buff->truesize += nlen;
	skb->truesize -= nlen;
	MATT_SKB_CB(buff)->offset_start = MATT_SKB_CB(skb)->offset_start + len;
	MATT_SKB_CB(buff)->offset_end = MATT_SKB_CB(skb)->offset_end;
	MATT_SKB_CB(skb)->offset_end = MATT_SKB_CB(buff)->offset_start;
	flags = MATT_SKB_CB(skb)->flags;
	MATT_SKB_CB(skb)->flags = flags & ~0x20;
	MATT_SKB_CB(buff)->flags = flags;
	MATT_SKB_CB(buff)->sacked = MATT_SKB_CB(skb)->sacked;
	xm_skb_split(skb, buff, len);
	skb_set_delivery_time(buff, skb->tstamp, true);
	old_factor = MATT_SKB_CB(skb)->gso_segs;
	matt_set_skb_tso_segs(skb, mss_now);
	matt_set_skb_tso_segs(buff, mss_now);
	MATT_SKB_CB(buff)->tx = MATT_SKB_CB(skb)->tx;
	if (!before(out->snd_nxt, MATT_SKB_CB(buff)->offset_end)) {
		int diff = old_factor - MATT_SKB_CB(skb)->gso_segs - MATT_SKB_CB(buff)->gso_segs;

		if (diff) {
			out->packets_out -= diff;
			if (MATT_SKB_CB(skb)->sacked & 0x01)
				out->sacked_out -= diff;
			if (MATT_SKB_CB(skb)->sacked & 0x02)
				out->retrans_out -= diff;
			if (MATT_SKB_CB(skb)->sacked & 0x04)
				out->lost_out -= diff;
		}
	}
	skb_header_release(buff);
	if (tcp_queue == MATT_FRAG_IN_WRITE_QUEUE)
		__skb_queue_after(&sk->sk_write_queue, skb, buff);
	else
		matt_rbtree_insert(&out->rb_rtx_queue, buff);
	if (tcp_queue == MATT_FRAG_IN_RTX_QUEUE)
		list_add(&buff->tcp_tsorted_anchor, &skb->tcp_tsorted_anchor);
	matt_dump_skb(__func__, skb);
	matt_dump_skb(__func__, buff);
	return 0;
}

static inline bool matt_fclone_busy(struct sock *sk, const struct sk_buff *skb)
{
	const struct sk_buff_fclones *fclones = container_of(skb, struct sk_buff_fclones, skb1);

	return skb->fclone == SKB_FCLONE_ORIG && refcount_read(&fclones->fclone_ref) > 1 &&
	       READ_ONCE(fclones->skb2.sk) == sk;
}

int __matt_retransmit_skb(struct sock *sk, struct sk_buff *skb, int segs)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);

	if (unlikely(matt_fclone_busy(sk, skb))) {
		set_bit(0, &sk->sk_tsq_flags);
		smp_mb__after_atomic();
		if (matt_fclone_busy(sk, skb))
			return -EBUSY;
	}
	if (before(cb->offset_start, out->snd_una)) {
		u16 len, old_segs;

		if (unlikely(before(cb->offset_end, out->snd_una))) {
#line 911
			MATT_TRACE(0, 2, "bug: why retrans acked skb?");
			return -EINVAL;
		}
		len = out->snd_una - cb->offset_start;
		old_segs = cb->gso_segs;
		if (matt_trim_head(sk, skb, out->snd_una - cb->offset_start))
			return -ENOMEM;
		if (len && old_segs != cb->gso_segs) {
			out->send[STATS_FACKED].data_num += (u16)(old_segs - cb->gso_segs);
			out->send[STATS_FACKED].data_bytes += len;
		} else {
			out->send[STATS_FACKED].data_num += max_t(u16, cb->gso_segs, 1);
			out->send[STATS_FACKED].data_bytes += skb->len - 16;
		}
	}
	out->send[STATS_RETRY].data_num += max_t(u16, cb->gso_segs, 1);
	out->send[STATS_RETRY].data_bytes += skb->len - 16;
	return matt_transmit_skb(sk, skb, 1);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
void matt_xmit_retransmit_queue(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_output_ctx *out = &ms->output_ctx;
	struct sk_buff *rtx_head, *skb;
	bool rearm_timer = false;
	int unset;

	rtx_head = skb_rb_first(&out->rb_rtx_queue);
	if (!rtx_head)
		return;
	for (skb = rtx_head; skb; skb = skb_rb_next(skb)) {
		struct matt_skb_cb *cb = MATT_SKB_CB(skb);
		u8 sacked = cb->sacked;
		int quota;

		if (before(out->snd_una, out->highest_sackoffset) && !before(cb->offset_start, out->highest_sackoffset))
			break;
		quota = out->snd_cwnd - (out->packets_out - (out->sacked_out + out->lost_out) + out->retrans_out);
		if (quota <= 0) {
#line 1354
			MATT_TRACE(0, 9, "without cwnd for retrans!");
			break;
		}
		if (out->retrans_out >= out->lost_out) {
#line 1360
			MATT_TRACE(0, 9, "retrans too much, retran=%u lost=%u", out->retrans_out, out->lost_out);
			break;
		}
		if ((sacked & 0x07) != 0x04)
			continue;
		if (matt_small_queue_check(sk, skb, 1))
			break;
		if (__matt_retransmit_skb(sk, skb, unset))
			break;
		cb->sacked |= 0x02 | 0x80;
		out->retrans_out += cb->gso_segs;
		rearm_timer = (skb == rtx_head) | rearm_timer;
#line 1392
		MATT_TRACE(0, 9, "sk=%p seq=%u num=%u retran=%u", sk, cb->offset_start, ms->pkt_mgmt_ctx.next_pn + 1, out->retrans_out);
	}
	if (rearm_timer)
		matt_timer_reset(sk, 5, out->rto);
}
#pragma clang diagnostic pop
