// SPDX-License-Identifier: GPL-2.0
/* matt timers (stock: mp_timer.c; derived from net/ipv4/tcp_timer.c). Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

/* timer type -> timer of struct matt_timer_ctx (same order as the struct) */
static inline struct timer_list *matt_timer_of(struct sock *sk, u8 type)
{
	struct matt_timer_ctx *tc = &MATT_SK(sk)->timer_ctx;

	switch (type) {
	case 0:
		return &tc->rtimer.timer;
	case 1:
		return &tc->ptimer.timer;
	case 2:
		return &tc->ntimer.timer;
	case 3:
		return &tc->pintimer.timer;
	case 4:
		return &tc->reotimer.timer;
	case 5:
		return &tc->rtotimer.timer;
	case 6:
		return &tc->stimer.timer;
	default:
		return NULL;
	}
}

int matt_timer_pending(struct sock *sk, u8 type)
{
	return timer_pending(matt_timer_of(sk, type));
}

void matt_timer_stop(struct sock *sk, u8 type)
{
	if (del_timer(matt_timer_of(sk, type)))
		sock_put(sk);
}

void matt_timer_start(struct sock *sk, u8 type, u64 timeout)
{
	struct timer_list *t = matt_timer_of(sk, type);

	if (timeout && !timer_pending(t)) {
		if (!mod_timer(t, jiffies + usecs_to_jiffies(timeout)))
			sock_hold(sk);
	}
}

void matt_timer_reset(struct sock *sk, u8 type, u64 timeout)
{
	struct timer_list *t = matt_timer_of(sk, type);

	if (timeout && !mod_timer(t, jiffies + usecs_to_jiffies(timeout)))
		sock_hold(sk);
}

void matt_timer_reduce(struct sock *sk, u8 type, u64 timeout)
{
	struct timer_list *t = matt_timer_of(sk, type);

	if (timeout && !timer_reduce(t, jiffies + usecs_to_jiffies(timeout)))
		sock_hold(sk);
}

void matt_timer_free(struct sock *sk)
{
	struct matt_timer_ctx *tc = &MATT_SK(sk)->timer_ctx;

	sk_stop_timer(sk, &tc->rtimer.timer);
	sk_stop_timer(sk, &tc->reotimer.timer);
	sk_stop_timer(sk, &tc->ptimer.timer);
	sk_stop_timer(sk, &tc->ntimer.timer);
	sk_stop_timer(sk, &tc->pintimer.timer);
	sk_stop_timer(sk, &tc->rtotimer.timer);
	sk_stop_timer(sk, &tc->stimer.timer);
}

void matt_timer_init(struct sock *sk)
{
	struct matt_timer_ctx *tc = &MATT_SK(sk)->timer_ctx;

	timer_setup(&tc->rtimer.timer, matt_timer_write_timeout, 0);
	timer_setup(&tc->ptimer.timer, matt_timer_pack_timeout, 0);
	timer_setup(&tc->ntimer.timer, matt_timer_nack_timeout, 0);
	timer_setup(&tc->pintimer.timer, matt_timer_ping_timeout, 0);
	timer_setup(&tc->reotimer.timer, matt_timer_reo_timeout, 0);
	timer_setup(&tc->rtotimer.timer, matt_timer_rto_timeout, 0);
	timer_setup(&tc->stimer.timer, matt_timer_stats_timeout, 0);
}

#define MATT_TIMER_FN(fn, field, handler, flag)							\
void fn(struct timer_list *t)									\
{												\
	struct matt_sock *ms = from_timer(ms, t, timer_ctx.field.timer);			\
	struct sock *sk = &ms->inet.sk;								\
												\
	bh_lock_sock(sk);									\
	if (!sock_owned_by_user(sk)) {								\
		handler(sk);									\
	} else {										\
		if (!test_and_set_bit(flag, &sk->sk_tsq_flags))					\
			sock_hold(sk);								\
	}											\
	bh_unlock_sock(sk);									\
	sock_put(sk);										\
}

MATT_TIMER_FN(matt_timer_write_timeout, rtimer, matt_write_timer_handler, MATT_WRITE_TIMER_DEFERRED)
MATT_TIMER_FN(matt_timer_pack_timeout, ptimer, matt_pack_timer_handler, MATT_PACK_TIMER_DEFERRED)
MATT_TIMER_FN(matt_timer_nack_timeout, ntimer, matt_nack_timer_handler, MATT_NACK_TIMER_DEFERRED)
MATT_TIMER_FN(matt_timer_ping_timeout, pintimer, matt_ping_timer_handler, MATT_PING_TIMER_DEFERRED)
MATT_TIMER_FN(matt_timer_reo_timeout, reotimer, matt_reo_timer_handler, MATT_REO_TIMER_DEFERRED)
MATT_TIMER_FN(matt_timer_rto_timeout, rtotimer, matt_rto_timer_handler, MATT_RTO_TIMER_DEFERRED)
MATT_TIMER_FN(matt_timer_stats_timeout, stimer, matt_stats_timer_handler, MATT_STATS_TIMER_DEFERRED)
