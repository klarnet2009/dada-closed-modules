// SPDX-License-Identifier: Dual BSD/GPL
/* Reconstructed from the stock mibbr.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#include <linux/module.h>
#include <linux/btf.h>
#include <net/tcp.h>
#include <linux/win_minmax.h>
#include <linux/proc_fs.h>
#include <linux/kprobes.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/net_namespace.h>
#include "mibbr_internal.h"

/* kernel functions that are resolved at load time through kallsyms_lookup_name() (see symbol_table) */
void (*xm_tcp_cleanup_congestion_control)(struct sock *sk);
void (*xm_tcp_init_congestion_control)(struct sock *sk);
u32 (*xm_minmax_running_max)(struct minmax *m, u32 win, u32 t, u32 meas);
int (*xm_register_btf_kfunc_id_set)(enum bpf_prog_type prog_type, const struct btf_kfunc_id_set *s);
unsigned long (*xm_kallsyms_lookup_name)(const char *name);

/* ---- netfilter hooks: switch the sockets of white-listed apps to mibbr ---- */

static void check_and_set_mibbr(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	int ret;

	if (sk->sk_state != TCP_ESTABLISHED && sk->sk_state != TCP_SYN_SENT)
		return;
	if (!is_mibbr_white_app(sk->sk_uid.val))
		return;
	if (!icsk->icsk_ca_ops || !memcmp(icsk->icsk_ca_ops->name, "mibbr", 5))
		return;

	printk("mibbr:to first set mibbr,uid=%d,sk=%p\n", sk->sk_uid.val, sk);
	ret = xm_set_mibbr(sk);
	if (ret)
		printk("mibbr:set cc fail,uid=%d,sk=%p, ret=%d\n", sk->sk_uid.val, sk, ret);
	else
		printk("mibbr:set cc success,uid=%d,sk=%p\n", sk->sk_uid.val, sk);
}

static unsigned int minet_ext_output_hook4(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct iphdr *iph = ip_hdr(skb);
	struct sock *sk = skb->sk;

	if (iph && iph->protocol == IPPROTO_TCP && sk)
		check_and_set_mibbr(sk);
	return NF_ACCEPT;
}

static unsigned int minet_ext_output_hook6(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct sock *sk = skb->sk;

	if (ip6h && ip6h->nexthdr == IPPROTO_TCP && sk)
		check_and_set_mibbr(sk);
	return NF_ACCEPT;
}

static struct nf_hook_ops minet_ext_ops[] __read_mostly = {
	{
		.hook		= minet_ext_output_hook4,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= 1,
	},
	{
		.hook		= minet_ext_output_hook6,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= 1,
	},
};

/* ---- resolving unexported kernel symbols (kallsyms_lookup_name via a kprobe) ---- */

static int noop_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	return 0;
}

static struct kprobe kprobe_kallsyms_lookup_name = {
	.symbol_name = "kallsyms_lookup_name",
};

struct xm_symbol {
	const char *name;
	unsigned long addr;
	unsigned long *var;
};

struct xm_symbol symbol_table[] = {
	{ "register_btf_kfunc_id_set", 0, (unsigned long *)&xm_register_btf_kfunc_id_set },
	{ "minmax_running_max", 0, (unsigned long *)&xm_minmax_running_max },
	{ "tcp_cleanup_congestion_control", 0, (unsigned long *)&xm_tcp_cleanup_congestion_control },
	{ "tcp_init_congestion_control", 0, (unsigned long *)&xm_tcp_init_congestion_control },
};

static int xiaomi_init_symbol(void)
{
	int ret;

	kprobe_kallsyms_lookup_name.pre_handler = noop_pre_handler;
	ret = register_kprobe(&kprobe_kallsyms_lookup_name);
	if (ret < 0) {
		printk("mibbr:symbol register_kprobe failed!\n");
		return -EINVAL;
	}
	printk("symbol register_kprobe successfully!\n");
	xm_kallsyms_lookup_name = (void *)kprobe_kallsyms_lookup_name.addr;
	unregister_kprobe(&kprobe_kallsyms_lookup_name);
	printk("mibbr:xm_kallsyms_lookup_name is %p\n", xm_kallsyms_lookup_name);
	if (!xm_kallsyms_lookup_name) {
		printk("mibbr:xm_kallsyms_lookup_name get failed!\n");
		return -EINVAL;
	}
	return 0;
}

static int xiaomi_get_opt_symbol(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(symbol_table); i++) {
		symbol_table[i].addr = xm_kallsyms_lookup_name(symbol_table[i].name);
		if (!symbol_table[i].addr) {
			printk("mibbr:Symbol %s not found\n", symbol_table[i].name);
			return -ENOENT;
		}
		*symbol_table[i].var = symbol_table[i].addr;
		printk("mibbr:Symbol %s => %p\n", symbol_table[i].name, (void *)symbol_table[i].addr);
	}
	return 0;
}

static int __init minet_ext_init(void)
{
	int ret;

	if (xiaomi_init_symbol()) {
		printk("mi_bbr:xiaomi_init_symbol fail!");
		return -EINVAL;
	}
	ret = xiaomi_get_opt_symbol();
	if (ret) {
		printk("xiaomi_get_opt_symbol fail!");
		return ret;
	}
	ret = bbr_config_init();
	if (ret < 0) {
		printk("mi_bbr:init_bbr_config_file fail!");
		return ret;
	}
	ret = nf_register_net_hooks(&init_net, minet_ext_ops, ARRAY_SIZE(minet_ext_ops));
	if (ret < 0) {
		printk("nf_register_net_hooks fail:ret=%d\n", ret);
		goto err;
	}
	ret = mibbr_register();
	if (ret < 0) {
		nf_unregister_net_hooks(&init_net, minet_ext_ops, ARRAY_SIZE(minet_ext_ops));
		goto err;
	}
	return ret;
err:
	proc_remove(bbr_dir);
	return ret;
}

static void __exit minet_ext_exit(void)
{
	nf_unregister_net_hooks(&init_net, minet_ext_ops, ARRAY_SIZE(minet_ext_ops));
	mibbr_unregister();
	proc_remove(bbr_dir);
}

module_init(minet_ext_init);
module_exit(minet_ext_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("xiaomi kernel network extension");
