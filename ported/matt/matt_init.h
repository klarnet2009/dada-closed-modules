// SPDX-License-Identifier: GPL-2.0
/* included from matt_main.c (shares the static objects) */
/* matt module init/exit, kernel-symbol table (kprobe kallsyms trick), sysctl table, proto/protosw/proto_ops tables.
 * Reconstructed from the OS4 stock disassembly. NOT hardware-tested: DO NOT LOAD before review. */
#include <linux/kprobes.h>
#include <linux/sysctl.h>
#include <linux/workqueue.h>
#include <linux/percpu_counter.h>
#include <net/protocol.h>
#include <net/inet_common.h>
#include <net/ipv6.h>

/* ---- kernel symbols resolved at init through kallsyms_lookup_name (kCFI-checked indirect calls) ---- */
unsigned long (*xm_kallsyms_lookup_name)(const char *name);
void (*xm_inet_register_protosw)(struct inet_protosw *p);
int (*xm_inet6_register_protosw)(struct inet_protosw *p);	/* G1ad: int in the kernel; the stock kCFI typeid is 0x5f86e117, G1ac had void [0xb94f8014]: a kCFI trap at module init */
unsigned long (*xm_nr_free_buffer_pages)(void);
void (*xm_inet_unregister_protosw)(struct inet_protosw *p);
void (*xm_inet6_unregister_protosw)(struct inet_protosw *p);
/* xm_sk_dst_check: matt_inet4.h; xm_sk_mem_schedule: matt_misc2.c */
bool (*xm_sk_page_frag_refill)(struct sock *sk, struct page_frag *pfrag);
size_t (*xm_copy_from_iter_nocache)(void *addr, size_t bytes, struct iov_iter *i);
int (*xm_sk_wait_data)(struct sock *sk, long *timeo, const struct sk_buff *skb);
bool (*xm_sk_busy_loop_end)(void *p, unsigned long start_time);
void (*xm_napi_busy_loop)(unsigned int napi_id, bool (*loop_end)(void *, unsigned long), void *loop_end_arg, bool prefer_busy_poll, u16 budget);
void (*xm_reuseport_detach_sock)(struct sock *sk);
u32 (*xm_minmax_running_max)(struct minmax *m, u32 win, u32 t, u32 meas);
int (*xm_inet_send_prepare)(struct sock *sk);
struct sock *(*xm_reuseport_select_sock)(struct sock *sk, u32 hash, struct sk_buff *skb, int hdr_len);
int (*xm_reuseport_alloc)(struct sock *sk, bool bind_inany);
int (*xm_reuseport_add_sock)(struct sock *sk, struct sock *sk2, bool bind_inany);
int (*xm_smp_call_function_single_async)(int cpu, struct __call_single_data *csd);
/* xm__lock_sock_fast: matt_misc2.c */
int (*xm_inet_add_offload)(const struct net_offload *prot, unsigned char protocol);
int (*xm_inet_del_offload)(const struct net_offload *prot, unsigned char protocol);
int (*xm_inet6_add_offload)(const struct net_offload *prot, unsigned char protocol);
int (*xm_inet6_del_offload)(const struct net_offload *prot, unsigned char protocol);
struct sk_buff *(*xm_skb_segment)(struct sk_buff *head_skb, netdev_features_t features);
void (*xm_skb_split)(struct sk_buff *skb, struct sk_buff *skb1, const u32 len);
int (*xm_pskb_expand_head)(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp_mask);
bool (*xm_napi_pp_put_page)(struct page *page, bool napi_safe);
void (*xm_skb_condense)(struct sk_buff *skb);
/* xm_inet_bind / xm_inet6_bind / xm_inet_release: matt_main.c */
int (*xm_inet6_release)(struct socket *sock);
int (*xm_inet6_getname)(struct socket *sock, struct sockaddr *uaddr, int peer);
void *xm_ipv6_flowlabel_exclusive;
/* xm__fl6_sock_lookup / xm_ip6_dst_lookup_flow / xm__get_hash_from_flowi6: matt_inet6.h */
void (*xm_sk_stream_kill_queues)(struct sock *sk);
void (*xm_sockopt_lock_sock)(struct sock *sk);
void (*xm_sockopt_release_sock)(struct sock *sk);

static struct symbol_entry symbol_table[SYM_COUNT] = {
	[SYM_INET_REGISTER_PROTOSW] = { "inet_register_protosw", 0, (void **)&xm_inet_register_protosw },
	[SYM_INET6_REGISTER_PROTOSW] = { "inet6_register_protosw", 0, (void **)&xm_inet6_register_protosw },
	[SYM_NR_FREE_BUFFER_PAGES] = { "nr_free_buffer_pages", 0, (void **)&xm_nr_free_buffer_pages },
	/* the stock names the v6 symbol here too (copy-paste in the original source): kept */
	[SYM_INET_UNREGISTER_PROTOSW] = { "inet6_unregister_protosw", 0, (void **)&xm_inet_unregister_protosw },
	[SYM_INET6_UNREGISTER_PROTOSW] = { "inet6_unregister_protosw", 0, (void **)&xm_inet6_unregister_protosw },
	[SYM___SK_DST_CHECK] = { "__sk_dst_check", 0, (void **)&xm_sk_dst_check },
	[SYM___SK_MEM_SCHEDULE] = { "__sk_mem_schedule", 0, (void **)&xm_sk_mem_schedule },
	[SYM_SK_PAGE_FRAG_REFILL] = { "sk_page_frag_refill", 0, (void **)&xm_sk_page_frag_refill },
	[SYM__COPY_FROM_ITER_NOCACHE] = { "_copy_from_iter_nocache", 0, (void **)&xm_copy_from_iter_nocache },
	[SYM_SK_WAIT_DATA] = { "sk_wait_data", 0, (void **)&xm_sk_wait_data },
	[SYM_SK_BUSY_LOOP_END] = { "sk_busy_loop_end", 0, (void **)&xm_sk_busy_loop_end },
	[SYM_NAPI_BUSY_LOOP] = { "napi_busy_loop", 0, (void **)&xm_napi_busy_loop },
	[SYM_REUSEPORT_DETACH_SOCK] = { "reuseport_detach_sock", 0, (void **)&xm_reuseport_detach_sock },
	[SYM_MINMAX_RUNNING_MAX] = { "minmax_running_max", 0, (void **)&xm_minmax_running_max },
	[SYM_INET_SEND_PREPARE] = { "inet_send_prepare", 0, (void **)&xm_inet_send_prepare },
	[SYM_REUSEPORT_SELECT_SOCK] = { "reuseport_select_sock", 0, (void **)&xm_reuseport_select_sock },
	[SYM_REUSEPORT_ALLOC] = { "reuseport_alloc", 0, (void **)&xm_reuseport_alloc },
	[SYM_REUSEPORT_ADD_SOCK] = { "reuseport_add_sock", 0, (void **)&xm_reuseport_add_sock },
	[SYM_SMP_CALL_FUNC_SA] = { "smp_call_function_single_async", 0, (void **)&xm_smp_call_function_single_async },
	[SYM__LOCK_SOCK_FAST] = { "__lock_sock_fast", 0, (void **)&xm__lock_sock_fast },
	[SYM_INET_ADD_OFFLOAD] = { "inet_add_offload", 0, (void **)&xm_inet_add_offload },
	[SYM_INET_DEL_OFFLOAD] = { "inet_del_offload", 0, (void **)&xm_inet_del_offload },
	[SYM_INET6_ADD_OFFLOAD] = { "inet6_add_offload", 0, (void **)&xm_inet6_add_offload },
	[SYM_INET6_DEL_OFFLOAD] = { "inet6_del_offload", 0, (void **)&xm_inet6_del_offload },
	[SYM_SKB_SEGMENT] = { "skb_segment", 0, (void **)&xm_skb_segment },
	[SYM_SKB_SPLIT] = { "skb_split", 0, (void **)&xm_skb_split },
	[SYM_SKB_UNCLONE_KT] = { "pskb_expand_head", 0, (void **)&xm_pskb_expand_head },
	[SYM_SKB_NAPI_PP] = { "napi_pp_put_page", 0, (void **)&xm_napi_pp_put_page },
	[SYM_SKB_CONDENSE] = { "skb_condense", 0, (void **)&xm_skb_condense },
	[SYM_INET_BIND] = { "inet_bind", 0, (void **)&xm_inet_bind },
	[SYM_INET6_BIND] = { "inet6_bind", 0, (void **)&xm_inet6_bind },
	[SYM_INET_RELEASE] = { "inet_release", 0, (void **)&xm_inet_release },
	[SYM_INET6_RELEASE] = { "inet6_release", 0, (void **)&xm_inet6_release },
	[SYM_INET6_GETNAME] = { "inet6_getname", 0, (void **)&xm_inet6_getname },
	[SYM_INET6_FL_EX] = { "ipv6_flowlabel_exclusive", 0, (void **)&xm_ipv6_flowlabel_exclusive },
	[SYM_INET6_FL_SK_LOOKUP] = { "__fl6_sock_lookup", 0, (void **)&xm__fl6_sock_lookup },
	[SYM_INET6_DST_LOOKUP_FLOW] = { "ip6_dst_lookup_flow", 0, (void **)&xm_ip6_dst_lookup_flow },
	[SYM_INET6_GET_HASH_FI6] = { "__get_hash_from_flowi6", 0, (void **)&xm__get_hash_from_flowi6 },
	[SYM_STREAM_KILL] = { "sk_stream_kill_queues", 0, (void **)&xm_sk_stream_kill_queues },
	[SYM_SOCKOPT_LOCK] = { "sockopt_lock_sock", 0, (void **)&xm_sockopt_lock_sock },
	[SYM_SOCKOPT_RELEASE] = { "sockopt_release_sock", 0, (void **)&xm_sockopt_release_sock },
};

static int noop_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	return 0;
}

static struct kprobe kprobe_kallsyms_lookup_name = {
	.symbol_name = "kallsyms_lookup_name",
};

static int xiaomi_get_opt_symbol(struct symbol_entry *se)
{
	se->addr = xm_kallsyms_lookup_name(se->name);
	if (!se->addr) {
		printk(KERN_ERR "%s: symbol %s not found\n", __func__, se->name);
		return -ENOENT;
	}
	*se->target = (void *)se->addr;
#line 350
	MATT_TRACE(0, 9, "Symbol %s => %p\n", se->name, (void *)se->addr);
	return 0;
}

static int xiaomi_init_symbol(void)
{
	int i, ret;

	kprobe_kallsyms_lookup_name.pre_handler = noop_pre_handler;
	ret = register_kprobe(&kprobe_kallsyms_lookup_name);
	if (ret < 0) {
		printk(KERN_ERR "%s: register_kprobe is invalid\n", __func__);
		return -EINVAL;
	}
	xm_kallsyms_lookup_name = (void *)kprobe_kallsyms_lookup_name.addr;
	unregister_kprobe(&kprobe_kallsyms_lookup_name);
	if (!xm_kallsyms_lookup_name) {
		printk(KERN_ERR "%s: xm_kallsyms_lookup_name is invalid\n", __func__);
		return -EINVAL;
	}
	for (i = 0; i < SYM_COUNT; i++) {
		ret = xiaomi_get_opt_symbol(&symbol_table[i]);
		if (ret)
			return ret;
	}
	return 0;
}

/* ---- sysctl ---- */
long sysctl_matt_mem[3];
int sysctl_matt_rmem[3];
int sysctl_matt_wmem[3];
extern int sysctl_matt_debug, sysctl_matt_enable, sysctl_inet_enable, sysctl_mem_limited, sysctl_limit_output, sysctl_defer_max;
extern int bbr_enable_opt, bbr_enable_debug, bbr_srtt_factor, bbr_mrtt_factor, bbr_pace_factor, bbr_min_rtt_win_factor,
	bbr_probe_rtt_cwnd_factor, bbr_no_pacing, bbr_init_cwnd_opt, bbr_avoid_burst, bbr_cwnd_limit_factor, bbr_cap_rtt_factor;

#define MATT_CTL(n, d, ml, h) { .procname = n, .data = &d, .maxlen = ml, .mode = 0644, .proc_handler = h }
#define MATT_INT(n, d) MATT_CTL(n, d, sizeof(int), proc_dointvec)
static struct ctl_table matt_table[] = {
	MATT_CTL("matt_mem", sysctl_matt_mem, sizeof(sysctl_matt_mem), proc_doulongvec_minmax),
	MATT_CTL("matt_rmem", sysctl_matt_rmem, sizeof(sysctl_matt_rmem), proc_dointvec),
	MATT_CTL("matt_wmem", sysctl_matt_wmem, sizeof(sysctl_matt_wmem), proc_dointvec),
	MATT_INT("debug", sysctl_matt_debug),
	MATT_INT("enable", sysctl_matt_enable),
	MATT_INT("inet_enable", sysctl_inet_enable),
	MATT_INT("mem_limited", sysctl_mem_limited),
	MATT_INT("limit_output_bytes", sysctl_limit_output),
	MATT_INT("defer_max", sysctl_defer_max),
	MATT_INT("bbr_enable_opt", bbr_enable_opt),
	MATT_INT("bbr_enable_debug", bbr_enable_debug),
	MATT_INT("bbr_srtt_factor", bbr_srtt_factor),
	MATT_INT("bbr_mrtt_factor", bbr_mrtt_factor),
	MATT_INT("bbr_pace_factor", bbr_pace_factor),
	MATT_INT("bbr_min_rtt_win_factor", bbr_min_rtt_win_factor),
	MATT_INT("bbr_probe_rtt_cwnd_factor", bbr_probe_rtt_cwnd_factor),
	MATT_INT("bbr_no_pacing", bbr_no_pacing),
	MATT_INT("bbr_init_cwnd_opt", bbr_init_cwnd_opt),
	MATT_INT("bbr_avoid_burst", bbr_avoid_burst),
	MATT_INT("bbr_cwnd_limit_factor", bbr_cwnd_limit_factor),
	MATT_INT("bbr_cap_rtt_factor", bbr_cap_rtt_factor),
	{}
};
static struct ctl_table_header *matt_sysctl_header;

static noinline void matt_sysctl_register(void)
{
	unsigned long n = xm_nr_free_buffer_pages() >> 4;

	n = max(n, 128UL);
	sysctl_matt_mem[0] = (n >> 2) * 3;
	sysctl_matt_mem[1] = n;
	sysctl_matt_mem[2] = sysctl_matt_mem[0] * 2;
#line 1082
	MATT_TRACE(0, 9, "total mem:%d, %d, %d", (int)sysctl_matt_mem[0], (int)n, (int)sysctl_matt_mem[2]);
	xm_nr_free_buffer_pages();
	sysctl_matt_rmem[0] = 4096;
	sysctl_matt_rmem[1] = 16384;
	sysctl_matt_rmem[2] = 0x2000000;
	sysctl_matt_wmem[0] = 4096;
	sysctl_matt_wmem[1] = 32768;
	sysctl_matt_wmem[2] = 0x2000000;
#line 1101
	MATT_TRACE(0, 9, "rmem:%d, %d, %d", 4096, 16384, 0x2000000);
#line 1103
	MATT_TRACE(0, 9, "wmem:%d, %d, %d", sysctl_matt_wmem[0], sysctl_matt_wmem[1], sysctl_matt_wmem[2]);
	matt_sysctl_header = register_net_sysctl_sz(&init_net, "net/matt", matt_table, ARRAY_SIZE(matt_table));
}

/* ---- offloads (gso/gro of the encapsulation) ---- */
struct sk_buff *matt_gso_segment(struct sk_buff *skb, netdev_features_t features) __asm__("matt_gso_segment.llvm.11802411537102813144") __attribute__((visibility("hidden")));

#define LLVMO	".llvm.11802411537102813144"
static const struct net_offload matt_offload __asm__("matt_offload" LLVMO) = {
	.callbacks = { .gso_segment = matt_gso_segment },
};
static const struct net_offload matt6_offload __asm__("matt6_offload" LLVMO) = {
	.callbacks = { .gso_segment = matt_gso_segment },
};

int matt_offload_init(void)
{
	return xm_inet_add_offload(&matt_offload, IPPROTO_MATT);
}

int mattv6_offload_init(void)
{
	return xm_inet6_add_offload(&matt6_offload, IPPROTO_MATT);
}

void matt_offload_del(void)
{
	xm_inet_del_offload(&matt_offload, IPPROTO_MATT);
}

void mattv6_offload_del(void)
{
	xm_inet6_del_offload(&matt6_offload, IPPROTO_MATT);
}

/* ---- proto / protosw / proto_ops ---- */
void matt_close(struct sock *sk, long timeout);
int matt_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len);
int matt_disconnect(struct sock *sk, int flags);
struct sock *matt_accept(struct sock *sk, int flags, int *err, bool kern);
int matt_init_sock(struct sock *sk);
void matt_destroy_sock(struct sock *sk);
void matt_shutdown(struct sock *sk, int how);
int matt_setsockopt(struct sock *sk, int level, int optname, sockptr_t optval, unsigned int optlen);
int matt_getsockopt(struct sock *sk, int level, int optname, char __user *optval, int __user *option);
int matt_sendmsg(struct sock *sk, struct msghdr *msg, size_t len);
int matt_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, int flags, int *addr_len);
int matt_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len);
int matt_do_recv(struct sock *sk, struct sk_buff *skb);
void matt_release_cb(struct sock *sk);
int matt_hash(struct sock *sk);
void matt_unhash(struct sock *sk);
void matt_enter_memory_pressure(struct sock *sk);

static atomic_long_t matt_memory_allocated;
struct percpu_counter matt_sockets_allocated;
struct workqueue_struct *matt_wq __read_mostly;

#define MATT_PROTO_COMMON \
	.close = matt_close, .connect = matt_connect, .disconnect = matt_disconnect, .accept = matt_accept, \
	.init = matt_init_sock, .destroy = matt_destroy_sock, .shutdown = matt_shutdown, .setsockopt = matt_setsockopt, \
	.getsockopt = matt_getsockopt, .sendmsg = matt_sendmsg, .recvmsg = matt_recvmsg, .bind = matt_bind, \
	.backlog_rcv = matt_do_recv, .release_cb = matt_release_cb, .hash = matt_hash, .unhash = matt_unhash, \
	.enter_memory_pressure = matt_enter_memory_pressure, .memory_allocated = &matt_memory_allocated, \
	.sockets_allocated = &matt_sockets_allocated, .memory_pressure = &matt_memory_pressure, \
	.sysctl_mem = sysctl_matt_mem, .sysctl_wmem = sysctl_matt_wmem, .sysctl_rmem = sysctl_matt_rmem, \
	.orphan_count = &matt_orphan_count, .no_autobind = true, .owner = THIS_MODULE

struct proto matt_prot = { MATT_PROTO_COMMON, .obj_size = sizeof(struct matt_sock), .name = "MATT" };
struct proto matt6_prot = { MATT_PROTO_COMMON, .obj_size = sizeof(struct matt6_sock), .name = "MATTv6" };

int matt_inet_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags);
unsigned int matt_inet_poll(struct file *file, struct socket *sock, poll_table *wait);
int matt_inet_listen(struct socket *sock, int backlog);
int matt_inet_accept(struct socket *sock, struct socket *newsock, int flags, bool kern);
int matt_inet_shutdown(struct socket *sock, int how);
int matt_inet_sendmsg(struct socket *sock, struct msghdr *msg, size_t size);
int matt_inet_recvmsg(struct socket *sock, struct msghdr *msg, size_t size, int flags);
int matt_inet_release(struct socket *sock);
int matt_inet6_release(struct socket *sock);
int matt_inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len);
int matt_inet6_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len);
int matt_inet_getname(struct socket *sock, struct sockaddr *uaddr, int peer);
int matt_inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);

#define MATT_OPS_COMMON \
	.owner = THIS_MODULE, .socketpair = sock_no_socketpair, .accept = matt_inet_accept, .connect = matt_inet_connect, \
	.getname = matt_inet_getname, .poll = (void *)matt_inet_poll, .ioctl = matt_inet_ioctl, .gettstamp = sock_gettstamp, \
	.listen = matt_inet_listen, .shutdown = matt_inet_shutdown, .setsockopt = sock_common_setsockopt, \
	.getsockopt = sock_common_getsockopt, .sendmsg = matt_inet_sendmsg, .recvmsg = matt_inet_recvmsg, .mmap = sock_no_mmap

static const struct proto_ops matt_proto_ops = { .family = PF_INET, .release = matt_inet_release, .bind = matt_inet_bind, MATT_OPS_COMMON };
static const struct proto_ops matt6_proto_ops = { .family = PF_INET6, .release = matt_inet6_release, .bind = matt_inet6_bind, MATT_OPS_COMMON };

static struct inet_protosw matt_protosw = { .type = SOCK_STREAM, .protocol = IPPROTO_MATT, .prot = &matt_prot, .ops = &matt_proto_ops };
static struct inet_protosw matt6_protosw = { .type = SOCK_STREAM, .protocol = IPPROTO_MATT, .prot = &matt6_prot, .ops = &matt6_proto_ops };

static int matt_proto_init(void)
{
	int ret;

	matt_prot.per_cpu_fw_alloc = &matt_memory_per_cpu_fw_alloc;
	ret = proto_register(&matt_prot, 1);
	if (ret) {
		printk(KERN_ERR "%s: invalid proto\n", __func__);
		return ret;
	}
	matt6_prot.per_cpu_fw_alloc = &matt_memory_per_cpu_fw_alloc;
	matt6_prot.ipv6_pinfo_offset = sizeof(struct matt_sock);
	ret = proto_register(&matt6_prot, 1);
	if (ret) {
		proto_unregister(&matt_prot);
		printk(KERN_ERR "%s: invalid proto6\n", __func__);
		return ret;
	}
	xm_inet_register_protosw(&matt_protosw);
	xm_inet6_register_protosw(&matt6_protosw);
	ret = matt_offload_init();
	if (ret) {
		xm_inet6_unregister_protosw(&matt6_protosw);
		xm_inet_unregister_protosw(&matt_protosw);
		proto_unregister(&matt6_prot);
		proto_unregister(&matt_prot);
		printk(KERN_ERR "%s: invalid offload\n", __func__);
		return ret;
	}
	ret = mattv6_offload_init();
	if (ret) {
		xm_inet6_unregister_protosw(&matt6_protosw);
		xm_inet_unregister_protosw(&matt_protosw);
		proto_unregister(&matt6_prot);
		proto_unregister(&matt_prot);
		printk(KERN_ERR "%s: invalid offload6\n", __func__);
		return ret;
	}
	return 0;
}

static int __init matt_init(void)
{
	int ret;

	ret = xiaomi_init_symbol();
	if (ret)
		return ret;
	ret = matt_hash_tables_init();
	if (ret)
		return ret;
	matt_wq = create_workqueue("matt_wq");
	if (!matt_wq) {
		matt_hash_tables_destroy();
		return 0;
	}
	ret = percpu_counter_init(&matt_sockets_allocated, 0, GFP_KERNEL);
	if (ret) {
		destroy_workqueue(matt_wq);
		matt_hash_tables_destroy();
		return ret;
	}
	ret = matt_proto_init();
	if (ret) {
		percpu_counter_destroy(&matt_sockets_allocated);
		destroy_workqueue(matt_wq);
		matt_hash_tables_destroy();
		return ret;
	}
	matt_sysctl_register();
	matt_tasklet_init();
	return 0;
}

static void __exit matt_exit(void)
{
	unregister_net_sysctl_table(matt_sysctl_header);
	matt_offload_del();
	mattv6_offload_del();
	xm_inet6_unregister_protosw(&matt6_protosw);
	proto_unregister(&matt6_prot);
	xm_inet_unregister_protosw(&matt_protosw);
	proto_unregister(&matt_prot);
	percpu_counter_destroy(&matt_sockets_allocated);
	destroy_workqueue(matt_wq);
	matt_hash_tables_destroy();
}

module_init(matt_init);
module_exit(matt_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("lihongyan5@xiaomi.com");
MODULE_DESCRIPTION("XiaoMi Adaptive Tansfer Tunnel");
MODULE_VERSION("20241015");
