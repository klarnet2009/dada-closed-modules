/* SPDX-License-Identifier: GPL-2.0 */
/* matt socket layer (stock: mp_sock.c / mp_hash.c), included from matt_main.c (needs its static hash objects) */
extern int (*xm_reuseport_alloc)(struct sock *sk, bool bind_inany);
extern int (*xm_reuseport_add_sock)(struct sock *sk, struct sock *sk2, bool bind_inany);
extern void (*xm_reuseport_detach_sock)(struct sock *sk);
extern struct sock *(*xm_reuseport_select_sock)(struct sock *sk, u32 hash, struct sk_buff *skb, int hdr_len);
extern void (*xm_sk_stream_kill_queues)(struct sock *sk);
extern int (*xm_inet_send_prepare)(struct sock *sk);
extern void (*xm_sockopt_lock_sock)(struct sock *sk);
extern void (*xm_sockopt_release_sock)(struct sock *sk);

static __always_inline u32 matt_addr_h(union matt_sockaddr *a)
{
	if (a->sa.sa_family == AF_INET6)
		return jhash2((const u32 *)&a->v6.sin6_addr, 4, 0);
	return (__force u32)a->v4.sin_addr.s_addr;
}

noinline u32 matt_listen_sock_hash(struct net *net, u16 port)
{
	return jhash_2words(port, net_hash_mix(net), 0) & (matt_listen_hash_size - 1);
}

noinline u32 matt_sock_hash(struct net *net, union matt_sockaddr *s, union matt_sockaddr *d)
{
	u32 h1 = matt_addr_h(s);
	u32 h2 = matt_addr_h(d);

	return jhash_3words(h1, ((u32)s->v4.sin_port << 16) | d->v4.sin_port, net_hash_mix(net), h2) &
	       (matt_sock_hash_sz - 1);
}

u32 matt_addr_hash(struct net *net, union matt_sockaddr *a)
{
	u32 h = matt_addr_h(a);

	return jhash_3words(h, a->v4.sin_port, net_hash_mix(net), 0);
}

void matt_set_state(struct sock *sk, int state)
{
	if (state == MATT_SS_CLOSED)
		sk->sk_prot->unhash(sk);
	sk->sk_state = state;
	sk->sk_state_change(sk);
#line 1516
	MATT_TRACE(0, 4, "sk=%p state=%u", sk, sk->sk_state);
}

void matt_unhash(struct sock *sk)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct net *net;
	struct matt_shash_head *head;

	if (sk_unhashed(sk))
		return;
	net = sock_net(sk);
#line 2734
	MATT_TRACE(0, 9, "normal unhash sk:%p", sk);
	if (sk->sk_max_ack_backlog) {
		struct list_head *p, *n;

		for (p = msk->reqs.next; p != &msk->reqs; p = n) {
			n = p->next;
			list_del_init(p);
			sk->sk_ack_backlog--;
			kfree(p);
		}
		head = matt_listen_sock_head(matt_listen_sock_hash(net, ntohs(msk->src.addr.addr.v4.sin_port)));
	} else {
		head = matt_sock_head(matt_sock_hash(net, &msk->src.addr.addr, &msk->dst.addr.addr));
	}
	spin_lock_bh(&head->lock);
	if (rcu_access_pointer(sk->sk_reuseport_cb))
		xm_reuseport_detach_sock(sk);
	__sk_nulls_del_node_init_rcu(sk);
	spin_unlock_bh(&head->lock);
}

int matt_hash(struct sock *sk)
{
	struct net *net = sock_net(sk);
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_shash_head *head;
	spinlock_t *lock;
	struct sock *sk2;
	struct hlist_nulls_node *node;
	int err = 0;
	int any;

#line 2670
	MATT_TRACE(0, 9, "hash sk:%p", sk);
	union matt_sockaddr *src = &msk->src.addr.addr;
	if (sk->sk_max_ack_backlog) {
		head = matt_listen_sock_head(matt_listen_sock_hash(net, ntohs(src->v4.sin_port)));
		lock = &head->lock;
		spin_lock_bh(lock);
		any = matt_is_any_addr2(src);
		sk_nulls_for_each(sk2, node, &head->head) {
			if (net != sock_net(sk2) || sk2->sk_state != MATT_SS_LISTENING)
				continue;
			if (!matt_cmp_sk_addr2(sk2, &MATT_SK(sk2)->src.addr.addr, src))
				continue;
			if (sk->sk_reuseport && sk2->sk_reuseport) {
				err = xm_reuseport_add_sock(sk, sk2, any != 0);
				if (err)
					goto out;
				sock_set_flag(sk, SOCK_RCU_FREE);
				__sk_nulls_add_node_rcu(sk, &head->head);
#line 2700
				MATT_TRACE(0, 9, "sk: %p add reuse group, old-sk: %p", sk, sk2);
			}
			goto out;
		}
		if (sk->sk_reuseport) {
#line 2708
			MATT_TRACE(0, 9, "sk: %p reuseport", sk);
			err = xm_reuseport_alloc(sk, any != 0);
			if (err)
				goto out;
		}
		sock_set_flag(sk, SOCK_RCU_FREE);
		__sk_nulls_add_node_rcu(sk, &head->head);
	} else {
		union matt_sockaddr *dst = &msk->dst.addr.addr;

		head = matt_sock_head(matt_sock_hash(net, src, dst));
		lock = &head->lock;
		spin_lock_bh(lock);
		sock_set_flag(sk, SOCK_RCU_FREE);
#line 2680
		MATT_TRACE(0, 4, "sk:%p %pI4:%d -> %pI4:%d", sk, &src->v4.sin_addr,
			   ntohs(src->v4.sin_port), &dst->v4.sin_addr, ntohs(dst->v4.sin_port));
		__sk_nulls_add_node_rcu(sk, &head->head);
	}
out:
	spin_unlock_bh(lock);
	return err;
}

/* ---- UDP tunnel sockets ---- */
static void matt_udp_sock_put(struct matt_udp_sock *us)
{
	if (refcount_dec_and_test(&us->refcnt))
		queue_work(matt_wq, &us->work);
}

static noinline void matt_set_udp_sock(struct matt_src *addr, struct matt_udp_sock *us)
{
	if (addr->udp_sk)
		matt_udp_sock_put(addr->udp_sk);
	addr->udp_sk = us;
	/* stock copies a whole struct matt_addr (32 bytes) out of the udp sock (picks up refcnt as udp_bind) */
	addr->addr = *(struct matt_addr *)&us->addr;
}

static void matt_addr_free(struct sock *sk, struct matt_addr *a)
{
	struct matt_src *addr = (struct matt_src *)a;

#line 1232
	MATT_TRACE(0, 9, "sk: %p", sk);
	if (addr->udp_sk)
		matt_udp_sock_put(addr->udp_sk);
	addr->udp_sk = NULL;
	memset(&addr->addr, 0, sizeof(addr->addr));
}

static int matt_udp_err(struct sock *sk, struct sk_buff *skb)
{
#line 591
	MATT_TRACE(0, 2, "sth wrong");
	return 0;
}

static int matt_udp_rcv(struct sock *sk, struct sk_buff *skb)
{
	if (skb->data_len && !__pskb_pull_tail(skb, skb->data_len))
		return 0;
	{
		u16 off = skb->transport_header;

		memset(skb->cb, 0, sizeof(skb->cb));
		MATT_SKB_CB(skb)->udph_offset = off;
	}
	skb_set_transport_header(skb, 8);
	matt_rcv(skb);
	return 0;
}

static void matt_udp_sock_destroy(struct work_struct *work)
{
	struct matt_udp_sock *us = (struct matt_udp_sock *)work;
	struct sock *sk = us->sk;
	struct matt_uhash_head *h;

#line 1189
	MATT_TRACE(0, 2, "m_udp_sk:%p udp sk:%p", us, sk);
	sock_hold(sk);
	udp_tunnel_sock_release(sk->sk_socket);
	h = matt_udp_sock_head(sock_net(sk), ntohs(us->addr.v4.sin_port));
	mutex_lock(&h->lock);
	__hlist_del(&us->node);
	mutex_unlock(&h->lock);
	sock_put(sk);
	kfree(us);
}

static struct matt_udp_sock *matt_udp_sock_create(struct sock *sk, union matt_sockaddr *a)
{
	struct net *net = sock_net(sk);
	struct udp_tunnel_sock_cfg cfg = {};
	struct udp_port_cfg conf = {};
	struct socket *sock = NULL;
	struct matt_udp_sock *us;
	struct matt_uhash_head *h;
	int err;

	us = kzalloc(sizeof(*us), GFP_ATOMIC);
	if (!us)
		return NULL;
	matt_udp_conf_init2(sk, &conf, a);
	if (conf.family == AF_INET6)
		err = udp_sock_create6(net, &conf, &sock);
	else if (conf.family == AF_INET)
		err = udp_sock_create4(net, &conf, &sock);
	else
		err = -EPFNOSUPPORT;
	if (err) {
#line 1285
		MATT_TRACE(0, 2, "failed to create udp socket: %d", err);
		kfree(us);
		return NULL;
	}
	cfg.encap_type = 1;
	cfg.encap_rcv = matt_udp_rcv;
	cfg.encap_err_lookup = matt_udp_err;
	cfg.gro_receive = matt_gro_receive;
	cfg.gro_complete = matt_gro_complete;
	setup_udp_tunnel_sock(net, sock, &cfg);
	refcount_set(&us->refcnt, 1);
	us->sk = sock->sk;
	us->addr = *a;
	h = matt_udp_sock_head(net, ntohs(a->v4.sin_port));
	hlist_add_head(&us->node, &h->head);
	INIT_WORK(&us->work, matt_udp_sock_destroy);
#line 1306
	MATT_TRACE(0, 4, "sk:%p m_udp_sk:%p udp sk:%p, ref:%d", sk, us, us->sk, refcount_read(&us->refcnt));
	return us;
}

static void matt_inet_get_local_port_range(const struct net *net, int *low, int *high)
{
	unsigned int seq;

	do {
		seq = read_seqbegin(&net->ipv4.ip_local_ports.lock);
		*low = net->ipv4.ip_local_ports.range[0];
		*high = net->ipv4.ip_local_ports.range[1];
	} while (read_seqretry(&net->ipv4.ip_local_ports.lock, seq));
}

/* ---- lookup / backlog / destroy ---- */
struct sock *matt_sock_lookup(struct sk_buff *skb, union matt_sockaddr *sa, union matt_sockaddr *da)
{
	struct net *net = dev_net(skb->dev);
	struct matt_shash_head *head;
	struct hlist_nulls_node *node;
	struct sock *sk;
	u32 hash;

	hash = matt_sock_hash(net, sa, da);
	head = matt_sock_head(hash);
	rcu_read_lock();
begin:
	sk_nulls_for_each_rcu(sk, node, &head->head) {
		if (net != sock_net(sk))
			continue;
		if (!matt_cmp_sk_addr2(sk, &MATT_SK(sk)->src.addr.addr, sa))
			continue;
		if (!matt_cmp_sk_addr2(sk, &MATT_SK(sk)->dst.addr.addr, da))
			continue;
		goto out;
	}
	if (get_nulls_value(node) != hash)
		goto begin;
	rcu_read_unlock();

	hash = matt_listen_sock_hash(net, ntohs(sa->v4.sin_port));
	head = matt_listen_sock_head(hash);
	rcu_read_lock();
begin2:
	sk_nulls_for_each_rcu(sk, node, &head->head) {
		if (net != sock_net(sk) || sk->sk_state != MATT_SS_LISTENING)
			continue;
		if (!matt_cmp_sk_addr2(sk, &MATT_SK(sk)->src.addr.addr, sa))
			continue;
		goto found;
	}
	if (get_nulls_value(node) != hash)
		goto begin2;
	sk = NULL;
	goto out;
found:
	if (sk->sk_reuseport)
		sk = xm_reuseport_select_sock(sk, matt_addr_hash(net, da), skb, 1);
out:
	rcu_read_unlock();
	return sk;
}

void matt_release_sock(struct sock *sk)
{
	struct sk_buff *skb, *next;

	while ((skb = sk->sk_backlog.head) != NULL) {
		sk->sk_backlog.head = sk->sk_backlog.tail = NULL;
		spin_unlock_bh(&sk->sk_lock.slock);
		do {
			next = skb->next;
			prefetch(next);
			skb_mark_not_on_list(skb);
			sk->sk_backlog_rcv(sk, skb);
			skb = next;
		} while (skb);
		spin_lock_bh(&sk->sk_lock.slock);
	}
	sk->sk_backlog.len = 0;
}

void matt_csk_destroy_sock(struct sock *sk)
{
	WARN_ON(sk->sk_state != MATT_SS_CLOSED);
	WARN_ON(!sock_flag(sk, SOCK_DEAD));
	WARN_ON(!sk_unhashed(sk));
	sk->sk_prot->destroy(sk);
	xm_sk_stream_kill_queues(sk);
	this_cpu_dec(*sk->sk_prot->orphan_count);
	sock_put(sk);
}

/* ---- bind ---- */
static struct matt_udp_sock *matt_udp_sk_lookup2(struct sock *sk, union matt_sockaddr *a, u16 port)
{
	struct net *net = sock_net(sk);
	struct matt_uhash_head *h = matt_udp_sock_head(net, port);
	struct matt_udp_sock *us;

	hlist_for_each_entry(us, &h->head, node) {
		if (net != sock_net(us->sk))
			continue;
#line 1253
		MATT_TRACE(0, 2, "udp sk:%p, sk:%p, port:%d", us, sk, port);
		if (a) {
			if (matt_cmp_sk_addr2(sk, &us->addr, a))
				return us;
		} else if (us->addr.v4.sin_port == htons(port)) {
			return us;
		}
	}
	return NULL;
}

int matt_set_bind_port(struct sock *sk, struct matt_addr *addr)
{
	struct matt_src *src = (struct matt_src *)addr;
	struct net *net = sock_net(sk);
	u16 port = src->addr.addr.v4.sin_port;
	struct matt_uhash_head *h;
	struct matt_udp_sock *us;
	int low, high, remaining, snum;

	if (port) {
		h = matt_udp_sock_head(net, ntohs(port));
		mutex_lock(&h->lock);
		us = matt_udp_sk_lookup2(sk, &src->addr.addr, ntohs(port));
		if (!us) {
			us = matt_udp_sock_create(sk, &src->addr.addr);
			if (!us) {
#line 1338
				MATT_TRACE(0, 2, "fail to create udp sock");
				mutex_unlock(&h->lock);
				return -EINVAL;
			}
		} else if (!refcount_inc_not_zero(&us->refcnt)) {
			mutex_unlock(&h->lock);
			return -EAGAIN;
		}
		mutex_unlock(&h->lock);
		matt_set_udp_sock(src, us);
		return 0;
	}

	matt_inet_get_local_port_range(net, &low, &high);
	remaining = high - low + 1;
	snum = low + (u32)(((u64)get_random_u32() * remaining) >> 32);	/* stock: sxtw(remaining) * rand, not reciprocal_scale */
#line 1355
	MATT_TRACE(0, 9, "port:%d range(%d, %d) remaining=%d", snum, low, high, remaining);
	do {
		snum++;
		if ((u32)snum > (u32)high || (u32)snum < (u32)low)	/* stock: unsigned compares */
			snum = low;
		if (inet_is_local_reserved_port(net, (u16)snum))
			continue;
		h = matt_udp_sock_head(net, snum);
#line 1369
		MATT_TRACE(0, 9, "tmp snum=%d remaining=%d", (u16)snum, remaining);
		mutex_lock(&h->lock);
		if (!matt_udp_sk_lookup2(sk, NULL, snum)) {
			src->addr.addr.v4.sin_port = htons(snum);
			us = matt_udp_sock_create(sk, &src->addr.addr);
			if (!us) {
				src->addr.addr.v4.sin_port = 0;
				mutex_unlock(&h->lock);
				return -EINVAL;
			}
			mutex_unlock(&h->lock);
			matt_set_udp_sock(src, us);
			__sk_dst_reset(sk);	/* G1ad: stock = ldr + stlr, not xchg */
			return 0;
		}
		mutex_unlock(&h->lock);
	} while (--remaining > 0);
	return -EADDRINUSE;
}

/* ---- bind / close ---- */
int matt_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct matt_sock *msk = MATT_SK(sk);
	int err;

	lock_sock(sk);
	if (msk->src.addr.addr.v4.sin_port || matt_get_user_addr2(sk, &msk->src.addr.addr, uaddr, addr_len)) {
#line 1754
		MATT_TRACE(0, 2, "bind error, invalid addr");
		err = -EINVAL;
	} else {
		err = matt_set_bind_port(sk, &msk->src.addr);
		if (err) {
			matt_addr_free(sk, &msk->src.addr);
#line 1763
			MATT_TRACE(0, 2, "matt_set_bind_port error");
		} else {
			matt_set_sk_addr2(sk, &msk->src.addr.addr, true);
		}
	}
	release_sock(sk);
	return err;
}

/* ---- close / connect ---- */
/* the stock calls __matt_close with the single argument sk (x1 is left unset) although the BTF prototype has a timeout */
noinline void __matt_close(struct sock *sk, long timeout)
{
	int state;

	sk->sk_shutdown = SHUTDOWN_MASK;
	if (sk->sk_state == MATT_SS_LISTENING) {
		matt_set_state(sk, MATT_SS_CLOSED);
#line 1581
		MATT_TRACE(0, 2, "sk:%p listen socket closing", sk);
	} else {
		matt_send_rst(sk, 1);
	}
	state = sk->sk_state;
	sock_hold(sk);
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_socket = NULL;
	sk->sk_wq = NULL;
	sock_set_flag(sk, SOCK_DEAD);
	write_unlock_bh(&sk->sk_callback_lock);
	local_bh_disable();
	bh_lock_sock(sk);
	matt_release_sock(sk);
	this_cpu_inc(matt_orphan_count);
	if (state == MATT_SS_CLOSED || sk->sk_state != MATT_SS_CLOSED) {
		if (sk->sk_state != MATT_SS_CLOSED) {
			matt_set_state(sk, MATT_SS_CLOSED);
#line 1614
			MATT_TRACE(0, 2, "sk:%p normal socket closing", sk);
		}
		matt_csk_destroy_sock(sk);
	}
	bh_unlock_sock(sk);
	local_bh_enable();
	/* no sock_put here (G1ag, review F1): the sock_hold above is closed by sock_put in matt_close, the creation ref by matt_csk_destroy_sock */
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
void matt_close(struct sock *sk, long timeout)
{
#line 1627
	MATT_TRACE(0, 2, "sk: %p", sk);
	long unset;

	lock_sock(sk);
	__matt_close(sk, unset);
	release_sock(sk);
	sock_put(sk);
}
#pragma clang diagnostic pop

int matt_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct sk_buff *skb;
	int err;

	if (sk->sk_state != MATT_SS_CLOSED ||
	    matt_get_user_addr2(sk, &msk->dst.addr.addr, uaddr, addr_len)) {
#line 1670
		MATT_TRACE(0, 2, "connect error, invalid addr");
		return -EINVAL;
	}
	err = matt_flow_route2(sk, &msk->dst.addr.addr, &msk->src.addr.addr, &msk->fl);
	if (err) {
#line 1680
		MATT_TRACE(0, 2, "invalid params, can not find remote rt");
		return err;
	}
	matt_update_mss(sk, &msk->dst.addr.addr);
	matt_set_sk_addr2(sk, &msk->dst.addr.addr, false);
	if (!msk->src.addr.addr.v4.sin_port) {
		err = matt_set_bind_port(sk, &msk->src.addr);
		if (err) {
			matt_addr_free(sk, &msk->src.addr);
#line 1697
			MATT_TRACE(0, 2, "matt_set_bind_port error");
			return err;
		}
		err = matt_flow_route2(sk, &msk->dst.addr.addr, &msk->src.addr.addr, &msk->fl);
		if (err) {
			matt_addr_free(sk, &msk->src.addr);
#line 1705
			MATT_TRACE(0, 2, "invalid params, can not find remote rt");
			return err;
		}
	}
	err = sk->sk_prot->hash(sk);
	if (err) {
#line 1712
		MATT_TRACE(0, 2, "hash error");
		goto fail;
	}
	matt_mstamp_refresh(sk);
	skb = matt_make_creq(sk);
	if (!skb) {
#line 1719
		MATT_TRACE(0, 2, "no mem");
		goto fail;
	}
#line 1723
	MATT_TRACE(0, 4, "sk:%p send creq", sk);
	err = matt_transmit_mgmt_skb(sk, skb, 0, &msk->fl);
	if (err) {
#line 1728
		MATT_TRACE(0, 2, "fail to tx creq");
	}
	matt_timer_start(sk, 0, 200000);
	matt_set_state(sk, MATT_SS_ESTABLISHING);
	return err;
fail:
	memset(&msk->fl, 0, sizeof(msk->fl));
	sk->sk_prot->unhash(sk);
	return err;
}

/* ---- socket init / destroy / disconnect / shutdown ---- */
extern int sysctl_matt_rmem[3], sysctl_matt_wmem[3], sysctl_matt_enable;
void matt_write_space(struct sock *sk);
void matt_inet_sock_destruct(struct sock *sk);

void matt_outq_init(struct sock *sk);
void matt_inputq_init(struct sock *sk);

int matt_init_sock(struct sock *sk)
{
	struct matt_sock *msk = MATT_SK(sk);

	if (!sysctl_matt_enable) {
#line 963
		MATT_TRACE(0, 2, "under management control");
		return -1;
	}
	sk->sk_destruct = matt_inet_sock_destruct;
	sk->sk_write_space = matt_write_space;
	sock_set_flag(sk, SOCK_USE_WRITE_QUEUE);
	INIT_LIST_HEAD(&msk->reqs);
	matt_outq_init(sk);
	INIT_LIST_HEAD(&msk->msq_node);
	matt_inputq_init(sk);
	sk->sk_sndbuf = READ_ONCE(sysctl_matt_wmem[2]);
	sk->sk_rcvbuf = READ_ONCE(sysctl_matt_rmem[2]);
	matt_mgmt_init(sk);
	matt_timer_init(sk);
	matt_set_cc_config(sk);
	local_bh_disable();
	sk_sockets_allocated_inc(sk);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	local_bh_enable();
	return 0;
}

void matt_destroy_sock(struct sock *sk)
{
	struct matt_sock *msk = MATT_SK(sk);

#line 1492
	MATT_TRACE(0, 2, "sk: %p", sk);
	matt_timer_free(sk);
	[[clang::__noinline__]] matt_inq_free(sk);
	matt_outq_free(sk);
	matt_cleanup_ulp(sk);
	matt_addr_free(sk, &msk->src.addr);
	matt_addr_free(sk, &msk->dst.addr);
	memset(&msk->fl, 0, sizeof(msk->fl));
	sk_sockets_allocated_dec(sk);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
}

int matt_disconnect(struct sock *sk, int flags)
{
	int state = sk->sk_state;

#line 1778
	MATT_TRACE(0, 2, "sk: %p", sk);
	if (state != MATT_SS_CLOSED) {
		matt_set_state(sk, MATT_SS_CLOSED);
		if (state != MATT_SS_LISTENING) {
			matt_send_rst(sk, 1);
			sk->sk_err = ECONNRESET;
		}
	}
	sk->sk_shutdown = 0;
	sock_reset_flag(sk, SOCK_DONE);
	__sk_dst_reset(sk);	/* G1ad: stock = load + store-release, not xchg */
	sk_error_report(sk);
	return 0;
}

void matt_shutdown(struct sock *sk, int how)
{
#line 1007
	MATT_TRACE(0, 4, "sk: %p", sk);
	if (!(how & SEND_SHUTDOWN)) {
#line 1009
		MATT_TRACE(0, 2, "not send shutdown");
	} else if (sk->sk_state == MATT_SS_ESTABLISHED) {
		matt_send_rst(sk, 1);
	}
	if (sk->sk_state != MATT_SS_LISTENING)
		matt_set_state(sk, MATT_SS_CLOSED);
}

struct matt_request_sock *matt_request_sock_dequeue(struct sock *sk)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_request_sock *req = (struct matt_request_sock *)msk->reqs.next;

	list_del_init(&req->list);
	sk->sk_ack_backlog--;
	return req;
}

void matt_inet_sock_destruct(struct sock *sk)
{
#line 934
	MATT_TRACE(0, 9, "sk: %p", sk);
	__skb_queue_purge(&sk->sk_receive_queue);
	__skb_queue_purge(&sk->sk_error_queue);
	sk_mem_reclaim_final(sk);
	if (sk->sk_type == SOCK_STREAM && sk->sk_state != MATT_SS_CLOSED) {
#line 942
		MATT_TRACE(0, 2, "attempt to release TCP socket in state %d %p", sk->sk_state, sk);
		return;
	}
	if (!sock_flag(sk, SOCK_DEAD)) {
#line 946
		MATT_TRACE(0, 2, "attempt to release alive inet socket %p", sk);
		return;
	}
	WARN_ON(atomic_read(&sk->sk_rmem_alloc));
	WARN_ON(refcount_read(&sk->sk_wmem_alloc));
	WARN_ON(sk->sk_wmem_queued);
	WARN_ON(sk_forward_alloc_get(sk));
	kfree(rcu_dereference_protected(inet_sk(sk)->inet_opt, 1));
	dst_release(rcu_dereference_protected(sk->sk_dst_cache, 1));
	dst_release(sk->sk_rx_dst);
}

void matt_write_space(struct sock *sk)
{
	struct socket *sock = sk->sk_socket;
	struct socket_wq *wq;

	if (sk_stream_wspace(sk) >= sk_stream_min_wspace(sk) && __sk_stream_memory_free(sk, 1) && sock) {
		clear_bit(SOCK_NOSPACE, &sock->flags);
#line 907
		MATT_TRACE(0, 9, "SOCK_NOSPACE clear");
		rcu_read_lock();
		wq = rcu_dereference(sk->sk_wq);
		if (skwq_has_sleeper(wq))
			wake_up_interruptible_sync_poll(&wq->wait, EPOLLOUT | EPOLLWRNORM | EPOLLWRBAND);
		if (wq && wq->fasync_list && !(sk->sk_shutdown & SEND_SHUTDOWN))
			sock_wake_async(wq, SOCK_WAKE_SPACE, POLL_OUT);
		rcu_read_unlock();
	} else {
#line 923
		MATT_TRACE(0, 9, "sk:%p sk_wmem_alloc:%d sk_sndbuf:%d sk_wmem_queued:%d min_wspace:%d sk_stream_wspace:%d sk_mem_free:%s", sk,
			   refcount_read(&sk->sk_wmem_alloc), sk->sk_sndbuf, sk->sk_wmem_queued, sk_stream_min_wspace(sk),
			   sk_stream_wspace(sk), __sk_stream_memory_free(sk, 1) ? "true" : "false");
	}
}

int matt_request_sock_enqueue(struct sock *sk, const struct matt_creq *creq)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_request_sock *req;

	if (READ_ONCE(sk->sk_ack_backlog) > READ_ONCE(sk->sk_max_ack_backlog))
		return -ENOMEM;
	req = kzalloc(sizeof(*req), GFP_ATOMIC);
	if (!req)
		return -ENOMEM;
	req->da = msk->pkt_mgmt_ctx.daddr;
	req->sa = msk->pkt_mgmt_ctx.saddr;
	req->version = 1;
	req->rcv_mss = ntohs(creq->mss);
	req->rcv_isn = ntohl(creq->hdr.offset);
	req->rcv_num = ntohl(creq->hdr.num);
	list_add_tail(&req->list, &msk->reqs);
	sk->sk_ack_backlog++;
#line 628
	MATT_TRACE(0, 9, "sk:%p creq enqueue %pI4:%d -> %pI4:%d, isn=%u, rcv_mss:%d", sk, &req->sa.v4.sin_addr, ntohs(req->sa.v4.sin_port), &req->da.v4.sin_addr, ntohs(req->da.v4.sin_port), req->rcv_isn, req->rcv_mss);
	return 0;
}

static noinline int matt_accept_sock_init(struct sock *sk, struct matt_request_sock *req)
{
	struct matt_sock *msk = MATT_SK(sk);
	int err;

	lock_sock(sk);
	msk->dst.addr.addr = req->da;
	msk->src.addr.addr = req->sa;
	err = matt_flow_route2(sk, &msk->dst.addr.addr, &msk->src.addr.addr, &msk->fl);
	if (err) {
#line 2433
		MATT_TRACE(0, 2, "fail, no rt!");
		goto out;
	}
	matt_set_sk_addr2(sk, &req->da, false);
	matt_set_sk_addr2(sk, &req->sa, true);
	err = sk->sk_prot->hash(sk);
	if (err) {
#line 2444
		MATT_TRACE(0, 2, "hash error, invalid params");
		goto out;
	}
	msk->rcv_mss = req->rcv_mss;
	msk->input_ctx.ack_bdp_th = req->rcv_mss << 1;
	matt_mstamp_refresh(sk);
	matt_set_state(sk, MATT_SS_ESTABLISHED);
	matt_timer_start(sk, 6, 500000);
out:
	release_sock(sk);
	return err;
}

/* ---- proto_ops (inet stream style wrappers) ---- */
int matt_inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct matt_sock *msk = MATT_SK(sk);
	int err = 0;

	lock_sock(sk);
	if (backlog) {
		if (!sk_unhashed(sk)) {
			err = 0;
			goto out;
		}
		if (msk->src.addr.addr.v4.sin_port) {
			[[clang::__noinline__]] matt_set_state(sk, MATT_SS_LISTENING);
			sk->sk_max_ack_backlog = backlog;
			if (!sk->sk_prot->hash) {
				err = 0;
				goto out;
			}
			err = sk->sk_prot->hash(sk);
			if (!err)
				goto out;
		} else {
#line 576
			MATT_TRACE(0, 2, "not support auto bind yet!");
			err = 0;
		}
	}
	if (sk->sk_prot->unhash)
		sk->sk_prot->unhash(sk);
	sk->sk_max_ack_backlog = 0;
	[[clang::__noinline__]] matt_set_state(sk, MATT_SS_CLOSED);
out:
	release_sock(sk);
#line 595
	MATT_TRACE(0, 9, "done - max backlog:%d, state:%d", sk->sk_max_ack_backlog, sk->sk_state);
	return err;
}

void matt_inet_accept2(struct socket *sock, struct socket *newsock, struct sock *newsk)
{
	sock_rps_record_flow(newsk);
	if (test_bit(SOCK_SUPPORT_ZC, &sock->flags))
		set_bit(SOCK_SUPPORT_ZC, &newsock->flags);
	sock_graft(newsk, newsock);
	newsock->state = SS_CONNECTED;
}

int matt_inet_accept(struct socket *sock, struct socket *newsock, int flags, bool kern)
{
	struct sock *sk1 = sock->sk;
	int err = -EINVAL;
	struct sock *sk2 = READ_ONCE(sk1->sk_prot)->accept(sk1, flags, &err, kern);

	if (!sk2)
		return err;
	lock_sock(sk2);
	matt_inet_accept2(sock, newsock, sk2);
	release_sock(sk2);
	return 0;
}

int matt_inet_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct sock *sk = sock->sk;

	if (unlikely(xm_inet_send_prepare(sk)))
		return -EAGAIN;
	return sk->sk_prot->sendmsg(sk, msg, size);
}

int matt_inet_recvmsg(struct socket *sock, struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;

	if (likely(!(flags & MSG_ERRQUEUE)))
		sock_rps_record_flow(sk);
	err = sk->sk_prot->recvmsg(sk, msg, size, flags, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}

int matt_inet_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int err = 0;

#line 677
	MATT_TRACE(0, 2, "sk: %p in", sk);
	if (unlikely((unsigned int)how >= 3)) {
#line 686
		MATT_TRACE(0, 2, "without shutdown mask!");
		return -EINVAL;
	}
	how++;
	lock_sock(sk);
	if (sock->state == SS_CONNECTING) {
		if ((1 << sk->sk_state) & 0x8c) {
			sock->state = SS_DISCONNECTING;
#line 695
			MATT_TRACE(0, 2, "SS_DISCONNECTING");
		} else {
			sock->state = SS_CONNECTED;
#line 698
			MATT_TRACE(0, 2, "SS_CONNECTED");
		}
	}
	if (sk->sk_state == MATT_SS_LISTENING) {
#line 722
		MATT_TRACE(0, 2, "listen sk shutdown");
		if (how & RCV_SHUTDOWN) {
			err = 0;
			sock->state = SS_UNCONNECTED;
		}
	} else {
		if (sk->sk_state == MATT_SS_CLOSED) {
#line 704
			MATT_TRACE(0, 2, "goto fallthrough");
			err = -ENOTCONN;
		}
#line 710
		MATT_TRACE(0, 2, "goto sk_shutdown");
		sk->sk_shutdown |= how;
		if (sk->sk_prot->shutdown)
			sk->sk_prot->shutdown(sk, how);
	}
	sk->sk_state_change(sk);
	release_sock(sk);
	return err;
}

/* ---- connect / poll ---- */
static inline int __matt_inet_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	struct sock *sk;
	long timeo;
	int err;

	if (addr_len < sizeof(uaddr->sa_family))	/* G1ad: unsigned compare as in the stock [b.hs] */
		return -EINVAL;
	sk = sock->sk;
	if (uaddr && uaddr->sa_family == AF_UNSPEC) {
		err = sk->sk_prot->disconnect(sk, flags);
		sock->state = err ? SS_DISCONNECTING : SS_UNCONNECTED;
		return err;
	}
	switch (sock->state) {
	default:
		return -EINVAL;
	case SS_CONNECTED:
		return -EISCONN;
	case SS_CONNECTING:
		err = -EALREADY;
		break;
	case SS_UNCONNECTED:
		if (sk->sk_state != MATT_SS_CLOSED)
			return -EISCONN;
		err = sk->sk_prot->connect(sk, uaddr, addr_len);
		if (err < 0)
			return err;
		sock->state = SS_CONNECTING;
		err = -EINPROGRESS;
		break;
	}
	timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);
	if (sk->sk_state == MATT_SS_ESTABLISHING) {
		DEFINE_WAIT_FUNC(wait, woken_wake_function);

		if (!timeo)
			return err;
		add_wait_queue(sk_sleep(sk), &wait);
		while (sk->sk_state == MATT_SS_ESTABLISHING) {
			release_sock(sk);
			timeo = wait_woken(&wait, TASK_INTERRUPTIBLE, timeo);
			lock_sock(sk);
			if (signal_pending(current) || !timeo)
				break;
		}
		remove_wait_queue(sk_sleep(sk), &wait);
		if (!timeo)
			return err;
		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			return err;
	}
	if (sk->sk_state == MATT_SS_CLOSED) {
		err = sock_error(sk) ? : -ECONNABORTED;
		sock->state = SS_UNCONNECTED;
		if (sk->sk_prot->disconnect(sk, flags))
			sock->state = SS_DISCONNECTING;
		return err;
	}
	sock->state = SS_CONNECTED;
	return 0;
}

int matt_inet_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	int err;

	lock_sock(sock->sk);
	err = __matt_inet_connect(sock, uaddr, addr_len, flags);
	release_sock(sock->sk);
	return err;
}

static inline bool matt_epollin_ready(struct sock *sk, int target)
{
	struct matt_sock *msk = MATT_SK(sk);
	int avail;

	if (!skb_queue_empty(&sk->sk_receive_queue)) {
#line 452
		MATT_TRACE(0, 9, "sk_receive_queue not empty target=%u copied=%u nxt=%u backlog=%u r_alloc=%u", target,
			   msk->input_ctx.copied, msk->input_ctx.rcv_nxt, sk->sk_backlog.len, atomic_read(&sk->sk_rmem_alloc));
		return true;
	}
	avail = READ_ONCE(msk->input_ctx.rcv_nxt) - READ_ONCE(msk->input_ctx.copied);
	if (avail >= target) {
#line 459
		MATT_TRACE(0, 9, "avail:%d target:%d", avail, target);
		return true;
	}
	return sk_is_readable(sk);
}

unsigned int matt_inet_poll(struct file *file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	__poll_t mask;
	int shutdown;

	sock_poll_wait(file, sock, wait);
	if (sk->sk_state == MATT_SS_LISTENING)
		return list_empty(&MATT_SK(sk)->reqs) ? 0 : (EPOLLIN | EPOLLRDNORM);
	shutdown = READ_ONCE(sk->sk_shutdown);
	if (shutdown == SHUTDOWN_MASK || sk->sk_state == MATT_SS_CLOSED)
		mask = EPOLLHUP;
	else
		mask = 0;
	if (sk->sk_state == MATT_SS_ESTABLISHED) {
		int target = sock_rcvlowat(sk, 0, INT_MAX);

		if (matt_epollin_ready(sk, target))
			mask |= EPOLLIN | EPOLLRDNORM;
		if (__sk_stream_is_writeable(sk, 1)) {
			mask |= EPOLLOUT | EPOLLWRNORM;
		} else {
			sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);
			set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
#line 533
			MATT_TRACE(0, 9, "NOSPACE, send SIGIO later");
			smp_mb__after_atomic();
			if (__sk_stream_is_writeable(sk, 1))
				mask |= EPOLLOUT | EPOLLWRNORM;
		}
	}
	smp_rmb();
	if (READ_ONCE(sk->sk_err) || !skb_queue_empty_lockless(&sk->sk_error_queue))
		mask |= EPOLLERR;
	return mask;
}

/* ---- accept / recvmsg ---- */
extern bool (*xm_sk_busy_loop_end)(void *p, unsigned long start_time);
extern void (*xm_napi_busy_loop)(unsigned int napi_id, bool (*loop_end)(void *, unsigned long), void *loop_end_arg, bool prefer_busy_poll, u16 budget);
int matt_recvmsg_locked(struct sock *sk, struct msghdr *msg, size_t len, int flags);

struct sock *matt_accept(struct sock *sk, int flags, int *errp, bool kern)
{
	struct matt_sock *msk = MATT_SK(sk);
	struct matt_request_sock *req = NULL;
	struct sock *newsk = NULL;
	long timeo;
	int error;

	lock_sock(sk);
	if (sk->sk_state != MATT_SS_LISTENING) {
		error = -EINVAL;
		goto out;
	}
	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);
	{
		DEFINE_WAIT(wait);

		for (;;) {
			prepare_to_wait_exclusive(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
			release_sock(sk);
			if (list_empty(&msk->reqs))
				timeo = schedule_timeout(timeo);
			lock_sock(sk);
			error = -EINVAL;
			if (sk->sk_state != MATT_SS_LISTENING)
				break;
			error = 0;
			if (!list_empty(&msk->reqs))
				break;
			error = sock_intr_errno(timeo);
			if (signal_pending(current))
				break;
			error = -EAGAIN;
			if (!timeo)
				break;
		}
		finish_wait(sk_sleep(sk), &wait);
	}
	if (error) {
#line 2507
		MATT_TRACE(0, 2, "sk:%p wait for accept fail", sk);
		goto out;
	}
	req = matt_request_sock_dequeue(sk);
	newsk = sk_alloc(sock_net(sk), sk->sk_family, GFP_KERNEL, sk->sk_prot, kern);
	if (!newsk) {
#line 2531
		MATT_TRACE(0, 2, "no memory");
		error = -ENOMEM;
		goto out;
	}
	sock_init_data(NULL, newsk);
	error = newsk->sk_prot->init(newsk);
	if (error) {
#line 2540
		MATT_TRACE(0, 2, "sk init fail");
		goto close;
	}
	newsk->sk_type = sk->sk_type;
	newsk->sk_flags = sk->sk_flags;
	newsk->sk_protocol = IPPROTO_MATT;
	newsk->sk_backlog_rcv = sk->sk_prot->backlog_rcv;
	newsk->sk_sndbuf = sk->sk_sndbuf;
	newsk->sk_rcvbuf = sk->sk_rcvbuf;
	newsk->sk_rcvtimeo = sk->sk_rcvtimeo;
	newsk->sk_sndtimeo = sk->sk_sndtimeo;
	inet_sk(newsk)->pmtudisc = inet_sk(sk)->pmtudisc;
	MATT_SK(newsk)->input_ctx.rcv_nxt = req->rcv_isn + 1;
	MATT_SK(newsk)->input_ctx.rcv_nxt_num = req->rcv_num + 1;
	MATT_SK(newsk)->input_ctx.copied = req->rcv_isn + 1;
	if (sk->sk_family == AF_INET6)
		inet_sk(newsk)->pinet6 = &((struct matt6_sock *)newsk)->inet6;
	error = newsk->sk_prot->bind(newsk, (struct sockaddr *)&req->sa, sizeof(req->sa));
	if (error) {
#line 2552
		MATT_TRACE(0, 2, "nsk bind fail");
		goto close;
	}
	error = matt_accept_sock_init(newsk, req);
	if (error) {
#line 2558
		MATT_TRACE(0, 2, "accept sk init fail");
		goto close;
	}
	goto out;
close:
#line 2570
	MATT_TRACE(0, 9, "close nsk:%p", newsk);
	newsk->sk_prot->close(newsk, 0);
	newsk = NULL;
out:
	release_sock(sk);
#line 2564
	MATT_TRACE(0, 4, "ret:%d sk:%p nsk:%p", error, sk, newsk);
	kfree(req);
	*errp = error;
	return newsk;
}

int matt_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, int flags, int *addr_len)
{
	int ret;

	if (sk_can_busy_loop(sk) && skb_queue_empty_lockless(&sk->sk_receive_queue) && sk->sk_state == MATT_SS_ESTABLISHED) {
		int nonblock = flags & MSG_DONTWAIT;	/* G56a: the stock passes the raw masked value (0x40 or 0) to the trace, not a bool */
		unsigned int napi_id;

#line 2315
		MATT_TRACE(0, 8, "start sk_busy_loop MSG_DONTWAIT:%d", nonblock);
		napi_id = READ_ONCE(sk->sk_napi_id);
		if (napi_id >= MIN_NAPI_ID)
			xm_napi_busy_loop(napi_id, nonblock ? NULL : xm_sk_busy_loop_end, sk, READ_ONCE(sk->sk_prefer_busy_poll),
					  READ_ONCE(sk->sk_busy_poll_budget) ?: BUSY_POLL_BUDGET);
	}
	lock_sock(sk);
	ret = matt_recvmsg_locked(sk, msg, len, flags);
	release_sock(sk);
#line 2322
	MATT_TRACE(0, 9, "done, rcvmsg len:%d", ret);
	return ret;
}

/* ---- sockopt / release_cb / do_recv ---- */
#define SOL_MATT 0x122
#define MATT_DEFERRED_ALL 0x3fc
void matt_rcv_established(struct sock *sk, struct sk_buff *skb);
int matt_rcv_state_process(struct sock *sk, struct sk_buff *skb);

int matt_setsockopt(struct sock *sk, int level, int optname, sockptr_t optval, unsigned int optlen)
{
	char name[16] = {};
	int ret;

	if (level != SOL_MATT) {
#line 1062
		MATT_TRACE(0, 2, "not support yet!");
		return 0;
	}
	if (optname != 2)
		return 0;
	if (!optlen)
		return -EINVAL;
	ret = strncpy_from_sockptr(name, optval, min_t(unsigned int, optlen, sizeof(name) - 1));
	if (ret < 0)
		return -EFAULT;
	name[ret] = 0;
	xm_sockopt_lock_sock(sk);
	ret = matt_set_ulp(sk, name);
	xm_sockopt_release_sock(sk);
	return ret;
}

/* global (stock keeps an out-of-line copy) AND inlined into matt_getsockopt */
void matt_get_info(struct sock *sk, struct matt_info *info)
{
	struct matt_output_ctx *oq = &MATT_SK(sk)->output_ctx;
	bool slow;

	memset(info, 0, sizeof(*info));
	slow = xm__lock_sock_fast(sk);
	info->rtt = oq->srtt;
	info->rto = oq->rto;
	info->rttvar = 0;
	info->min_rtt = oq->min_rtt;
#line 1104
	MATT_TRACE(0, 8, "sk:%p rtt:%u minrtt:%u rto:%u", sk, oq->srtt, oq->min_rtt, oq->rto);
	unlock_sock_fast(sk, slow);
}

int matt_getsockopt(struct sock *sk, int level, int optname, char __user *optval, int __user *optlen)
{
	int len;
	struct matt_info info;

	if (level != SOL_MATT) {
#line 1150
		MATT_TRACE(0, 2, "not support yet!");
		return 0;
	}
	len = 0;
	if (copy_from_sockptr(&len, USER_SOCKPTR(optlen), sizeof(len)))
		return -EFAULT;
	if (len < 0)
		return -EINVAL;
	len = min_t(u32, len, 4);
	if (optname != 1)
		return -ENOPROTOOPT;
	if (copy_from_sockptr(&len, USER_SOCKPTR(optlen), sizeof(len)))
		return -EFAULT;
	[[clang::always_inline]] matt_get_info(sk, &info);	/* locks internally: the stock getsockopt has ONE lock_sock_fast (inside the inlined body) */
	len = min_t(u32, len, sizeof(info));
	if (copy_to_sockptr(USER_SOCKPTR(optlen), &len, sizeof(len)))
		return -EFAULT;
	if (copy_to_sockptr(USER_SOCKPTR(optval), &info, len))
		return -EFAULT;
	return 0;
}

void matt_release_cb(struct sock *sk)
{
	unsigned long flags = smp_load_acquire(&sk->sk_tsq_flags), nflags;

	do {
		if (!(flags & MATT_DEFERRED_ALL))
			return;
		nflags = flags & ~MATT_DEFERRED_ALL;
	} while (!try_cmpxchg(&sk->sk_tsq_flags, &flags, nflags));
	if (flags & MATT_F_MSQ_DEFERRED) {
		[[clang::__noinline__]] matt_msq_write(sk);
		__sock_put(sk);
	}
	if (flags & MATT_F_WRITE_TIMER_DEFERRED) {
		matt_write_timer_handler(sk);
		__sock_put(sk);
	}
	if (flags & MATT_F_RTO_TIMER_DEFERRED) {
		matt_rto_timer_handler(sk);
		__sock_put(sk);
	}
	if (flags & MATT_F_REO_TIMER_DEFERRED) {
		matt_reo_timer_handler(sk);
		__sock_put(sk);
	}
	if (flags & MATT_F_PACK_TIMER_DEFERRED) {
		matt_pack_timer_handler(sk);
		__sock_put(sk);
	}
	if (flags & MATT_F_NACK_TIMER_DEFERRED) {
		matt_nack_timer_handler(sk);
		__sock_put(sk);
	}
	if (flags & MATT_F_PING_TIMER_DEFERRED) {
		matt_ping_timer_handler(sk);
		__sock_put(sk);
	}
	if (flags & MATT_F_STATS_TIMER_DEFERRED) {
		matt_stats_timer_handler(sk);
		__sock_put(sk);
	}
}

int matt_do_recv(struct sock *sk, struct sk_buff *skb)
{
	struct matt_input_ctx *in = &MATT_SK(sk)->input_ctx;
	int reason;
	u32 segs;

	if (matt_invalid_packet_check(skb)) {
		reason = 0;
		goto drop;
	}
	if (sk->sk_state == MATT_SS_ESTABLISHED) {
		sock_rps_save_rxhash(sk, skb);
		sk_mark_napi_id(sk, skb);	/* includes sk_rx_queue_update */
		matt_rcv_established(sk, skb);
		return 0;
	}
	reason = matt_rcv_state_process(sk, skb);
	if (!reason)
		return 0;
drop:
	segs = max_t(u16, skb_shinfo(skb)->gso_segs, 1);
	in->dropped[STATS_DROP_INVAL_DATA].data_num += segs;
	in->dropped[STATS_DROP_ALL].data_num += segs;
	in->dropped[STATS_DROP_INVAL_DATA].data_bytes += skb->len - 16;
	in->dropped[STATS_DROP_ALL].data_bytes += skb->len - 16;
#line 2609
	MATT_TRACE(0, 9, "drop done, reason:%d", reason);
	kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
	return reason;
}

/* stock: matt_set_state is inlined into matt_reset (other TU in our split) */
void matt_reset(struct sock *sk)
{
	matt_set_state(sk, MATT_SS_CLOSED);
	matt_timer_free(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;
	if (!sock_flag(sk, SOCK_DEAD)) {
#line 1648
		MATT_TRACE(0, 2, "sk:%p sk_state_change", sk);
		sk->sk_state_change(sk);
	} else {
#line 1651
		MATT_TRACE(0, 2, "sk:%p matt_csk_destroy_sock", sk);
		matt_csk_destroy_sock(sk);
	}
}
