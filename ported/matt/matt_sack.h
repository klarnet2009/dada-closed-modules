// SPDX-License-Identifier: GPL-2.0
/* matt SACK tagging (stock: mp_input.c, derived from tcp_sacktag_write_queue / tcp_sacktag_walk / tcp_match_skb_to_sack / tcp_sacktag_one)
 * and clean_rtx_queue. Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

#define MATT_SACKED_ACKED 0x01
#define MATT_SACKED_RETRANS 0x02
#define MATT_LOST 0x04
#define MATT_EVER_RETRANS 0x80

static __always_inline struct sk_buff *matt_sacktag_bsearch(struct sock *sk, u32 seq, u32 end)
{
	struct sk_buff *skb;
	struct rb_node *p = MATT_SK(sk)->output_ctx.rb_rtx_queue.rb_node;

	while (p) {
		skb = rb_to_skb(p);
		if (before(seq, MATT_SKB_CB(skb)->offset_start)) {
			p = p->rb_left;
		} else if (!before(seq, MATT_SKB_CB(skb)->offset_end)) {
			p = p->rb_right;
		} else {
#line 1123
			MATT_TRACE(0, 9, "seq( %u, %u ) start:%u, skb start:%u", MATT_SKB_CB(skb)->offset_start, MATT_SKB_CB(skb)->offset_end);
			return skb;
		}
	}
	return NULL;
}

static noinline void matt_sacktag_write_queue(struct sock *sk, struct sk_buff *skb, struct matt_sacktag_state *state)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	struct matthdr *mh = (struct matthdr *)skb->data;
	struct matt_sack_block_wire *wire;
	struct sk_buff *cur = NULL;
	int sz, num, used = 0, i, j;
	u32 start = 0, end;

	if (mh->type == MATT_MGMT_TYPE_NACK) {
		wire = (struct matt_sack_block_wire *)(skb->data + 0x18);
		sz = ntohl(*(__be32 *)(skb->data + 0x14));
	} else if (mh->type == MATT_MGMT_TYPE_PACK) {
		wire = (struct matt_sack_block_wire *)(skb->data + 0x34);
		sz = ntohl(*(__be32 *)(skb->data + 0x30));
	} else {
		return;
	}
	num = sz >> 3;
	if (num > 128)
		num = 128;
	if (!num)
		return;
#line 1175
	MATT_TRACE(0, 9, "sack found:num=%d!", num);
	if (sz >= 8) {
		for (i = 0; i < num; i++) {
			outq->sacks[i].offset_start = ntohl(wire[i].offset_start);
			outq->sacks[i].offset_end = ntohl(wire[i].offset_end);
#line 1183
			MATT_TRACE(0, 9, "sacks:[%u->%u], used_sacks=%d", outq->sacks[i].offset_start, outq->sacks[i].offset_end, sz);
		}
		used = num;
		for (i = used - 1; i > 0; i--) {
			for (j = 0; j < i; j++) {
				if (before(outq->sacks[j + 1].offset_start, outq->sacks[j].offset_start))
					swap(outq->sacks[j], outq->sacks[j + 1]);
			}
		}
		for (i = 0; i < used; i++) {
			start = outq->sacks[i].offset_start;
			end = outq->sacks[i].offset_end;
			if (!(cur && before(start, MATT_SKB_CB(cur)->offset_start))) {
				cur = matt_sacktag_bsearch(sk, start, end);
				if (!cur)
					goto next;
			}
#line 1209
			MATT_TRACE(0, 9, "matchup(%u, %u) num_sacks:%u used_sacks:%d", MATT_SKB_CB(cur)->offset_start, MATT_SKB_CB(cur)->offset_end, num,
				   used);
			for (; cur; cur = rb_to_skb(rb_next(&cur->rbnode))) {
				struct matt_skb_cb *cb = MATT_SKB_CB(cur);
				bool in_sack;
				s32 diff;
				u32 pkt_len, mss;
				u8 sacked;

				if (!before(cb->offset_start, end)) {
#line 1075
					MATT_TRACE(0, 9, "skb is out of sack, endseq:%d seq( %u, %u )", end, cb->offset_start, cb->offset_end);
					break;
				}
				diff = cb->offset_start - start;
				in_sack = diff >= 0 && !before(end, cb->offset_end);
				if (MATT_SKB_CB(cur)->gso_segs >= 2 && !in_sack) {
					if (!before(start, cb->offset_end))
						break;
					mss = cb->gso_size;
					if (diff < 0) {
						pkt_len = start - cb->offset_start;
						if (pkt_len < mss)
							pkt_len = mss;
					} else {
						pkt_len = end - cb->offset_start;
						if (pkt_len < mss)
							break;
					}
					if (pkt_len > mss) {
						u32 new_len = pkt_len / mss * mss;

						if (diff < 0 && new_len < pkt_len)
							new_len += mss;
						pkt_len = new_len;
#line 950
						MATT_TRACE(0, 9, "pkg_len=%u > mss=%u", pkt_len, mss);
					}
					if (diff < 0 && pkt_len >= cur->len) {
#line 955
						MATT_TRACE(0, 2, "should never here, pkg_len=%u skb_len=%u", pkt_len, cur->len);
						break;
					}
#line 961
					MATT_TRACE(0, 9, "skb(%u, %u) sack(%u, %u) pkg_len=%u mss=%u", cb->offset_start, cb->offset_end, start, end,
						   pkt_len, mss);
					if (matt_fragment(sk, MATT_FRAG_IN_RTX_QUEUE, cur, pkt_len, mss, GFP_ATOMIC) < 0)
						break;
					in_sack = diff >= 0;
				}
				if (!in_sack)
					break;
				sacked = cb->sacked;
				if (before(outq->snd_una, cb->offset_end) && !(sacked & MATT_SACKED_ACKED)) {
					u64 xmit_us = div_u64(cur->skb_mstamp_ns, NSEC_PER_USEC);
					u32 rtt_us = max_t(s64, MATT_SK(sk)->pkt_mgmt_ctx.matt_mstamp - xmit_us, 0);
					u32 segs = MATT_SKB_CB(cur)->gso_segs;

					if (!((sacked & (MATT_SACKED_RETRANS | MATT_EVER_RETRANS)) && outq->srtt > rtt_us)) {
						outq->rack.rtt_us = rtt_us;
						outq->rack.advanced = 1;
						if (xmit_us > outq->rack.mstamp ||
						    (xmit_us == outq->rack.mstamp && before(outq->rack.end_seq, cb->offset_end))) {
							outq->rack.mstamp = xmit_us;
							outq->rack.end_seq = cb->offset_end;
						}
					}
					if (sacked & MATT_SACKED_RETRANS) {
						if (sacked & MATT_LOST) {
							outq->lost_out -= segs;
							outq->retrans_out -= segs;
							sacked &= ~(MATT_LOST | MATT_SACKED_RETRANS);
						}
					} else if (sacked & MATT_LOST) {
						outq->lost_out -= segs;
						sacked &= ~MATT_LOST;
					}
					state->flag |= 0x20;
					sacked |= MATT_SACKED_ACKED;
					outq->sacked_out += segs;
					state->sack_delivered += segs;
#line 1056
					MATT_TRACE(0, 9, "find seq( %d, %d ) in sack, sacked_out=%u", cb->offset_start, cb->offset_end, outq->sacked_out);
				}
				cb->sacked = sacked;
				matt_rate_skb_delivered(sk, cur, state->rate);
				if (cb->sacked & MATT_SACKED_ACKED)
					list_del_init(&cur->tcp_tsorted_anchor);
			}
next:
#line 1219
			MATT_TRACE(0, 9, "sack#%d(%u, %u) num_sacks:%u used_sacks:%d highest_off:%u", i, start, end, num, used, start);
		}
	}
	outq->highest_sackoffset = start;
	outq->sack_used = used;
	outq->first_sackoffset = outq->sacks[0].offset_start;
}

static __always_inline void matt_rtx_queue_unlink_and_free(struct sock *sk, struct sk_buff *skb)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	struct matt_data_stats *ds = &outq->send[STATS_FACKED];

	ds->data_num += max_t(u16, MATT_SKB_CB(skb)->gso_segs, 1);
	ds->data_bytes += skb->len - sizeof(struct matthdr);
#line 741
	MATT_TRACE(0, 9, "sk=%p skb:[%u->%u]", sk, MATT_SKB_CB(skb)->offset_start, MATT_SKB_CB(skb)->offset_end);
	__list_del_entry(&skb->tcp_tsorted_anchor);
	skb->tcp_tsorted_anchor.next = NULL;
	skb->tcp_tsorted_anchor.prev = NULL;
	rb_erase(&skb->rbnode, &outq->rb_rtx_queue);
	matt_wmem_free_skb(sk, skb);
}

/* partial ack: account the trimmed part (send[idx]) */
static __always_inline void matt_trim_stats(struct matt_data_stats *ds, struct sk_buff *skb, u32 trimmed, u32 old_segs)
{
	u32 segs = MATT_SKB_CB(skb)->gso_segs;

	if (!(u16)trimmed || old_segs == segs) {
		ds->data_num += max_t(u32, segs, 1);
		ds->data_bytes += skb->len - sizeof(struct matthdr);
	} else {
		ds->data_num += (u16)(old_segs - segs);
		ds->data_bytes += (u16)trimmed;
	}
}

static __always_inline void matt_ack_update_rtt(struct sock *sk, struct matt_sacktag_state *state)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	u32 old_srtt, srtt, base, diff, abs_del, pct;
	int over;

	if (!outq->cur_rtt) {
#line 598
		MATT_TRACE(0, 9, "sk:%p | invalid rtt", sk);
		return;
	}
	old_srtt = outq->srtt;
	state->rate->rtt_us = outq->cur_rtt;
	if (outq->cur_rtt < outq->min_rtt) {
#line 606
		MATT_TRACE(0, 4, "min rtt update sk:%p | old=%u us → new=%u us", sk, outq->min_rtt, outq->cur_rtt);
		outq->min_rtt = outq->cur_rtt;
	}
	base = outq->cur_rtt > outq->min_rtt ? outq->cur_rtt : outq->min_rtt;
	srtt = (base + outq->srtt * 7) >> 3;
	outq->srtt = srtt;
	diff = base > srtt ? base - srtt : srtt - base;
	outq->rtt_var = (outq->rtt_var * 3 + diff) >> 2;
	outq->rto = srtt << 1;
	if (!outq->announce_srtt) {
#line 625
		MATT_TRACE(0, 4, "rtt action sk:%p | send matt report old=%u us → new=%u us", sk, 0, srtt);
		matt_send_report(sk);
		srtt = outq->srtt;
		outq->announce_srtt = srtt;
		over = 0;
	} else {
		abs_del = old_srtt > srtt ? old_srtt - srtt : srtt - old_srtt;
		if (abs_del * 100 >= old_srtt * 5 * 2) {
			matt_send_report(sk);
			pct = abs_del * 100 / old_srtt;
#line 641
			MATT_TRACE(0, 3, "rtt change sk:%p | over th %u%% old srtt=%u us, cur srtt=%u us, abs del rtt=%u us, delta percent=%u%%", sk, 10,
				   old_srtt, srtt, abs_del, pct);
			outq->announce_srtt = outq->srtt;
			srtt = outq->srtt;
			over = 1;
		} else {
			over = 0;
		}
	}
#line 651
	MATT_TRACE(0, 9, "rtt info sk:%p | cur_rtt=%u us, min_rtt=%u us, srtt=%u us, rtt_var=%u us, rto=%u us, change_over=%d (th=%u%%)", sk,
		   outq->cur_rtt, outq->min_rtt, srtt, outq->rtt_var, outq->rto, over, 10);
}

static noinline void matt_clean_rtx_queue(struct sock *sk, u32 ack_offset, struct matt_sacktag_state *state)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	struct rb_root *root = &outq->rb_rtx_queue;
	struct rb_node *p = rb_first(root);
	struct sk_buff *skb;
	const struct matt_sack_block *sbase = &outq->sacks[0];

	while (p) {
		struct matt_skb_cb *cb;
		struct rb_node *next;
		u32 acked, end, k, used;
		bool fully;
		u8 sacked;

		skb = rb_to_skb(p);
		cb = MATT_SKB_CB(skb);
		end = cb->offset_end;
		sacked = cb->sacked;
		if (!before(ack_offset, end)) {
			acked = MATT_SKB_CB(skb)->gso_segs;
			fully = true;
		} else {
			u32 start = cb->offset_start;
			s32 diff = start - ack_offset;
			u32 old_segs, trimmed;

			used = outq->sack_used;
			if (!used) {
				if (diff >= 0)
					break;
			} else if (diff >= 0) {
				if (before(outq->highest_sackoffset, start))
					break;
				for (k = 0; k < used; k++) {
					const struct matt_sack_block *sb = sbase + (s32)k; /* pointer: no array-bounds trap, stock has none */
#line 668
					MATT_TRACE(0, 9, "sack#%u seq(%u, %u)", k, sb->offset_start, sb->offset_end);
					if (before(cb->offset_start, sb->offset_start))
						continue;
					if (before(sb->offset_end, cb->offset_start))
						continue;
					if ((s32)(sb->offset_end - cb->offset_end) >= 0) {
#line 694
						MATT_TRACE(0, 9, "fullack in sack skb(%u, %u) sack(%u, %u)", cb->offset_start, cb->offset_end,
							   sb->offset_start, sb->offset_end);
						acked = MATT_SKB_CB(skb)->gso_segs;
						fully = true;
						goto account;
					}
#line 674
					MATT_TRACE(0, 9, "partack in sack skb(%u, %u) sack(%u, %u)", cb->offset_start, cb->offset_end,
						   sb->offset_start, sb->offset_end);
					trimmed = sb->offset_end - cb->offset_start;
					old_segs = MATT_SKB_CB(skb)->gso_segs;
					if (matt_trim_head(sk, skb, trimmed)) {
						acked = 0;
						fully = false;
						goto account;
					}
					matt_trim_stats(&outq->send[STATS_SACKED], skb, trimmed, old_segs);
#line 685
					MATT_TRACE(0, 9, "partack trim out skb(%u, %u)", cb->offset_start, cb->offset_end);
					acked = old_segs - MATT_SKB_CB(skb)->gso_segs;
					fully = false;
					goto account;
				}
				acked = 0;
				fully = false;
				goto account;
			}
			/* the skb straddles ack_offset */
			BUG_ON(!before(outq->snd_una, end));
			trimmed = outq->snd_una - start;
			old_segs = MATT_SKB_CB(skb)->gso_segs;
			if (matt_trim_head(sk, skb, trimmed)) {
				acked = 0;
				fully = false;
				goto account;
			}
			matt_trim_stats(&outq->send[STATS_FACKED], skb, trimmed, old_segs);
			acked = old_segs - MATT_SKB_CB(skb)->gso_segs;
			if (acked) {
				BUG_ON(!MATT_SKB_CB(skb)->gso_segs);
				BUG_ON(!before(cb->offset_start, cb->offset_end));
			}
			fully = false;
		}
account:
		if (!(sacked & (MATT_SACKED_RETRANS | MATT_EVER_RETRANS))) {
			u64 xmit_us = div_u64(skb->skb_mstamp_ns, NSEC_PER_USEC);
			u32 rtt_us = max_t(s64, MATT_SK(sk)->pkt_mgmt_ctx.matt_mstamp - xmit_us, 0);

			if (!((sacked & (MATT_SACKED_RETRANS | MATT_EVER_RETRANS)) && outq->srtt > rtt_us)) {
				outq->rack.rtt_us = rtt_us;
				outq->rack.advanced = 1;
				if (xmit_us > outq->rack.mstamp || (xmit_us == outq->rack.mstamp && before(outq->rack.end_seq, cb->offset_end))) {
					outq->rack.mstamp = xmit_us;
					outq->rack.end_seq = cb->offset_end;
				}
			}
		} else if (cb->sacked & MATT_SACKED_RETRANS) {
			outq->retrans_out -= acked;
		}
		if (sacked & MATT_SACKED_ACKED)
			outq->sacked_out -= acked;
		if (sacked & MATT_LOST)
			outq->lost_out -= acked;
		outq->packets_out -= acked;
		next = rb_next(p);
		if (acked && fully) {
			outq->delivered += acked;
			matt_rate_skb_delivered(sk, skb, state->rate);
			matt_rtx_queue_unlink_and_free(sk, skb);
		}
		p = next;
	}
	matt_ack_update_rtt(sk, state);
}
