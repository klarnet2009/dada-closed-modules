// SPDX-License-Identifier: GPL-2.0
/* matt UDP-tunnel offload: gso_segment (derived from tcp_gso_segment, ThinLTO-promoted name) and gro_complete.
 * Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"
#include <net/gro.h>

extern struct sk_buff *(*xm_skb_segment)(struct sk_buff *head_skb, netdev_features_t features);
void matt_raw_skb_dump(const uint8_t *fn, const struct sk_buff *skb, bool full_pkt);

/* the stock calls the out-of-line (static) copy of refcount_sub_and_test from the kernel header */
static __used noinline bool matt_refcount_sub_and_test(int i, refcount_t *r) __asm__("refcount_sub_and_test");
static __used noinline bool matt_refcount_sub_and_test(int i, refcount_t *r)
{
	int old = atomic_fetch_sub_release(i, &r->refs);

	if (old == i) {
		smp_acquire__after_ctrl_dep();
		return true;
	}
	if (unlikely(old < 0 || old - i < 0))
		refcount_warn_saturate(r, REFCOUNT_SUB_UAF);
	return false;
}

struct sk_buff *matt_gso_segment(struct sk_buff *skb, netdev_features_t features)
	__asm__("matt_gso_segment.llvm.11802411537102813144") __attribute__((visibility("hidden")));

struct sk_buff *matt_gso_segment(struct sk_buff *skb, netdev_features_t features)
{
	void (*destructor)(struct sk_buff *);
	struct sk_buff *segs, *seg;
	struct matthdr *mh;
	unsigned int mss, step, sum_truesize = 0;
	u32 num, offset;
	bool copy_destructor;
	int delta;

	if (!pskb_may_pull(skb, sizeof(struct matthdr))) {
#line 44
		MATT_TRACE(0, 2, "invalid skb, hlen: %u", skb_headlen(skb));
		return ERR_PTR(-EINVAL);
	}
	__skb_pull(skb, sizeof(struct matthdr));
	mss = skb_shinfo(skb)->gso_size;
	if (skb->len <= mss) {
#line 52
		MATT_TRACE(0, 2, "segs no needed, skb_len:%u <= mss:%u", skb->len, mss);
		return ERR_PTR(-EINVAL);
	}
	destructor = skb->destructor;
	copy_destructor = destructor == matt_data_wfree;
	segs = xm_skb_segment(skb, features);
	if (IS_ERR(segs)) {
#line 61
		MATT_TRACE(0, 2, "segs err: %pe", segs);
		return segs;
	}
	step = skb_shinfo(segs)->gso_size ? skb_shinfo(segs)->gso_segs * mss : mss;
	seg = segs;
	mh = (struct matthdr *)skb_transport_header(seg);
	num = ntohl(mh->num);
	offset = ntohl(mh->offset);
	mh->flags &= ~0x20;
	mh->len = htons(seg->len - sizeof(struct matthdr));
	matt_dump_skb("matt_gso_segment", seg);
	if (seg->next) {
		num++;
		offset += step;
		do {
			struct sk_buff *prev = seg;

			if (copy_destructor) {
				prev->destructor = skb->destructor;
				sum_truesize += prev->truesize;
				prev->sk = skb->sk;
			}
			seg = seg->next;
			mh = (struct matthdr *)skb_transport_header(seg);
			mh->num = htonl(num);
			mh->offset = htonl(offset);
			mh->flags &= ~0x20;
			mh->len = htons(seg->len - sizeof(struct matthdr));
			matt_dump_skb("matt_gso_segment", seg);
			num++;
			offset += step;
		} while (seg->next);
	}
	mh->flags |= 0x20;
	if (copy_destructor) {
		swap(skb->sk, seg->sk);
		swap(skb->destructor, seg->destructor);
		sum_truesize += seg->truesize;
		delta = sum_truesize - skb->truesize;
		if (likely(delta >= 0))
			refcount_add(delta, &seg->sk->sk_wmem_alloc);
		else
			WARN_ON_ONCE(refcount_sub_and_test(-delta, &seg->sk->sk_wmem_alloc));
	}
	return segs;
}

int matt_gro_complete(struct sock *sk, struct sk_buff *skb, int nhoff)
{
	struct matthdr *mh;
	u32 len;

	if (skb->encapsulation)
		skb->inner_transport_header = skb->transport_header;
	skb_shinfo(skb)->gso_segs = NAPI_GRO_CB(skb)->count;
	mh = (struct matthdr *)(skb->data + nhoff);
	len = skb->len - nhoff - sizeof(struct matthdr);
	mh->len = htons(len);
#line 581
	MATT_TRACE(0, 9, "mh num:%u offset:%u reserved:%u flags:%d type:%d len:%u", ntohl(mh->num), ntohl(mh->offset), ntohl(mh->reserved),
		   mh->flags, mh->type, (u16)len);
	matt_raw_skb_dump("matt_gro_complete", skb, false);
	return 0;
}
