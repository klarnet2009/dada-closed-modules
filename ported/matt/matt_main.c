// SPDX-License-Identifier: GPL-2.0
/*
 * matt: Xiaomi Adaptive Transfer Tunnel (TCP-like transport + BBR + GRO), OS4 stock vendor_dlkm/matt.ko.
 * Reconstructed from the stock disassembly (no public source). PARTIAL (small helpers only), NOT hardware-tested: DO NOT LOAD.
 * Author of the stock: lihongyan5@xiaomi.com, version 20241015.
 */
#include "matt.h"

/*
 * hash tables: the stock keeps six scalar objects (clang split a static struct): listen/established hash pointers are
 * ThinLTO-promoted (GLOBAL HIDDEN name.llvm.<hash>), the rest stay local.
 */
#define LLVMH	".llvm.3228964515555043929"
struct matt_shash_head *matt_listen_hash __asm__("matt_hashinfo.0" LLVMH) __attribute__((visibility("hidden")));
static u32 matt_listen_hash_size __asm__("matt_hashinfo.1") __used;
struct matt_shash_head *matt_sock_hashp __asm__("matt_hashinfo.2" LLVMH) __attribute__((visibility("hidden")));
static u32 matt_sock_hash_sz __asm__("matt_hashinfo.3") __used;
static struct matt_uhash_head *matt_udp_hash __asm__("matt_hashinfo.4") __used;
static u32 matt_udp_hash_sz __asm__("matt_hashinfo.5") __used;

/* kernel symbols the stock resolves through a kprobe-kallsyms lookup (kCFI-checked indirect calls) */
int (*xm_inet_release)(struct socket *sock);
int (*xm_inet_bind)(struct socket *sock, struct sockaddr *uaddr, int addr_len);
int (*xm_inet6_bind)(struct socket *sock, struct sockaddr *uaddr, int addr_len);

static unsigned long matt_memory_pressure __used;



u32 matt_sock_hash_size(void)
{
	return matt_sock_hash_sz;
}

struct matt_shash_head *matt_sock_head(u32 hash)
{
	return &matt_sock_hashp[hash];
}

u32 matt_listen_sock_hash_size(void)
{
	return matt_listen_hash_size;
}

struct matt_shash_head *matt_listen_sock_head(u32 hash)
{
	return &matt_listen_hash[hash];
}

noinline struct matt_uhash_head *matt_udp_sock_head(struct net *net, u16 port)
{
	u32 h = jhash_2words(port, net_hash_mix(net), 0);

	return &matt_udp_hash[h & (matt_udp_hash_sz - 1)];
}

void matt_hash_tables_destroy(void)
{
	vfree(matt_listen_hash);
	vfree(matt_sock_hashp);
	vfree(matt_udp_hash);
}

int matt_inet_release(struct socket *sock)
{
	return xm_inet_release(sock);
}

int matt_inet6_release(struct socket *sock)
{
	return xm_inet_release(sock);
}

int matt_inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	return xm_inet_bind(sock, uaddr, addr_len);
}

int matt_inet6_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	return xm_inet6_bind(sock, uaddr, addr_len);
}

int matt_inet_getname(struct socket *sock, struct sockaddr *uaddr, int peer)
{
	return matt_get_sk_addr2(sock, uaddr, peer);
}

int matt_inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static void matt_enter_memory_pressure(struct sock *sk)
{
	matt_memory_pressure = 1;
}

void matt_mark_push(struct matt_output_ctx *outq)
{
	outq->pushed_seq = outq->write_seq;
}

int matt_current_mss(struct sock *sk)
{
	return MATT_SK(sk)->mss;
}

bool matt_invalid_packet_check(struct sk_buff *skb)
{
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);

	if ((s8)cb->flags < 0)
		return false;
	return cb->offset_start == cb->offset_end;
}

/* the stock call site passes only (sk, msg): documented hack, the callee is declared with 3 parameters in matt.h */
extern int matt_sendmsg_locked(struct sock *sk, struct msghdr *msg);

int matt_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	int ret;

	lock_sock(sk);
	ret = matt_sendmsg_locked(sk, msg);
	release_sock(sk);
	return ret;
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("lihongyan5@xiaomi.com");
MODULE_DESCRIPTION("XiaoMi Adaptive Tansfer Tunnel");
MODULE_VERSION("20241015");

/* ---- hash tables (stock: mp_hash.c) ---- */
int matt_hash_tables_init(void)
{
	unsigned long n = totalram_pages();
	u32 size;
	u32 i;

	n >>= (n >> 17) ? 10 : 12;
	size = min_t(u32, roundup_pow_of_two((u32)n), 0x8000);
	matt_listen_hash = vmalloc(size * sizeof(struct matt_shash_head));
	if (!matt_listen_hash)
		goto fail;
	matt_listen_hash_size = size;
	for (i = 0; i < size; i++) {
		matt_listen_hash[i].lock = __SPIN_LOCK_UNLOCKED(matt_listen_hash[i].lock);
		INIT_HLIST_NULLS_HEAD(&matt_listen_hash[i].head, i);
	}
	matt_sock_hashp = vmalloc(size * sizeof(struct matt_shash_head));
	if (!matt_sock_hashp)
		goto fail;
	matt_sock_hash_sz = size;
	for (i = 0; i < size; i++) {
		matt_sock_hashp[i].lock = __SPIN_LOCK_UNLOCKED(matt_sock_hashp[i].lock);
		INIT_HLIST_NULLS_HEAD(&matt_sock_hashp[i].head, i);
	}
	matt_udp_hash = vmalloc(size * sizeof(struct matt_uhash_head));
	if (!matt_udp_hash)
		goto fail;
	matt_udp_hash_sz = size;
	for (i = 0; i < size; i++) {
		mutex_init(&matt_udp_hash[i].lock);
		INIT_HLIST_HEAD(&matt_udp_hash[i].head);
	}
	return 0;
fail:
	vfree(matt_listen_hash);
	vfree(matt_sock_hashp);
	vfree(matt_udp_hash);
#line 307
	MATT_TRACE(0, 2, "ht init fail!");
	return -ENOMEM;
}

#include "matt_tx.h"
#include "matt_sock.h"
#include "matt_init.h"
