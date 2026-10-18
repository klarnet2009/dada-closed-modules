/* SPDX-License-Identifier: GPL-2.0 */
/* hand-written declarations shared by the matt sources (types come from the BTF-generated matt.h) */
#ifndef _MATT_DECL_H
#define _MATT_DECL_H

extern int sysctl_matt_debug;
extern int sysctl_limit_output;

static inline bool before(__u32 seq1, __u32 seq2)
{
	return (__s32)(seq1 - seq2) < 0;
}
#define after(seq2, seq1) before(seq1, seq2)

static inline void skb_header_release(struct sk_buff *skb)
{
	skb->nohdr = 1;
	atomic_set(&skb_shinfo(skb)->dataref, 1 + (1 << SKB_DATAREF_SHIFT));
}
#define tcp_skb_tsorted_save(skb) { unsigned long _save = skb->_skb_refdst; skb->_skb_refdst = 0UL;
#define tcp_skb_tsorted_restore(skb) skb->_skb_refdst = _save; }

/* CRC forging of the 3 exports (genksyms-only padding, see STATUS.md): an extra tag parameter visible to genksyms only */
#ifdef __GENKSYMS__
#define GKPAD(tag)	, struct tag *gkpad
#else
#define GKPAD(tag)
#endif

#define MATT_TRACE(mod, lvl, fmt, ...) matt_trace_msg(mod, lvl, "%s: %d: " fmt, __func__, __LINE__, ##__VA_ARGS__)

/* logging (matt_log.c) */
extern struct category_name_info g_matt_category_name[MATT_MODULE_ID_MAX];
extern const u64 conversion_bits_scaled[5];
char *matt_drop_reason_to_str(enum matt_drop_reason type);
char *matt_types_to_str(u8 type);
void adaptive_bit_unit_to_str(char *s, int len, long long bps);
void matt_get_time_of_the_day_in_hr_min_sec_usec(char *tbuf, int len);
uint64_t matt_get_time_of_the_day_us(void);
void matt_trace_msg_cmn(unsigned int idx, MATT_MODULE_ID category, MATT_TRACE_LEVEL verbose, const char *str_format, va_list val);
void matt_trace_msg(MATT_MODULE_ID module, MATT_TRACE_LEVEL level, const char *str_format GKPAD(fcrcr2lXe0hxAQ), ...);
void matt_hex_dump(const char *fn, const char *prefix_str, int prefix_type, int rowsize, int groupsize, const void *buf, size_t len, bool ascii);
void matt_dump_creq(struct matt_creq *creq);
void matt_dump_crsp(struct matt_crsp *crsp);
void matt_dump_skb(const uint8_t *fn, struct sk_buff *skb);

/* address families (matt_addr.c) */
extern int sysctl_inet_enable;
u32 matt_encap_len2(union matt_sockaddr *a);
u32 matt_encap_lower_header_len2(union matt_sockaddr *a);
int matt_is_any_addr2(union matt_sockaddr *a);
void matt_udp_conf_init2(struct sock *sk, struct udp_port_cfg *conf, union matt_sockaddr *a);
int matt_flow_route2(struct sock *sk, union matt_sockaddr *da, union matt_sockaddr *sa, struct flowi *fl);
int matt_lower_xmit2(struct sock *sk, struct sk_buff *skb, union matt_sockaddr *da, struct flowi *fl, bool mgmt);
void matt_get_skb_addrs2(struct sk_buff *skb, union matt_sockaddr *da, union matt_sockaddr *sa);
int matt_get_user_addr2(struct sock *sk, union matt_sockaddr *a, struct sockaddr *addr, int addr_len);
bool matt_cmp_sk_addr2(struct sock *sk, union matt_sockaddr *a, union matt_sockaddr *addr);
int matt_get_sk_addr2(struct socket *sock, struct sockaddr *a, int peer);
void matt_set_sk_addr2(struct sock *sk, union matt_sockaddr *a, bool src);
int matt_inet4_is_any_addr(union matt_sockaddr *a);
void matt_inet4_udp_conf_init(struct sock *sk, struct udp_port_cfg *conf, union matt_sockaddr *a);
int matt_inet4_flow_route(struct sock *sk, union matt_sockaddr *da, union matt_sockaddr *sa, struct flowi *fl);
int matt_inet4_lower_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl, bool mgmt);
void matt_inet4_get_skb_addrs(struct sk_buff *skb, union matt_sockaddr *da, union matt_sockaddr *sa);
int matt_inet4_get_user_addr(struct sock *sk, union matt_sockaddr *a, struct sockaddr *addr, int addr_len);
bool matt_inet4_cmp_sk_addr(struct sock *sk, union matt_sockaddr *a, union matt_sockaddr *addr);
int matt_inet4_get_sk_addr(struct socket *sock, struct sockaddr *uaddr, int peer);
void matt_inet4_set_sk_addr(struct sock *sk, union matt_sockaddr *a, bool src);
int matt_inet6_is_any_addr(union matt_sockaddr *a);
void matt_inet6_udp_conf_init(struct sock *sk, struct udp_port_cfg *conf, union matt_sockaddr *a);
int matt_inet6_flow_route(struct sock *sk, union matt_sockaddr *da, union matt_sockaddr *sa, struct flowi *fl);
int matt_inet6_lower_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl, bool mgmt);
void matt_inet6_get_skb_addrs(struct sk_buff *skb, union matt_sockaddr *da, union matt_sockaddr *sa);
int matt_inet6_get_user_addr(struct sock *sk, union matt_sockaddr *a, struct sockaddr *addr, int addr_len);
bool matt_inet6_cmp_sk_addr(struct sock *sk, union matt_sockaddr *a, union matt_sockaddr *addr);
int matt_inet6_get_sk_addr(struct socket *sock, struct sockaddr *uaddr, int peer);
void matt_inet6_set_sk_addr(struct sock *sk, union matt_sockaddr *a, bool src);
int xm_inet6_getname2(struct socket *sock, struct sockaddr *uaddr, int peer);

/* rate sampling / small helpers (matt_rate.c) */
void matt_rate_skb_sent(struct sock *sk, struct sk_buff *skb);
void matt_rate_skb_delivered(struct sock *sk, struct sk_buff *skb, struct rate_sample *rs);
void matt_rate_gen(struct sock *sk, u32 delivered, u32 lost, struct rate_sample *rs);
void matt_rate_check_app_limited(struct sock *sk);
void matt_mgmt_init(struct sock *sk);
void matt_mstamp_refresh(struct sock *sk);
void matt_reset_cur_rtt(struct sock *sk);
void matt_update_cur_rtt(struct sock *sk, u32 ts, u32 ack_delay);
void matt_check_space(struct sock *sk);
/* congestion control (matt_cc.c) */
u32 matt_minmax_running_max(struct minmax *m, u32 win, u32 t, u32 meas);
u32 matt_minmax_running_min(struct minmax *m, u32 win, u32 t, u32 meas);

/* ULP registry (matt_ulp.c) */
int matt_register_ulp(struct matt_ulp_ops *ulp GKPAD(fcrcNt97LBxR01));
void matt_unregister_ulp(struct matt_ulp_ops *ulp GKPAD(fcrcHnp4vrkGae));
void matt_get_available_ulp(char *buf, size_t maxlen);
void matt_update_ulp(struct sock *sk, struct proto *proto, void (*write_space)(struct sock *));
void matt_cleanup_ulp(struct sock *sk);
int matt_set_ulp(struct sock *sk, const char *name);

/* timers (matt_timer.c) */
int matt_timer_pending(struct sock *sk, u8 type);
void matt_timer_stop(struct sock *sk, u8 type);
void matt_timer_reset(struct sock *sk, u8 type, u64 timeout);
void matt_timer_start(struct sock *sk, u8 type, u64 timeout);
void matt_timer_reduce(struct sock *sk, u8 type, u64 timeout);
void matt_timer_free(struct sock *sk);
void matt_timer_init(struct sock *sk);
void matt_timer_write_timeout(struct timer_list *t);
void matt_timer_pack_timeout(struct timer_list *t);
void matt_timer_nack_timeout(struct timer_list *t);
void matt_timer_ping_timeout(struct timer_list *t);
void matt_timer_reo_timeout(struct timer_list *t);
void matt_timer_rto_timeout(struct timer_list *t);
void matt_timer_stats_timeout(struct timer_list *t);
void matt_write_timer_handler(struct sock *sk);
void matt_pack_timer_handler(struct sock *sk);
void matt_nack_timer_handler(struct sock *sk);
void matt_ping_timer_handler(struct sock *sk);
void matt_reo_timer_handler(struct sock *sk);
void matt_rto_timer_handler(struct sock *sk);
void matt_stats_timer_handler(struct sock *sk);

void matt_set_cc_config(struct sock *sk);

void mibbr_main(struct sock *sk, const struct rate_sample *rs);

/* packet builders / senders (matt_make2.c, matt_send.c) */
struct sk_buff *matt_make_ping(struct sock *sk);
struct sk_buff *matt_make_pong(struct sock *sk, u32 timestamp);
struct sk_buff *matt_make_rst(struct sock *sk, u32 reasoncode);
struct sk_buff *matt_make_report(struct sock *sk);
struct sk_buff *matt_make_creq(struct sock *sk);
struct sk_buff *matt_make_crsp(struct sock *sk, u32 timestamp);
struct sk_buff *matt_make_pack(struct sock *sk);
struct sk_buff *matt_make_nack(struct sock *sk);
int matt_transmit_mgmt_skb(struct sock *sk, struct sk_buff *skb, int clone_it, struct flowi *fl);
int matt_transmit_skb(struct sock *sk, struct sk_buff *skb, int clone_it);
int matt_send_crsp(struct sock *sk, u32 timestamp);
int matt_send_pack(struct sock *sk);
int matt_send_nack(struct sock *sk);
int matt_send_eack(struct sock *sk);
void matt_send_dup_pack(struct sock *sk);
void matt_send_dup_nack(struct sock *sk);
void matt_wmem_free_skb(struct sock *sk, struct sk_buff *skb);
void matt_skb_entail(struct sock *sk, struct sk_buff *skb);
void matt_mgmt_queue_skb(struct sock *sk, struct sk_buff *skb);
void matt_update_skb_after_send(struct sock *sk, struct sk_buff *skb);
void matt_update_mss(struct sock *sk, union matt_sockaddr *da);
void matt_send_ack(struct sock *sk);
void matt_data_wfree(struct sk_buff *skb);
void matt_tasklet_func(struct tasklet_struct *t);
void matt_tasklet_init(void);
void matt_msq_write(struct sock *sk);
void matt_write_xmit(struct sock *sk, int push_one);
void matt_xmit_retransmit_queue(struct sock *sk);
void matt_outq_free(struct sock *sk);
void matt_reset(struct sock *sk);
int matt_hash_tables_init(void);
void matt_hash_tables_destroy(void);
int matt_offload_init(void);
int mattv6_offload_init(void);
void matt_offload_del(void);
void mattv6_offload_del(void);

/* kernel symbols resolved at init (defined in the files that use them, filled by matt_init.h) */
extern struct dst_entry *(*xm_sk_dst_check)(struct sock *sk, u32 cookie);
extern int (*xm_sk_mem_schedule)(struct sock *sk, int size, int kind);
extern bool (*xm__lock_sock_fast)(struct sock *sk);
extern struct ip6_flowlabel *(*xm__fl6_sock_lookup)(struct sock *sk, __be32 label);
extern struct dst_entry *(*xm_ip6_dst_lookup_flow)(struct net *net, const struct sock *sk, struct flowi6 *fl6, const struct in6_addr *final_dst);
extern u32 (*xm__get_hash_from_flowi6)(const struct flowi6 *fl6, struct flow_keys *keys);

/* socket layer (matt_sock.h, included from matt_main.c) */
extern struct workqueue_struct *matt_wq;
u32 matt_listen_sock_hash(struct net *net, u16 port);
u32 matt_sock_hash(struct net *net, union matt_sockaddr *s, union matt_sockaddr *d);
u32 matt_addr_hash(struct net *net, union matt_sockaddr *a);
void matt_set_state(struct sock *sk, int state);
int matt_hash(struct sock *sk);
void matt_unhash(struct sock *sk);
struct matt_shash_head *matt_sock_head(u32 hash);
struct matt_shash_head *matt_listen_sock_head(u32 hash);
struct matt_uhash_head *matt_udp_sock_head(struct net *net, u16 port);
#ifndef SKB_SMALL_HEAD_HEADROOM
#define SKB_SMALL_HEAD_HEADROOM 320
#endif
#ifndef MAX_TCP_HEADER
#define MAX_TCP_HEADER 192
#endif
void matt_csk_destroy_sock(struct sock *sk);
int matt_rcv(struct sk_buff *skb);
void matt_mark_skb_lost(struct sock *sk, struct sk_buff *skb);
void matt_rack_detect_loss(struct sock *sk, u32 *reo_timeout);
bool matt_rack_mark_lost(struct sock *sk);
void matt_mark_lost(struct sock *sk, u32 ack_offset);
void matt_mark_lost_now(struct sock *sk, u32 ack_offset);
void matt_rearm_rto(struct sock *sk);
int _xm_skb_unclone_keeptruesize(struct sk_buff *skb, gfp_t pri);
void __matt_push(struct sock *sk);
void matt_push(struct sock *sk, int size_goal);
void matt_remove_empty_skb(struct sock *sk);
struct sk_buff *matt_stream_alloc_skb(struct sock *sk, gfp_t gfp, bool force);
int matt_send_mss(struct sock *sk, int *size_goal, int flags);
int matt_skb_copy_to_page_nocache(struct sock *sk, struct iov_iter *from, struct sk_buff *skb, struct page *page, int off, int copy);
void matt_cleanup_rbuf(struct sock *sk, int copied);
unsigned int matt_inq_free(struct sock *sk);
void matt_rtx_queue_purge(struct sock *sk);
bool matt_add_backlog(struct sock *sk, struct sk_buff *skb);
bool judgement_for_rst(struct sock *sk);
int matt_established_process_creq(struct sock *sk, struct sk_buff *skb);
int matt_send_rst(struct sock *sk, u32 reasoncode);
int matt_fragment(struct sock *sk, enum matt_queue tcp_queue, struct sk_buff *skb, u32 len, unsigned int mss_now, gfp_t gfp);
int __matt_retransmit_skb(struct sock *sk, struct sk_buff *skb, int segs);
int matt_trim_head(struct sock *sk, struct sk_buff *skb, u32 len);
struct sk_buff *matt_gro_receive(struct sock *sk, struct list_head *head, struct sk_buff *skb);
int matt_gro_complete(struct sock *sk, struct sk_buff *skb, int nhoff);

#endif
