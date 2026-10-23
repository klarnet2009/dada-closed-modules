// SPDX-License-Identifier: GPL-2.0
/* matt queue helpers (stock: mp_tx.c / mp_rx.c): push, alloc skb, purge queues, reset, rcv buffer accounting */
#define sk_stream_moderate_sndbuf hdr_sk_stream_moderate_sndbuf
#include "matt.h"
#undef sk_stream_moderate_sndbuf

unsigned int matt_skb_rbtree_purge(struct rb_root *root);
extern int (*xm_sk_mem_schedule)(struct sock *sk, int size, int kind);

int matt_wmem_schedule(struct sock *sk, int copy)
{
	if (sk->sk_prot->memory_allocated && copy - sk->sk_forward_alloc >= 1) {
		if (!xm_sk_mem_schedule(sk, copy - sk->sk_forward_alloc, 0))
			copy = 0;
	}
	return copy;
}

static noinline void sk_stream_moderate_sndbuf(struct sock *sk)
{
	u32 val;

	if (sk->sk_userlocks & SOCK_SNDBUF_LOCK)
		return;
	val = min(sk->sk_sndbuf, sk->sk_wmem_queued >> 1);
	val = max_t(u32, val, sk_unused_reserved_mem(sk));
	WRITE_ONCE(sk->sk_sndbuf, max_t(u32, val, SOCK_MIN_SNDBUF));
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
void __matt_push(struct sock *sk)
{
	long unset __attribute__((uninitialized));

	if (sk->sk_state == MATT_SS_CLOSED)
		return;
	matt_write_xmit(sk, unset);
}
#pragma clang diagnostic pop

void matt_push(struct sock *sk, int size_goal)
{
	struct sk_buff *skb = skb_peek_tail(&sk->sk_write_queue);

	if (!skb)
		return;
	if (skb->len < size_goal && READ_ONCE(MATT_SK(sk)->output_ctx.rb_rtx_queue.rb_node) &&
	    refcount_read(&sk->sk_wmem_alloc) > skb->truesize) {
		if (refcount_read(&sk->sk_wmem_alloc) > skb->truesize)
			return;
	}
	if (sk->sk_state == MATT_SS_CLOSED)
		return;
	matt_write_xmit(sk, size_goal);
}

void matt_remove_empty_skb(struct sock *sk)
{
	struct sk_buff *skb = skb_peek_tail(&sk->sk_write_queue);

	if (skb && MATT_SKB_CB(skb)->offset_start == MATT_SKB_CB(skb)->offset_end) {
		skb->_skb_refdst = 0;
		skb->destructor = NULL;
		__skb_unlink(skb, &sk->sk_write_queue);
		matt_wmem_free_skb(sk, skb);
	}
}

/* G56a: the stock inlines a boolean form of matt_wmem_schedule() here (no zero test of the size) */
static __always_inline bool matt_wmem_ok_inl(struct sock *sk, int copy)
{
	if (sk->sk_prot->memory_allocated && copy - sk->sk_forward_alloc >= 1)
		return xm_sk_mem_schedule(sk, copy - sk->sk_forward_alloc, 0) != 0;
	return true;
}

struct sk_buff *matt_stream_alloc_skb(struct sock *sk, gfp_t gfp, bool force)
{
	struct sk_buff *skb = alloc_skb_fclone(MAX_TCP_HEADER, gfp);

	if (skb) {
		skb->truesize = SKB_TRUESIZE(skb_end_offset(skb));
		if (matt_wmem_ok_inl(sk, skb->truesize)) {
			skb_reserve(skb, MAX_TCP_HEADER);
			skb->ip_summed = CHECKSUM_UNNECESSARY;	/* G1ad: stock and w,#0xffffffbf; orr #0x20 = 1 */
			INIT_LIST_HEAD(&skb->tcp_tsorted_anchor);
			return skb;
		}
		kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
	} else {
#line 697
		MATT_TRACE(0, 2, "no memory, enter_memory_pressure");
		sk->sk_prot->enter_memory_pressure(sk);
		sk_stream_moderate_sndbuf(sk);
	}
	return NULL;
}

static inline int matt_xmit_size_goal(struct sock *sk, u32 mss_now)
{
	struct matt_sock *msk = MATT_SK(sk);
	u32 max_window = msk->output_ctx.max_window;
	int cutoff, new_size_goal;
	u32 size_goal;

	cutoff = max_window > 0x5b0 ? max_window >> 1 : max_window;
	new_size_goal = sk->sk_gso_max_size;
	if (cutoff && new_size_goal > cutoff)
		new_size_goal = cutoff;
	size_goal = msk->gso_segs * mss_now;
	if (unlikely(new_size_goal < size_goal || new_size_goal >= size_goal + mss_now)) {
		msk->gso_segs = min_t(u16, new_size_goal / mss_now, sk->sk_gso_max_segs);
		size_goal = msk->gso_segs * mss_now;
	}
#line 1849
	MATT_TRACE(0, 9, "size_goal=%u new_size_goal=%u gso_max_size=%u gso_segs=%u", size_goal, new_size_goal, sk->sk_gso_max_size, msk->gso_segs);
	return max(size_goal, mss_now);
}

int matt_send_mss(struct sock *sk, int *size_goal, int flags)
{
	struct matt_sock *msk = MATT_SK(sk);
	int mss_now;

	matt_update_mss(sk, &msk->dst.addr.addr);
	mss_now = msk->mss;
	*size_goal = matt_xmit_size_goal(sk, mss_now);
	return mss_now;
}

noinline int matt_skb_copy_to_page_nocache(struct sock *sk, struct iov_iter *from, struct sk_buff *skb, struct page *page, int off, int copy)
{
	size_t n = copy_from_iter(page_address(page) + off, copy, from);

	if (n != copy) {
		iov_iter_revert(from, n);
		return -EFAULT;
	}
	skb->len += copy;
	skb->data_len += copy;
	skb->truesize += copy;
	sk->sk_wmem_queued += copy;
	sk_mem_charge(sk, copy);
	return 0;
}

void matt_cleanup_rbuf(struct sock *sk, int copied)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	int space = READ_ONCE(sk->sk_rcvbuf) - (READ_ONCE(sk->sk_backlog.len) + atomic_read(&sk->sk_rmem_alloc));
	u32 new_win;

	if (!copied)
		return;
	new_win = max(space >> 2, 0);
	if (space >= 4 && new_win >= (in->rcv_wnd << 1)) {
#line 46
		MATT_TRACE(0, 9, "new win=%u last win=%u", new_win, in->rcv_wnd);
		matt_send_eack(sk);
	}
}

unsigned int matt_inq_free(struct sock *sk)
{
	return matt_skb_rbtree_purge(&MATT_SK(sk)->input_ctx.out_of_order_queue);
}

void matt_rtx_queue_purge(struct sock *sk)
{
	struct rb_node *p = rb_first(&MATT_SK(sk)->output_ctx.rb_rtx_queue);

	while (p) {
		struct sk_buff *skb = rb_to_skb(p);

		matt_dump_skb("matt_rtx_queue_purge", skb);
		p = rb_next(p);
		skb->_skb_refdst = 0;
		skb->destructor = NULL;
		rb_erase(&skb->rbnode, &MATT_SK(sk)->output_ctx.rb_rtx_queue);
		matt_wmem_free_skb(sk, skb);
	}
}

void matt_outq_free(struct sock *sk)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	struct sk_buff *skb;

	while ((skb = skb_peek(&sk->sk_write_queue)) != NULL) {
		__skb_unlink(skb, &sk->sk_write_queue);
		matt_dump_skb("matt_write_queue_purge", skb);
		skb->_skb_refdst = 0;
		skb->destructor = NULL;
		matt_wmem_free_skb(sk, skb);
	}
	matt_rtx_queue_purge(sk);
	INIT_LIST_HEAD(&out->tsorted_tx_queue);
	out->packets_out = 0;
}

extern bool (*xm_sk_page_frag_refill)(struct sock *sk, struct page_frag *pfrag);

int matt_sendmsg_locked(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct matt_output_ctx *out = &MATT_SK(sk)->output_ctx;
	int flags = msg->msg_flags;
	int copied = 0, err = 0, mss_now, size_goal, process_backlog = 0;
	long timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	struct sk_buff *skb;

	mss_now = matt_send_mss(sk, &size_goal, flags);
	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);
	while (msg_data_left(msg)) {
		struct page_frag *pfrag;
		bool can_coalesce;
		size_t copy = 0;
		int i, copy_i;

		skb = skb_peek_tail(&sk->sk_write_queue);
		if (skb)
			copy = size_goal - skb->len;
		if (!copy) {
new_segment:
			if (!sk_stream_memory_free(sk))
				goto wait_for_space;
			if (unlikely(process_backlog >= 16)) {
				process_backlog = 0;
				if (sk_flush_backlog(sk))
					continue;
			}
			(void)READ_ONCE(out->rb_rtx_queue.rb_node);
			skb = matt_stream_alloc_skb(sk, GFP_ATOMIC, false);
			if (!skb)
				goto wait_for_space;
			process_backlog++;
			matt_skb_entail(sk, skb);
			copy = size_goal;
		}
		if (copy > msg_data_left(msg))
			copy = msg_data_left(msg);
		i = skb_shinfo(skb)->nr_frags;
		pfrag = sk_page_frag(sk);
		if (!xm_sk_page_frag_refill(sk, pfrag))
			goto wait_for_space;
		can_coalesce = skb_can_coalesce(skb, i, pfrag->page, pfrag->offset);
		if (!can_coalesce) {
			if (i >= READ_ONCE(sysctl_max_skb_frags)) {
				out->pushed_seq = out->write_seq;
				goto new_segment;
			}
		}
		copy_i = min_t(int, copy, pfrag->size - pfrag->offset);
		copy_i = matt_wmem_schedule(sk, copy_i);
		if (!copy_i)
			goto wait_for_space;
		err = matt_skb_copy_to_page_nocache(sk, &msg->msg_iter, skb, pfrag->page, pfrag->offset, copy_i);
		if (err)
			goto do_error;
		if (can_coalesce) {
			skb_frag_size_add(&skb_shinfo(skb)->frags[i - 1], copy_i);
		} else {
			skb_fill_page_desc(skb, i, pfrag->page, pfrag->offset, copy_i);
			page_ref_inc(pfrag->page);	/* g61a: as the stock (tcp_sendmsg_locked): no compound_head */
		}
		pfrag->offset += copy_i;
		out->write_seq += copy_i;
		MATT_SKB_CB(skb)->offset_end += copy_i;
		MATT_SKB_CB(skb)->types = 0;
		MATT_SKB_CB(skb)->flags &= 0x7f;
		if (!copied)
			MATT_SKB_CB(skb)->flags &= ~0x20;
		copied += copy_i;
		if (!msg_data_left(msg)) {
			MATT_SKB_CB(skb)->flags |= 0x20;
			goto out;
		}
		if ((s32)(out->pushed_seq - out->write_seq + 0x20000U) < 0) {
			out->pushed_seq = out->write_seq;
			MATT_SKB_CB(skb)->flags |= 0x20;
			[[clang::__noinline__]] __matt_push(sk);
		}
		continue;

wait_for_space:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		matt_remove_empty_skb(sk);
		if (copied) {
#line 2068
			MATT_TRACE(0, 9, "wait_for_space, do copied:%d", copied);
			[[clang::__noinline__]] matt_push(sk, size_goal);
		}
		err = sk_stream_wait_memory(sk, &timeo);
		if (err)
			goto do_error;
		mss_now = matt_send_mss(sk, &size_goal, flags);
	}
out:
	if (copied) {
#line 2092
		MATT_TRACE(0, 9, "do out, total copied:%d write_seq=%u nxt=%u una=%u wnd=%u", copied, READ_ONCE(out->write_seq),
			   out->snd_nxt, out->snd_una, out->snd_wnd);
		[[clang::__noinline__]] matt_push(sk, size_goal);
	}
	return copied;

do_error:
	matt_remove_empty_skb(sk);
	if (copied)
		goto out;
	err = sk_stream_error(sk, flags, err);
	if (unlikely(!READ_ONCE(out->rb_rtx_queue.rb_node) && out->write_seq == out->snd_nxt && err == -EAGAIN))
		sk->sk_write_space(sk);
#line 2110
	MATT_TRACE(0, 2, "out, total copied:%d,write_seq=%u,nxt=%u,una=%u,wnd=%u,pbacklog=%d,bl_len=%u", copied, READ_ONCE(out->write_seq),
		   out->snd_nxt, out->snd_una, out->snd_wnd, process_backlog, sk->sk_backlog.len);
	return copied;
}

extern int sysctl_defer_max;
extern int (*xm_smp_call_function_single_async)(int cpu, call_single_data_t *csd);
extern int (*xm_sk_wait_data)(struct sock *sk, long *timeo, const struct sk_buff *skb);

/* copy of skb_attempt_defer_free() of the 6.6.57 kernel (inlined in the stock) */
static inline void matt_skb_attempt_defer_free(struct sk_buff *skb)
{
	int cpu = skb->alloc_cpu;
	struct softnet_data *sd;
	unsigned int defer_max;
	bool kick;

	if (WARN_ON_ONCE(cpu >= nr_cpu_ids) || !cpu_online(cpu) || cpu == raw_smp_processor_id()) {
nodefer:
		kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
		return;
	}
	sd = &per_cpu(softnet_data, cpu);
	defer_max = sysctl_defer_max;
	if (READ_ONCE(sd->defer_count) >= defer_max)
		goto nodefer;
	spin_lock_bh(&sd->defer_lock);
	kick = sd->defer_count == (defer_max >> 1);
	WRITE_ONCE(sd->defer_count, sd->defer_count + 1);
	skb->next = sd->defer_list;
	WRITE_ONCE(sd->defer_list, skb);
	spin_unlock_bh(&sd->defer_lock);
	if (unlikely(kick) && !cmpxchg(&sd->defer_ipi_scheduled, 0, 1))
		xm_smp_call_function_single_async(cpu, &sd->defer_csd);
}

int matt_recvmsg_locked(struct sock *sk, struct msghdr *msg, size_t len, int flags)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	int copied = 0, target, orig_flags = flags;
	struct sk_buff *skb, *last;
	long timeo;

#line 2135
	MATT_TRACE(0, 9, "enter sk%p rcv_nxt=%u backlog=%u r_alloc=%u", sk, in->rcv_nxt, sk->sk_backlog.len,
		   atomic_read(&sk->sk_rmem_alloc));
	if (sk->sk_state == TCP_LISTEN)
		return -ENOTCONN;
	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);
	do {
		u32 offset;

		last = skb_peek_tail(&sk->sk_receive_queue);
		skb_queue_walk(&sk->sk_receive_queue, skb) {
			last = skb;
			offset = in->copied - MATT_SKB_CB(skb)->offset_start;
			if (unlikely((s32)offset < 0)) {
#line 2165
				MATT_TRACE(0, 3, "recvmsg seq # bug: copied %u, seq %u, rcvnxt %u, fl %u", in->copied,
					   MATT_SKB_CB(skb)->offset_start, in->rcv_nxt, orig_flags);
				break;
			}
#line 2171
			MATT_TRACE(0, 9, "recv1 copied:%u nxt=%u skb->seq:%u offset:%u skb->len:%u len:%u copied:%u", in->copied,
				   in->rcv_nxt, MATT_SKB_CB(skb)->offset_start, offset, skb->len, len, copied);
			if (offset < skb->len)
				goto found_ok_skb;
		}
		if (copied >= target && !READ_ONCE(sk->sk_backlog.tail))
			break;
		if (copied) {
			if (!timeo || sk->sk_err || sk->sk_state == TCP_CLOSE || signal_pending(current))
				break;
		} else {
			if (sock_flag(sk, SOCK_DONE))
				break;
			if (sk->sk_err) {
				copied = sock_error(sk);
				break;
			}
			if (sk->sk_state == TCP_CLOSE) {
				copied = -ENOTCONN;
				break;
			}
			if (!timeo) {
				copied = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				copied = sock_intr_errno(timeo);
				break;
			}
		}
		if (copied >= target) {
#line 2221
			MATT_TRACE(0, 9, "before:sk=%p copied=%u target=%u rcv_nxt=%u backlog=%u r_alloc=%u", sk, copied, target,
				   in->rcv_nxt, sk->sk_backlog.len, atomic_read(&sk->sk_rmem_alloc));
			__sk_flush_backlog(sk);
#line 2225
			MATT_TRACE(0, 9, "after:sk=%p rcv_nxt=%u backlog=%u r_alloc=%u", sk, copied, target, in->rcv_nxt,
				   sk->sk_backlog.len, atomic_read(&sk->sk_rmem_alloc));
		} else {
			int err = xm_sk_wait_data(sk, &timeo, last);

			if (err < 0)
				return copied ? copied : err;
		}
		continue;
found_ok_skb:
		{
			size_t used = skb->len - offset;
			int err;

			if (len < used)
				used = len;
			err = skb_copy_datagram_iter(skb, offset, &msg->msg_iter, used);
			if (err) {
				if (!copied)
					copied = -EFAULT;
				break;
			}
			copied += used;
			len -= used;
			in->copied += used;
			matt_mstamp_refresh(sk);
#line 2262
			MATT_TRACE(0, 9, "recv2 copied:%u skb->seq:%u nxt=%u offset:%u skb->len:%u len:%u copied:%u", in->copied,
				   MATT_SKB_CB(skb)->offset_start, in->rcv_nxt, offset, skb->len, len, copied);
			if (used + offset < skb->len)
				continue;
			__skb_unlink(skb, &sk->sk_receive_queue);
			if (likely(skb->destructor == sock_rfree)) {
				sock_rfree(skb);
				skb->destructor = NULL;
				skb->sk = NULL;
				matt_skb_attempt_defer_free(skb);
			} else {
				kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
			}
		}
	} while (len > 0);
	[[clang::__noinline__]] matt_cleanup_rbuf(sk, copied);
#line 2279
	MATT_TRACE(0, 9, "out sk%p copied=%d rcv_nxt=%u backlog=%u r_alloc=%u", sk, copied, in->rcv_nxt, sk->sk_backlog.len,
		   atomic_read(&sk->sk_rmem_alloc));
	return copied;
}
