// SPDX-License-Identifier: GPL-2.0
/* matt queue init/purge, mgmt send wrappers, small socket helpers (stock: mp_rx.c, mp_tx.c, mp_socket.c). Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

extern int sysctl_limit_output;
bool (*xm__lock_sock_fast)(struct sock *sk);
int (*xm_sk_mem_schedule)(struct sock *sk, int size, int kind);

struct sk_buff *matt_make_ping(struct sock *sk);
struct sk_buff *matt_make_pong(struct sock *sk, u32 timestamp);
struct sk_buff *matt_make_rst(struct sock *sk, u32 reasoncode);
struct sk_buff *matt_make_report(struct sock *sk);
int matt_transmit_mgmt_skb(struct sock *sk, struct sk_buff *skb, int clone_it, struct flowi *fl);
int __matt_retransmit_skb(struct sock *sk, struct sk_buff *skb, int segs);
void reset_input_params(struct matt_input_ctx *input_ctx);
void reset_output_params(struct matt_output_ctx *outq);

void matt_inputq_init(struct sock *sk)
{
	struct matt_input_ctx *ic = &MATT_SK(sk)->input_ctx;

	ic->rcv_srtt = 0;
	ic->ack_bdp_th = 0;
	ic->out_of_order_queue = RB_ROOT;
	ic->ooo_last_skb = NULL;
	ic->queue_bytes_trigger = sysctl_limit_output >> 2;
	reset_input_params(ic);
	{
		struct minmax_sample val = { .t = jiffies, .v = ~0U };

		ic->min_srtt.s[2] = ic->min_srtt.s[1] = ic->min_srtt.s[0] = val;
	}
}

void matt_outq_init(struct sock *sk)
{
	struct matt_output_ctx *oq = &MATT_SK(sk)->output_ctx;

	INIT_LIST_HEAD(&oq->tsorted_tx_queue);
	oq->rb_rtx_queue = RB_ROOT;
	skb_queue_head_init(&sk->sk_write_queue);	/* G56a: the stock also initialises the queue lock (qlen and lock zeroed by one 8-byte store at +0x178) */
	oq->rto = 50000;
	oq->rack.reo_wnd_steps = 1;
	oq->announce_srtt = 0;
	oq->snd_cwnd_clamp = 22369;
	reset_output_params(oq);
}

unsigned int matt_skb_rbtree_purge(struct rb_root *root)
{
	struct rb_node *p = rb_first(root);
	unsigned int sum = 0;

	while (p) {
		struct sk_buff *skb = rb_entry(p, struct sk_buff, rbnode);

		p = rb_next(p);
		rb_erase(&skb->rbnode, root);
		sum += skb->truesize;
		kfree_skb_reason(skb, (enum skb_drop_reason)2);
	}
	return sum;
}

void matt_rbtree_insert(struct rb_root *root, struct sk_buff *skb)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct sk_buff *skb1;

	while (*p) {
		parent = *p;
		skb1 = rb_entry(parent, struct sk_buff, rbnode);
		if (before(MATT_SKB_CB(skb)->offset_start, MATT_SKB_CB(skb1)->offset_start))
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}
	rb_link_node(&skb->rbnode, parent, p);
	rb_insert_color(&skb->rbnode, root);
}

int matt_send_ping(struct sock *sk)
{
	struct sk_buff *skb = matt_make_ping(sk);

	if (!skb) {
#line 726
		MATT_TRACE(0, 2, "no mem");
		return -ENOMEM;
	}
	return matt_transmit_mgmt_skb(sk, skb, 0, &MATT_SK(sk)->fl);
}

int matt_send_pong(struct sock *sk, u32 timesatmp)
{
	struct sk_buff *skb = matt_make_pong(sk, timesatmp);

	if (!skb) {
#line 740
		MATT_TRACE(0, 2, "no mem");
		return -ENOMEM;
	}
	return matt_transmit_mgmt_skb(sk, skb, 0, &MATT_SK(sk)->fl);
}

int matt_send_rst(struct sock *sk, u32 reasoncode)
{
	struct sk_buff *skb = matt_make_rst(sk, reasoncode);

	if (!skb) {
#line 754
		MATT_TRACE(0, 2, "no mem");
		return -ENOMEM;
	}
	return matt_transmit_mgmt_skb(sk, skb, 0, &MATT_SK(sk)->fl);
}

int matt_send_report(struct sock *sk)
{
	struct sk_buff *skb = matt_make_report(sk);

	if (!skb) {
#line 768
		MATT_TRACE(0, 2, "no mem");
		return -ENOMEM;
	}
	return matt_transmit_mgmt_skb(sk, skb, 0, &MATT_SK(sk)->fl);
}

int matt_retransmit_skb(struct sock *sk, struct sk_buff *skb, int segs)
{
	int err = __matt_retransmit_skb(sk, skb, segs);

	if (!err) {
		MATT_SKB_CB(skb)->sacked |= 0x02 | 0x80;
		MATT_SK(sk)->output_ctx.retrans_out += MATT_SKB_CB(skb)->gso_segs;
	}
	return err;
}
