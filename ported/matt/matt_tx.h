// SPDX-License-Identifier: GPL-2.0
/* included from matt_main.c (shares the per-cpu objects) */
/* matt data transmit path (stock: mp_tx.c): transmit_skb, the sock-write destructor and the MSQ tasklet (tcp_wfree / tsq tasklet clones). DO NOT LOAD before review. */

static DEFINE_PER_CPU(int, matt_memory_per_cpu_fw_alloc) __used;
static DEFINE_PER_CPU(unsigned int, matt_orphan_count) __used;
static DEFINE_PER_CPU(struct msq_tasklet, msq_tasklet);

/* statistics of one transmitted skb (the same switch as in matt_transmit_mgmt_skb) */
static inline void matt_tx_stats(struct matt_output_ctx *oq, struct sk_buff *skb)
{
	u32 segs = max_t(u16, MATT_SKB_CB(skb)->gso_segs, 1);

	oq->tsend.total_num += segs;
	oq->tsend.total_bytes += skb->len;
	if (MATT_SKB_CB(skb)->flags & 0x80) {
		switch (MATT_SKB_CB(skb)->types) {
		case MATT_MGMT_TYPE_CONN_REQ:
			oq->tsend.req_num++;
			break;
		case MATT_MGMT_TYPE_CONN_RSP:
			oq->tsend.rsp_num++;
			break;
		case MATT_MGMT_TYPE_RESET:
			oq->tsend.rst_num++;
			break;
		case MATT_MGMT_TYPE_PACK:
			oq->tsend.pack_num++;
			break;
		case MATT_MGMT_TYPE_NACK:
			oq->tsend.nack_num++;
			break;
		case MATT_MGMT_TYPE_PING:
			oq->tsend.ping_num++;
			break;
		case MATT_MGMT_TYPE_PONG:
			oq->tsend.pong_num++;
			break;
		default:
			break;
		}
	} else {
		oq->tsend.data_num += segs;
		oq->tsend.data_bytes += skb->len - 16u;
	}
}

void matt_data_wfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct matt_sock *ms = MATT_SK(sk);
	unsigned long flags, nval, oval;
	struct msq_tasklet *tsq;
	bool empty;

	WARN_ON(refcount_sub_and_test(skb->truesize - 1, &sk->sk_wmem_alloc));
	if (refcount_read(&sk->sk_wmem_alloc) >= SKB_TRUESIZE(1) && this_cpu_ksoftirqd() == current)
		goto out;
	oval = smp_load_acquire(&sk->sk_tsq_flags);
	do {
		if (!(oval & MATT_MSQF_THROTTLED) || (oval & MATT_MSQF_QUEUED))
			goto out;
		nval = (oval & ~MATT_MSQF_THROTTLED) | MATT_MSQF_QUEUED;
		nval = cmpxchg(&sk->sk_tsq_flags, oval, nval);
	} while (nval != oval);

	local_irq_save(flags);
	tsq = this_cpu_ptr(&msq_tasklet);
	empty = list_empty(&tsq->head);
	list_add(&ms->msq_node, &tsq->head);
	if (empty)
		tasklet_schedule(&tsq->tasklet);
	local_irq_restore(flags);
	return;
out:
	sk_free(sk);
}

/* global (stock keeps an out-of-line copy: matt_release_cb calls it) and inlined into matt_tasklet_func */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
void matt_msq_write(struct sock *sk)
{
	struct matt_output_ctx *outq = &MATT_SK(sk)->output_ctx;
	long unset __attribute__((uninitialized));	/* the stock calls matt_write_xmit(sk) with ONE argument: x1 is whatever the caller left */

	if (sk->sk_state != TCP_ESTABLISHED)
		return;
	if (outq->lost_out > outq->retrans_out) {
		matt_mstamp_refresh(sk);
		matt_xmit_retransmit_queue(sk);
	}
	matt_write_xmit(sk, unset);
}
#pragma clang diagnostic pop

void matt_tasklet_func(struct tasklet_struct *t)
{
	struct msq_tasklet *tsq = from_tasklet(tsq, t, tasklet);
	LIST_HEAD(list);
	unsigned long flags;
	struct list_head *q, *n;
	struct matt_sock *ms;
	struct sock *sk;

	local_irq_save(flags);
	list_splice_init(&tsq->head, &list);
	local_irq_restore(flags);

	list_for_each_safe(q, n, &list) {
		ms = list_entry(q, struct matt_sock, msq_node);
		sk = (struct sock *)ms;
		list_del(&ms->msq_node);
		smp_mb__before_atomic();
		clear_bit(MSQ_QUEUED, &sk->sk_tsq_flags);
		bh_lock_sock(sk);
		if (!sock_owned_by_user(sk)) {
			[[clang::always_inline]] matt_msq_write(sk);
		} else if (!test_and_set_bit(MATT_MSQ_DEFERRED, &sk->sk_tsq_flags)) {
			sock_hold(sk);
		}
		bh_unlock_sock(sk);
		sk_free(sk);
	}
}

void matt_tasklet_init(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct msq_tasklet *t = &per_cpu(msq_tasklet, i);

		INIT_LIST_HEAD(&t->head);
		tasklet_setup(&t->tasklet, matt_tasklet_func);
	}
}

int matt_transmit_skb(struct sock *sk, struct sk_buff *skb, int clone_it)
{
	struct matt_sock *ms = MATT_SK(sk);
	struct matt_output_ctx *oq = &ms->output_ctx;
	struct matt_mgmt *mgmt = &ms->pkt_mgmt_ctx;
	struct sk_buff *oskb = NULL;
	struct matt_skb_cb *cb;
	struct matthdr *hdr;
	int err;

	BUG_ON(!skb);
	mgmt->matt_wstamp_ns = max(mgmt->matt_wstamp_ns, mgmt->matt_clock_cache);
	skb_set_delivery_time(skb, mgmt->matt_wstamp_ns, true);
	if (clone_it) {
		oskb = skb;
		tcp_skb_tsorted_save(oskb) {
			if (unlikely(skb_cloned(oskb)))
				skb = pskb_copy(oskb, GFP_ATOMIC);
			else
				skb = skb_clone(oskb, GFP_ATOMIC);
		} tcp_skb_tsorted_restore(oskb);
		if (unlikely(!skb))
			return -ENOBUFS;
		skb->dev = NULL;
	}
	skb_push(skb, sizeof(*hdr));
	skb_reset_transport_header(skb);
	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = matt_data_wfree;
	refcount_add(skb->truesize, &sk->sk_wmem_alloc);
	cb = MATT_SKB_CB(skb);
	skb->pfmemalloc = 0;
	hdr = (struct matthdr *)skb->data;
	hdr->offset = htonl(cb->offset_start);
	hdr->reserved = htonl(mgmt->matt_mstamp);
	hdr->flags = cb->flags;
	hdr->type = cb->types;
	hdr->len = htons(skb->len - sizeof(*hdr));
	hdr->num = htonl(mgmt->next_pn);
	mgmt->next_pn += cb->gso_segs;
	if (skb->len != sizeof(*hdr)) {
		oq->data_segs_out += cb->gso_segs;
		oq->lsndtime = jiffies;
		oq->bytes_sent += skb->len - 16u;
	}
	skb_shinfo(skb)->gso_type = sk->sk_gso_type;
	skb_shinfo(skb)->gso_segs = cb->gso_segs;
	skb_shinfo(skb)->gso_size = cb->gso_size;
	matt_dump_skb(__func__, skb);
	MATT_SKB_CB(oskb)->tx.last_tx_mstamp = oq->lsndtime;
	err = matt_lower_xmit2(sk, skb, &ms->dst.addr.addr, &ms->fl, false);
	if (!oskb || err)
		return err;
	list_move_tail(&oskb->tcp_tsorted_anchor, &oq->tsorted_tx_queue);
	matt_rate_skb_sent(sk, oskb);
	matt_tx_stats(oq, oskb);
	return 0;
}
