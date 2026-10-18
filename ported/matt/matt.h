/* SPDX-License-Identifier: GPL-2.0 */
/*
 * matt: Xiaomi Adaptive Transfer Tunnel. Structure layouts recovered from the BTF of the OS4 stock matt.ko
 * (g1s_tools/dumpmod.py + genhdr.py; sizes asserted at the end). NOT hardware-tested: DO NOT LOAD.
 */
#ifndef _MATT_H
#define _MATT_H
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/jhash.h>
#include <linux/win_minmax.h>
#include <linux/rbtree.h>
#include <linux/interrupt.h>
#include <linux/list_nulls.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#include <net/net_namespace.h>
#include <net/netns/hash.h>
#include <net/flow.h>
#include <net/udp_tunnel.h>
#include <net/busy_poll.h>

#pragma clang diagnostic ignored "-Wmissing-prototypes"

typedef enum { MATT_MODULE_ID_START = 0, MATT_MODULE_ID_MAIN = 1, MATT_MODULE_ID_SOCK = 2, MATT_MODULE_ID_CC = 3, MATT_MODULE_ID_TX = 4, MATT_MODULE_ID_RX = 5, MATT_MODULE_ID_MEM = 6, MATT_MODULE_ID_METRIC = 7, MATT_MODULE_ID_ANY = 8, MATT_MODULE_ID_MAX = 9 } MATT_MODULE_ID;
typedef enum { MATT_TRACE_LEVEL_NONE = 0, MATT_TRACE_LEVEL_FATAL = 1, MATT_TRACE_LEVEL_ERROR = 2, MATT_TRACE_LEVEL_WARN = 3, MATT_TRACE_LEVEL_INFO = 4, MATT_TRACE_LEVEL_INFO_HIGH = 5, MATT_TRACE_LEVEL_INFO_MED = 6, MATT_TRACE_LEVEL_INFO_LOW = 7, MATT_TRACE_LEVEL_DEBUG = 8, MATT_TRACE_LEVEL_TRACE = 9, MATT_TRACE_LEVEL_ALL = 10, MATT_TRACE_LEVEL_MAX = 11 } MATT_TRACE_LEVEL;
enum { UNIT_CONV = 0, KILO_CONV = 1, MEGA_CONV = 2, GIGA_CONV = 3, TERA_CONV = 4 };
typedef enum { MATT_MGMT_TYPE_CONN_REQ = 0, MATT_MGMT_TYPE_CONN_RSP = 1, MATT_MGMT_TYPE_RESET = 2, MATT_MGMT_TYPE_PACK = 3, MATT_MGMT_TYPE_NACK = 4, MATT_MGMT_TYPE_PING = 5, MATT_MGMT_TYPE_PONG = 6, MATT_MGMT_TYPE_REPORT = 7, MATT_MGMT_TYPE_MAX = 255 } MATT_MGMT_TYPE;
enum { IPPROTO_MATT = 168 };
enum { MATT_VERSION_1 = 1, MATT_VERSION_2 = 2, MATT_VERSION_MAX = 255 };
enum { MATT_STATUS_SUCCESS = 0, MATT_STATUS_INVALID_CON_REQ = 1, MATT_STATUS_INVALID_NO_MEMORY = 2 };
enum matt_state { MATT_SS_CLOSED = 7, MATT_SS_LISTENING = 10, MATT_SS_ESTABLISHING = 3, MATT_SS_ESTABLISHED = 1 };
enum matt_ca_event { CA_EVENT_TX_START = 0, CA_EVENT_CWND_RESTART = 1, CA_EVENT_COMPLETE_CWR = 2, CA_EVENT_LOSS = 3, CA_EVENT_ECN_NO_CE = 4, CA_EVENT_ECN_IS_CE = 5 };
enum matt_drop_reason { MATT_SKB_NOT_DROPPED_YET = 0, MATT_SKB_DROP_REASON_OLD_DATA = 1, MATT_SKB_DROP_REASON_OVERWINDOW = 2, MATT_SKB_DROP_REASON_OVERWINDOW_PARTIAL = 3 };
enum matt_recv_stats { STATS_DROP_OVERFLOW = 0, STATS_DROP_OVERWINDOW = 1, STATS_DROP_OLD_DATA = 2, STATS_DROP_INVAL_DATA = 3, STATS_DROP_OLD_ACK = 4, STATS_DROP_ALL = 5, STATS_DROP_OVERWINDOW_PARTIAL = 6, STATS_OFO = 7, STATS_RECV_MAX = 8 };
enum matt_msq_flags { MATT_MSQF_THROTTLED = 1, MATT_MSQF_QUEUED = 2, MATT_F_MSQ_DEFERRED = 4, MATT_F_WRITE_TIMER_DEFERRED = 8, MATT_F_REO_TIMER_DEFERRED = 16, MATT_F_PACK_TIMER_DEFERRED = 32, MATT_F_NACK_TIMER_DEFERRED = 64, MATT_F_PING_TIMER_DEFERRED = 128, MATT_F_RTO_TIMER_DEFERRED = 256, MATT_F_STATS_TIMER_DEFERRED = 512 };
enum matt_queue { MATT_FRAG_IN_WRITE_QUEUE = 0, MATT_FRAG_IN_RTX_QUEUE = 1 };
enum matt_send_stats { STATS_LOST = 0, STATS_RELOST = 1, STATS_RTO = 2, STATS_RETRY = 3, STATS_FACKED = 4, STATS_SACKED = 5, STATS_NACK_LOST = 6, STATS_RACK_LOST = 7, STATS_SEND_MAX = 8 };
enum matt_msq_enum { MSQ_THROTTLED = 0, MSQ_QUEUED = 1, MATT_MSQ_DEFERRED = 2, MATT_WRITE_TIMER_DEFERRED = 3, MATT_REO_TIMER_DEFERRED = 4, MATT_PACK_TIMER_DEFERRED = 5, MATT_NACK_TIMER_DEFERRED = 6, MATT_PING_TIMER_DEFERRED = 7, MATT_RTO_TIMER_DEFERRED = 8, MATT_STATS_TIMER_DEFERRED = 9 };
enum matt_cc_algo { MATT_CC_ALG_BBR = 0, MATT_CC_ALG_MAX = 1 };
enum matt_ca_state { MATT_CA_Open = 0, MATT_CA_Disorder = 1, MATT_CA_CWR = 2, MATT_CA_Recovery = 3, MATT_CA_Loss = 4 };
enum kernel_symbols { SYM_INET_REGISTER_PROTOSW = 0, SYM_INET6_REGISTER_PROTOSW = 1, SYM_NR_FREE_BUFFER_PAGES = 2, SYM_INET_UNREGISTER_PROTOSW = 3, SYM_INET6_UNREGISTER_PROTOSW = 4, SYM___SK_DST_CHECK = 5, SYM___SK_MEM_SCHEDULE = 6, SYM_SK_PAGE_FRAG_REFILL = 7, SYM__COPY_FROM_ITER_NOCACHE = 8, SYM_SK_WAIT_DATA = 9, SYM_SK_BUSY_LOOP_END = 10, SYM_NAPI_BUSY_LOOP = 11, SYM_REUSEPORT_DETACH_SOCK = 12, SYM_MINMAX_RUNNING_MAX = 13, SYM_INET_SEND_PREPARE = 14, SYM_REUSEPORT_SELECT_SOCK = 15, SYM_REUSEPORT_ALLOC = 16, SYM_REUSEPORT_ADD_SOCK = 17, SYM_SMP_CALL_FUNC_SA = 18, SYM__LOCK_SOCK_FAST = 19, SYM_INET_ADD_OFFLOAD = 20, SYM_INET_DEL_OFFLOAD = 21, SYM_INET6_ADD_OFFLOAD = 22, SYM_INET6_DEL_OFFLOAD = 23, SYM_SKB_SEGMENT = 24, SYM_SKB_SPLIT = 25, SYM_SKB_UNCLONE_KT = 26, SYM_SKB_NAPI_PP = 27, SYM_SKB_CONDENSE = 28, SYM_INET_BIND = 29, SYM_INET6_BIND = 30, SYM_INET_RELEASE = 31, SYM_INET6_RELEASE = 32, SYM_INET6_GETNAME = 33, SYM_INET6_FL_EX = 34, SYM_INET6_FL_SK_LOOKUP = 35, SYM_INET6_DST_LOOKUP_FLOW = 36, SYM_INET6_GET_HASH_FI6 = 37, SYM_STREAM_KILL = 38, SYM_SOCKOPT_LOCK = 39, SYM_SOCKOPT_RELEASE = 40, SYM_COUNT = 41 };

/* tcp-style rate sample (as in include/net/tcp.h) */
struct ack_sample {
	u32 pkts_acked;
	s32 rtt_us;
	u32 in_flight;
};
struct rate_sample {
	u64 prior_mstamp;
	u32 prior_delivered;
	u32 prior_delivered_ce;
	s32 delivered;
	s32 delivered_ce;
	long interval_us;
	u32 snd_interval_us;
	u32 rcv_interval_us;
	long rtt_us;
	int losses;
	u32 acked_sacked;
	u32 prior_in_flight;
	u32 last_end_seq;
	bool is_app_limited;
	bool is_retrans;
	bool is_ack_delayed;
};

struct matt_sock;
struct matt_ulp_ops;
struct matt_request_sock;
struct matt_creq;

struct category_name_info {
	unsigned char category_name_str[32];
};
struct matt_cc_ctx {
	struct matt_cc_ops *ops;
	u8 ca_state;
	u64 ca_priv[13];
};
union matt_sockaddr {
	struct sockaddr_in6 v6;
	struct sockaddr_in v4;
	struct sockaddr sa;
};
struct matt_addr {
	union matt_sockaddr addr;
	u8 udp_bind : 1;
};
struct matt_dst {
	struct matt_addr addr;
	u32 mtu_info;
};
struct matt_data_stats {
	u64 data_bytes;
	u32 data_num;
};
struct matt_sack_block {
	u32 offset_start;
	u32 offset_end;
};
struct matt_stats {
	u64 total_bytes;
	u64 data_bytes;
	u32 total_num;
	u32 data_num;
	u32 pack_num;
	u32 nack_num;
	u32 req_num;
	u32 rsp_num;
	u32 rst_num;
	u32 ping_num;
	u32 pong_num;
};
struct matt_input_ctx {
	struct work_struct work;
	struct rb_root out_of_order_queue;
	struct sk_buff *ooo_last_skb;
	struct minmax max_bw;
	struct minmax min_srtt;
	u32 num_sacks;
	u32 sack_first_index;
	struct matt_sack_block selective_acks[128];
	u32 queue_bytes_trigger;
	u32 rcv_nxt;
	u32 rcv_wup;
	u32 rcv_high_seq;
	u32 last_rcv_high_seq;
	u32 rcv_nxt_num;
	u32 rcv_period;
	u32 rcv_srtt;
	u32 rcv_history_minrtt;
	u32 rcv_max_bw;
	u32 ack_bdp_th;
	u32 rcv_num;
	u32 min_owd;
	u32 min_owd_tx_ts;
	u32 min_owd_rx_ts;
	u32 ack_delay;
	u32 rcv_cur_num_ofo;
	u64 rcv_total_num_ofo;
	u32 nack_time;
	u32 pack_time;
	u32 eack_time;
	u32 nack_send;
	u32 eack_send;
	u32 pack_send;
	u64 bytes_in;
	u64 data_bytes_received;
	u32 data_mstamp;
	struct matt_stats trecv;
	struct matt_data_stats dropped[8];
	u32 copied;
	u32 last_copied;
	u32 last_bw;
	u32 last_rate;
	u32 data_segs_received;
	u32 rcv_wnd;
	u32 rcv_wnd_used;
	u32 rcv_wnd_stamp;
	u8 big_one;
};
struct matt_mgmt {
	u32 next_pn;
	union matt_sockaddr *sa;
	union matt_sockaddr *da;
	union matt_sockaddr saddr;
	union matt_sockaddr daddr;
	u64 matt_mstamp;
	u64 matt_clock_cache;
	u64 matt_wstamp_ns;
	u32 local_packet_num;
	u32 local_start_offset;
	u32 peer_packet_num;
	u32 peer_start_offset;
	u8 fail_cnt;
	u8 piong_flag;
	u8 creq_retry_cnt;
	u8 idle_check;
};
struct matt_rack {
	u64 mstamp;
	u32 rtt_us;
	u32 end_seq;
	u32 last_delivered;
	u8 reo_wnd_steps;
	u8 reo_wnd_persist : 5;
	u8 dsack_seen : 1;
	u8 advanced : 1;
};
struct matt_output_ctx {
	struct work_struct work;
	struct rb_root rb_rtx_queue;
	struct list_head tsorted_tx_queue;
	struct matt_sack_block sacks[128];
	u64 bytes_acked;
	u64 last_bytes_acked;
	u64 bytes_sent;
	u32 max_window;
	u32 write_seq;
	u32 pushed_seq;
	u32 snd_nxt;
	u32 snd_una;
	u32 snd_wnd;
	u32 srtt;
	u32 cur_rtt;
	u32 ack_delay;
	u32 rtt_var;
	u32 rto;
	u32 reorder;
	u32 min_rtt;
	u32 announce_srtt;
	u32 retrans_out;
	u32 sacked_out;
	u32 highest_sackoffset;
	u32 first_sackoffset;
	u32 sack_used;
	u32 lost_out;
	u32 packets_out;
	u32 packets_retransmit;
	struct matt_stats tsend;
	struct matt_data_stats send[8];
	u32 data_segs_out;
	u32 lsndtime;
	u32 snd_cwnd;
	u32 snd_cwnd_clamp;
	u32 snd_ssthresh;
	u32 rate_delivered;
	u32 rate_interval_us;
	struct minmax rtt_min;
	struct minmax max_bw;
	u32 last_bw;
	u32 snd_max_bw;
	u32 first_tx_mstamp;
	u32 delivered_mstamp;
	u32 delivered;
	u32 lost;
	u32 delivered_ce;
	u8 app_limited;
	u8 rate_app_limited;
	struct matt_rack rack;
};
struct matt_port {
	struct hlist_node node;
	unsigned short port;
	struct net *net;
	u8 server : 1;
};
struct matt_src {
	struct matt_addr addr;
	struct matt_port port;
	struct matt_udp_sock *udp_sk;
};
struct matt_nack_timer {
	struct timer_list timer;
	u32 nack_freq;
};
struct matt_pack_timer {
	struct timer_list timer;
	u32 pack_freq;
};
struct matt_ping_timer {
	struct timer_list timer;
	u32 wait_time;
};
struct matt_reo_timer {
	struct timer_list timer;
	u32 reo_rto;
};
struct matt_retrans_timer {
	struct timer_list timer;
	u32 retrans_rto;
};
struct matt_rto_timer {
	struct timer_list timer;
	u32 wait_time;
};
struct matt_stats_timer {
	struct timer_list timer;
	u32 wait_time;
};
struct matt_timer_ctx {
	struct matt_retrans_timer rtimer;
	struct matt_pack_timer ptimer;
	struct matt_nack_timer ntimer;
	struct matt_ping_timer pintimer;
	struct matt_reo_timer reotimer;
	struct matt_rto_timer rtotimer;
	struct matt_stats_timer stimer;
};
struct matt_sock {
	struct inet_sock inet;
	struct list_head reqs;
	struct matt_addr_family_ops *af_ops;
	const struct matt_ulp_ops *ulp_ops;
	struct matt_src src;
	struct matt_dst dst;
	struct flowi fl;
	struct matt_cc_ctx cc_ctx;
	struct matt_input_ctx input_ctx;
	struct matt_output_ctx output_ctx;
	struct list_head msq_node;
	struct matt_mgmt pkt_mgmt_ctx;
	struct matt_timer_ctx timer_ctx;
	u16 mss;
	u16 rcv_mss;
	u16 gso_segs;
};
struct matt6_sock {
	struct matt_sock matt;
	struct ipv6_pinfo inet6;
};
struct matt_addr_family_ops {
	u32 iph_len;
	int (*is_any_addr)(union matt_sockaddr *);
	void (*udp_conf_init)(struct sock *, struct udp_port_cfg *, union matt_sockaddr *);
	int (*flow_route)(struct sock *, union matt_sockaddr *, union matt_sockaddr *, struct flowi *);
	int (*lower_xmit)(struct sock *, struct sk_buff *, struct flowi *, bool);
	void (*get_skb_addrs)(struct sk_buff *, union matt_sockaddr *, union matt_sockaddr *);
};
struct matt_bbr_info {
	__u32 bbr_bw_lo;
	__u32 bbr_bw_hi;
	__u32 bbr_min_rtt;
	__u32 bbr_pacing_gain;
	__u32 bbr_cwnd_gain;
};
union matt_cc_info {
	struct matt_bbr_info bbr;
};
struct matt_cc_ops {
	u32 (*ssthresh)(struct sock *);
	void (*cong_avoid)(struct sock *, u32, u32);
	void (*set_state)(struct sock *, u8);
	void (*cwnd_event)(struct sock *, enum matt_ca_event);
	void (*in_ack_event)(struct sock *, u32);
	void (*pkts_acked)(struct sock *, const struct ack_sample *);
	u32 (*min_tso_segs)(struct sock *);
	void (*cong_control)(struct sock *, const struct rate_sample *);
	u32 (*undo_cwnd)(struct sock *);
	u32 (*sndbuf_expand)(struct sock *);
	size_t (*get_info)(struct sock *, u32, int *, union matt_cc_info *);
	char name[16];
	void (*init)(struct sock *);
	void (*release)(struct sock *);
} ____cacheline_aligned_in_smp;	/* like tcp_congestion_ops: BTF size 128 */
_Static_assert(sizeof(struct matt_cc_ops) == 128, "matt_cc_ops");
struct matthdr {
	__be32 num;
	__be32 offset;
	__be32 reserved;
	__u8 flags;
	__u8 type;
	__be16 len;
};
struct matt_creq {
	struct matthdr hdr;
	__be16 version;
	__be16 mss;
	__be32 ts;
	__be32 snd_buf;
	__be32 rcv_buf;
};
struct matt_crsp {
	struct matthdr hdr;
	__be32 status;
	__be16 version;
	__be16 mss;
	__be32 ts;
	__be32 snd_buf;
	__be32 rcv_buf;
};
struct matt_shash_table {
	struct matt_shash_head *hash;
	u32 size;
};
struct matt_uhash_table {
	struct matt_uhash_head *hash;
	u32 size;
};
struct matt_hashinfo {
	struct matt_shash_table lhash;
	struct matt_shash_table chash;
	struct matt_uhash_table uhash;
};
struct matt_info {
	__u32 rto;
	__u32 rtt;
	__u32 rttvar;
	__u32 min_rtt;
};
struct matt_nack {
	struct matthdr hdr;
	u32 recv_buffer;
	u32 sacks_size;
	struct matt_sack_block sacks[0];
};
struct matt_pack {
	struct matthdr hdr;
	u32 subtype;
	u32 timestamp;
	u32 delay_ack : 24;
	u32 loss_rate : 8;
	u32 recv_rate;
	u32 recv_buffer;
	u32 hol_offset;
	u32 left_offset;
	u32 right_offset;
	u32 sacks_size;
	struct matt_sack_block sacks[0];
};
struct matt_ping {
	struct matthdr hdr;
	__be32 timestamp;
};
struct matt_pong {
	struct matthdr hdr;
	__be32 timestamp;
};
struct matt_proto_family_ops {
	int (*get_user_addr)(struct sock *, union matt_sockaddr *, struct sockaddr *, int);
	bool (*cmp_sk_addr)(struct sock *, union matt_sockaddr *, union matt_sockaddr *);
	int (*get_sk_addr)(struct socket *, struct sockaddr *, int);
	void (*set_sk_addr)(struct sock *, union matt_sockaddr *, bool);
	int (*getsockopt)(struct sock *, int, int, void *, void *);
	int (*setsockopt)(struct sock *, int, int, sockptr_t, unsigned int);
};
struct matt_report {
	struct matthdr hdr;
	__be32 rtt;
	__be32 limit;
	__be32 cwnd;
};
struct matt_request_sock {
	struct list_head list;
	union matt_sockaddr da;
	union matt_sockaddr sa;
	u32 rcv_isn;
	u32 rcv_num;
	u32 snd_wnd;
	u32 version;
	u16 rcv_mss;
};
struct matt_rst {
	struct matthdr hdr;
	__be32 reasoncode;
};
struct matt_sack_block_wire {
	__be32 offset_start;
	__be32 offset_end;
};
struct matt_sacktag_state {
	u64 first_sackt;
	u64 last_sackt;
	u32 reord;
	u32 sack_delivered;
	int flag;
	unsigned int mss_now;
	struct rate_sample *rate;
};
struct matt_shash_head {
	struct hlist_nulls_head head;
	spinlock_t lock;
};
struct matt_skb_cb {
	union {
		struct {
			__u32 is_app_limited : 1;
			__u32 delivered_ce : 20;
			__u32 unused : 11;
			__u32 delivered;
			u32 first_tx_mstamp;
			u32 delivered_mstamp;
			u32 last_tx_mstamp;
		} tx;
		struct {
			u32 peer_tx_mstamp;
			u32 local_rx_mstamp;
		} rx;
	};
	u32 offset_start;
	u32 offset_end;
	u32 num;
	u16 gso_segs;
	u16 gso_size;
	u8 flags;
	u8 types;
	u8 sacked;
	u16 udph_offset;
};
struct matt_udp_sock {
	struct work_struct work;
	struct hlist_node node;
	struct sock *sk;
	union matt_sockaddr addr;
	refcount_t refcnt;
};
struct matt_uhash_head {
	struct hlist_head head;
	struct mutex lock;
};
struct matt_ulp_ops {
	struct list_head list;
	int (*init)(struct sock *);
	void (*update)(struct sock *, struct proto *, void (*)(struct sock *));
	void (*release)(struct sock *);
	int (*get_info)(struct sock *, struct sk_buff *);
	size_t (*get_info_size)(const struct sock *);
	void (*clone)(const struct matt_request_sock *, struct sock *, const gfp_t);
	char name[16];
	struct module *owner;
};
struct msq_tasklet {
	struct tasklet_struct tasklet;
	struct list_head head;
};
struct symbol_entry {
	const char *name;
	unsigned long addr;
	void **target;
};

#define MATT_SK(sk) ((struct matt_sock *)(sk))
#define MATT_SKB_CB(skb) ((struct matt_skb_cb *)&((skb)->cb[0]))

#define MATT_SIZE_ASSERT(t, n) _Static_assert(sizeof(struct t) == (n), "size of " #t)
MATT_SIZE_ASSERT(matt_sock, 5072);
MATT_SIZE_ASSERT(matt_input_ctx, 1496);
MATT_SIZE_ASSERT(matt_output_ctx, 1528);
MATT_SIZE_ASSERT(matt_mgmt, 128);
MATT_SIZE_ASSERT(matt_timer_ctx, 448);
MATT_SIZE_ASSERT(matt_request_sock, 96);
MATT_SIZE_ASSERT(matt_skb_cb, 44);
MATT_SIZE_ASSERT(matt_cc_ctx, 120);
MATT_SIZE_ASSERT(matt_udp_sock, 104);
MATT_SIZE_ASSERT(matt_uhash_head, 56);
MATT_SIZE_ASSERT(matt_creq, 32);
MATT_SIZE_ASSERT(matt_crsp, 36);
MATT_SIZE_ASSERT(matt_pack, 52);
MATT_SIZE_ASSERT(matt6_sock, 5224);

#include "matt_decl.h"

#endif
