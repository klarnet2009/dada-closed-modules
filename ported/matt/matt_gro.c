// SPDX-License-Identifier: GPL-2.0
/* matt GRO receive: tcp_gro_receive() (6.6.57) with the skb_gro_receive() clone inlined and the matt header logic.
 * Reconstructed from the OS4 stock disassembly (matt_gro_receive, 451 insns). DO NOT LOAD before review. */
#include "matt.h"
#include <net/gro.h>

void matt_raw_skb_dump(const uint8_t *fn, const struct sk_buff *skb, bool full_pkt);

/* skb_gro_receive() of 6.6.57 (the stock inlines it): no skb_zcopy() check and no SKBFL_SHARED_FRAG propagation as in 6.6.144 */
static __always_inline int matt_skb_gro_receive(struct sk_buff *p, struct sk_buff *skb)
{
	struct skb_shared_info *pinfo, *skbinfo = skb_shinfo(skb);
	unsigned int offset = skb_gro_offset(skb);
	unsigned int headlen = skb_headlen(skb);
	unsigned int len = skb_gro_len(skb);
	unsigned int delta_truesize;
	unsigned int new_truesize;
	struct sk_buff *lp;
	int segs;

	if (p->pp_recycle != skb->pp_recycle)
		return -ETOOMANYREFS;

	unsigned int max_size = netif_get_gro_max_size(p->dev, p);

	if (unlikely(p->len + len >= max_size || NAPI_GRO_CB(skb)->flush))
		return -E2BIG;

	if (unlikely(p->len + len >= GRO_LEGACY_MAX_SIZE)) {
		if (NAPI_GRO_CB(skb)->proto != IPPROTO_TCP ||
		    (p->protocol == htons(ETH_P_IPV6) && skb_headroom(p) < sizeof(struct hop_jumbo_hdr)) ||
		    p->encapsulation)
			return -E2BIG;
	}

	segs = NAPI_GRO_CB(skb)->count;
	lp = NAPI_GRO_CB(p)->last;
	pinfo = skb_shinfo(lp);

	if (headlen <= offset) {
		skb_frag_t *frag;
		skb_frag_t *frag2;
		int i = skbinfo->nr_frags;
		int nr_frags = pinfo->nr_frags + i;

		if (nr_frags > MAX_SKB_FRAGS)
			goto merge;

		offset -= headlen;
		pinfo->nr_frags = nr_frags;
		skbinfo->nr_frags = 0;

		frag = pinfo->frags + nr_frags;
		frag2 = skbinfo->frags + i;
		do {
			*--frag = *--frag2;
		} while (--i);

		skb_frag_off_add(frag, offset);
		skb_frag_size_sub(frag, offset);

		new_truesize = SKB_TRUESIZE(skb_end_offset(skb));
		delta_truesize = skb->truesize - new_truesize;

		skb->truesize = new_truesize;
		skb->len -= skb->data_len;
		skb->data_len = 0;

		NAPI_GRO_CB(skb)->free = NAPI_GRO_FREE;
		goto done;
	} else if (skb->head_frag) {
		int nr_frags = pinfo->nr_frags;
		skb_frag_t *frag = pinfo->frags + nr_frags;
		struct page *page = virt_to_head_page(skb->head);
		unsigned int first_size = headlen - offset;
		unsigned int first_offset;

		if (nr_frags + 1 + skbinfo->nr_frags > MAX_SKB_FRAGS)
			goto merge;

		first_offset = skb->data - (unsigned char *)page_address(page) + offset;

		pinfo->nr_frags = nr_frags + 1 + skbinfo->nr_frags;

		skb_frag_fill_page_desc(frag, page, first_offset, first_size);

		memcpy(frag + 1, skbinfo->frags, sizeof(*frag) * skbinfo->nr_frags);

		new_truesize = SKB_DATA_ALIGN(sizeof(struct sk_buff));
		delta_truesize = skb->truesize - new_truesize;
		skb->truesize = new_truesize;
		NAPI_GRO_CB(skb)->free = NAPI_GRO_FREE_STOLEN_HEAD;
		goto done;
	}

merge:
	skb->destructor = NULL;
	skb->sk = NULL;
	delta_truesize = skb->truesize;
	if (offset > headlen) {
		unsigned int eat = offset - headlen;

		skb_frag_off_add(&skbinfo->frags[0], eat);
		skb_frag_size_sub(&skbinfo->frags[0], eat);
		skb->data_len -= eat;
		skb->len -= eat;
		offset = headlen;
	}

	__skb_pull(skb, offset);

	if (NAPI_GRO_CB(p)->last == p)
		skb_shinfo(p)->frag_list = skb;
	else
		NAPI_GRO_CB(p)->last->next = skb;
	NAPI_GRO_CB(p)->last = skb;
	__skb_header_release(skb);
	lp = p;

done:
	NAPI_GRO_CB(p)->count += segs;
	p->data_len += len;
	p->truesize += delta_truesize;
	p->len += len;
	if (lp != p) {
		lp->data_len += len;
		lp->truesize += delta_truesize;
		lp->len += len;
	}
	NAPI_GRO_CB(skb)->same_flow = 1;
	return 0;
}

/* first held packet of the flow candidates; the stock keeps an out-of-line copy besides the inlined one (mh is unused) */
struct sk_buff *matt_gro_lookup(struct list_head *head, struct matthdr *mh)
{
	struct sk_buff *p;

	list_for_each_entry(p, head, list) {
		if (NAPI_GRO_CB(p)->same_flow)
			return p;
	}
	return NULL;
}

/* tcp_gro_receive() without the tcp flag logic: the mgmt bit (0x80 in flags) flushes, the stream is checked by the offset */
struct sk_buff *matt_gro_receive(struct sock *sk, struct list_head *head, struct sk_buff *skb)
{
	struct sk_buff *pp = NULL;
	struct sk_buff *p;
	struct matthdr *mh;
	struct matthdr *mh2;
	unsigned int len;
	unsigned int hlen;
	unsigned int off;
	unsigned int mss = 1;
	int flush = 1;

	off = skb_gro_offset(skb);
	hlen = off + sizeof(*mh);
	mh = skb_gro_header(skb, hlen, off);
	if (unlikely(!mh)) {
#line 433
		MATT_TRACE(0, 2, "invalid skb_gro_header!");
		goto out;
	}
#line 442
	MATT_TRACE(0, 9, "mh1 num:%u offset:%u reserved:%u flags:%d type:%d len:%u", ntohl(mh->num), ntohl(mh->offset), ntohl(mh->reserved),
		   mh->flags, mh->type, ntohs(mh->len));
	if ((s8)mh->flags < 0) {
#line 446
		MATT_TRACE(0, 9, "mgmt pkg, flush it");
		goto out;
	}
	if (skb_gro_header_hard(skb, hlen)) {
		mh = skb_gro_header_slow(skb, hlen, off);
		if (unlikely(!mh)) {
#line 457
			MATT_TRACE(0, 2, "invalid matt header!");
			goto out;
		}
	}
	skb_gro_pull(skb, sizeof(*mh));
	len = skb_gro_len(skb);
#line 467
	MATT_TRACE(0, 9, "off=%u hlen=%u mhlen=%u skb_gro_len=%u", off, hlen, (unsigned int)sizeof(*mh), len);

	p = matt_gro_lookup(head, mh);
	if (p) {
		mh2 = (struct matthdr *)(p->data + off);
#line 483
		MATT_TRACE(0, 9, "mh2 num:%u offset:%u reserved:%u flags:%d type:%d len:%u", ntohl(mh2->num), ntohl(mh2->offset), ntohl(mh2->reserved),
			   mh2->flags, mh2->type, ntohs(mh2->len));
		flush = NAPI_GRO_CB(p)->flush;
		if (NAPI_GRO_CB(p)->flush_id != 1 || NAPI_GRO_CB(p)->count != 1 || !NAPI_GRO_CB(p)->is_atomic)
			flush |= NAPI_GRO_CB(p)->flush_id;
		else
			NAPI_GRO_CB(p)->is_atomic = false;
		mss = skb_shinfo(p)->gso_size;
		if (unlikely(skb_shinfo(skb)->gso_size))
			flush |= mss != skb_shinfo(skb)->gso_size;
		else
			flush |= (len - 1) >= mss;
		flush |= (ntohl(mh2->offset) + skb_gro_len(p)) ^ ntohl(mh->offset);
		if (flush || matt_skb_gro_receive(p, skb))
			mss = 1;
		else
			mh2->flags |= mh->flags;
	}

	if (unlikely(skb_shinfo(skb)->gso_size))
		flush = len != NAPI_GRO_CB(skb)->count * skb_shinfo(skb)->gso_size;
	else
		flush = len < mss;
	if (p && (!NAPI_GRO_CB(skb)->same_flow || flush)) {
		pp = p;
		matt_dump_skb(__func__, pp);
		matt_raw_skb_dump(__func__, pp, false);
	}
out:
	NAPI_GRO_CB(skb)->flush |= (flush != 0);
	return pp;
}
