// SPDX-License-Identifier: GPL-2.0
/* matt_raw_skb_dump: net/core/skbuff.c skb_dump() with printk replaced by matt_trace_msg and print_hex_dump inlined (4 copies).
 * Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"
#include <linux/highmem.h>

static __always_inline void matt_hex_dump_off16(const char *fn, const char *prefix_str, const void *buf, size_t len)
{
	const u8 *ptr = buf;
	int i, linelen, remaining = len;
	unsigned char linebuf[131] = {};

	for (i = 0; i < len; i += 16) {
		linelen = min(remaining, 16);
		remaining -= 16;
		hex_dump_to_buffer(ptr + i, linelen, 16, 1, linebuf, sizeof(linebuf), false);
#line 58
		matt_trace_msg(0, 9, "%s: %d: %s%s%.8x: %s", "matt_hex_dump", __LINE__, fn, prefix_str, i, linebuf);
	}
}

void matt_raw_skb_dump(const uint8_t *fn, const struct sk_buff *skb, bool full_pkt)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	struct net_device *dev = skb->dev;
	struct sock *sk = skb->sk;
	struct sk_buff *list_skb;
	bool has_mac, has_trans;
	int headroom, tailroom;
	int i, len, seg_len;

	if (full_pkt)
		len = skb->len;
	else
		len = min_t(int, skb->len, MAX_HEADER + 128);

	headroom = skb_headroom(skb);
	tailroom = skb_tailroom(skb);

	has_mac = skb_mac_header_was_set(skb);
	has_trans = skb_transport_header_was_set(skb);

#line 108
	MATT_TRACE(0, 9, "%s skb len=%u headroom=%u headlen=%u tailroom=%u mac=(%d,%d) mac_len=%u net=(%d,%d) trans=%d shinfo(txflags=%u nr_frags=%u gso(size=%hu type=%u segs=%hu)) csum(0x%x ip_summed=%u complete_sw=%u valid=%u level=%u) hash(0x%x sw=%u l4=%u) proto=0x%04x pkttype=%u iif=%d",
		   fn, skb->len, headroom, skb_headlen(skb), tailroom, has_mac ? skb->mac_header : -1,
		   has_mac ? skb_mac_header_len(skb) : -1, skb->mac_len, skb->network_header,
		   has_trans ? skb_network_header_len(skb) : -1, has_trans ? skb->transport_header : -1, sh->tx_flags, sh->nr_frags,
		   sh->gso_size, sh->gso_type, sh->gso_segs, skb->csum, skb->ip_summed, skb->csum_complete_sw, skb->csum_valid,
		   skb->csum_level, skb->hash, skb->sw_hash, skb->l4_hash, ntohs(skb->protocol), skb->pkt_type, skb->skb_iif);

	if (dev) {
#line 117
		MATT_TRACE(0, 9, "%s dev name=%s feat=%pNF\n", fn, dev->name, &dev->features);
	}
	if (sk) {
#line 120
		MATT_TRACE(0, 9, "%s sk family=%hu type=%u proto=%u\n", fn, sk->sk_family, sk->sk_type, sk->sk_protocol);
	}

	if (full_pkt && headroom)
		matt_hex_dump_off16(fn, "skb headroom: ", skb->head, headroom);

	seg_len = min_t(int, skb_headlen(skb), len);
	if (seg_len)
		matt_hex_dump_off16(fn, "skb linear:   ", skb->data, seg_len);
	len -= seg_len;

	if (full_pkt && tailroom)
		matt_hex_dump_off16(fn, "skb tailroom: ", skb_tail_pointer(skb), tailroom);

	for (i = 0; len && i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		u32 p_off, p_len, copied;
		struct page *p;
		u8 *vaddr;

		skb_frag_foreach_page(frag, skb_frag_off(frag), skb_frag_size(frag), p, p_off, p_len, copied) {
			seg_len = min_t(int, p_len, len);
			vaddr = kmap_atomic(p);
			matt_hex_dump_off16(fn, "skb frag:     ", vaddr + p_off, seg_len);
			kunmap_atomic(vaddr);
			len -= seg_len;
			if (!len)
				break;
		}
	}

	if (full_pkt && skb_has_frag_list(skb)) {
#line 158
		MATT_TRACE(0, 9, "skb fraglist:");
		skb_walk_frags(skb, list_skb)
			matt_raw_skb_dump(fn, list_skb, true);
	}
}
