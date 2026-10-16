#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/icmp.h>

#include <net/netfilter/nf_conntrack.h>

#include "miicmpfilter.h"
#include "miicmpfilter_nl.h"

#define DRIVER_NAME "miicmpfilter"
#define DRIVER_VERSION "1.0"

static bool net_registered = false;
static char unreached_ip[16];
unsigned int hook_func(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	u_int16_t dport = 0;
	const char* interface = NULL;
	struct udphdr *udp_header = NULL;
	struct icmphdr* icmp_header = NULL;
	struct iphdr *ip_header = NULL;
	if(skb == NULL || skb->dev == NULL)
		return NF_ACCEPT;

	interface = skb->dev->name;
	if(interface == NULL)
		return NF_ACCEPT;

	if (strncmp(interface, "wlan0", strlen(interface)) != 0 &&
	    strncmp(interface, "wlan1", strlen(interface)) != 0)
		return NF_ACCEPT;

	ip_header = ip_hdr(skb);
	if (ip_header == NULL || ip_header->protocol != IPPROTO_ICMP)
		return NF_ACCEPT;

	icmp_header = icmp_hdr(skb);
	if (icmp_header == NULL || icmp_header->type != ICMP_DEST_UNREACH)
		return NF_ACCEPT;

	udp_header = (struct udphdr*)((struct iphdr *)(icmp_header + 1) + 1);
	if (udp_header == NULL)
		return NF_ACCEPT;

	dport = htons(udp_header->dest);

	memset(unreached_ip, 0, sizeof(unreached_ip));
	snprintf(unreached_ip, sizeof(unreached_ip), "%pI4", &ip_header->saddr);
	send_nl_msg(dport, unreached_ip, interface);
	return NF_ACCEPT;
}

static struct nf_hook_ops miicmpfilter_hook[] = {
	{
		.hook = hook_func,
		.pf = PF_INET,
		.hooknum = NF_INET_POST_ROUTING,
		.priority = NF_IP_PRI_FIRST,
	},
};

bool register_net_hook(void)
{
	int ret = 0;
	if (net_registered)
	{
		printk (KERN_INFO "MIICMPFILTER: net hook is already registered");
		return true;
	}

	ret = nf_register_net_hooks(&init_net, miicmpfilter_hook, ARRAY_SIZE(miicmpfilter_hook));
	if (ret)
	{
		printk (KERN_ERR "MIICMPFILTER: Cannot register netfilter");
		return false;
	}

	net_registered = true;
	printk(KERN_INFO "MIICMPFILTER: register_net_hook");
	return true;
}

bool unregister_net_hook(void)
{
	if (!net_registered)
	{
		printk (KERN_INFO "MIICMPFILTER: net hook is already unregistered");
		return true;
	}

	nf_unregister_net_hooks(&init_net, miicmpfilter_hook, ARRAY_SIZE(miicmpfilter_hook));
	net_registered = false;
	printk(KERN_INFO "MIICMPFILTER: unregister_net_hook");
	return true;
}

static int miicmpfilter_module_init(void)
{
	if (!register_genl_family())
		return -1;

	net_registered = false;
	printk(KERN_INFO "MIICMPFILTER: init");
	return 0;
}

static void miicmpfilter_module_exit(void)
{
	unregister_genl_family();
	if (net_registered)
		nf_unregister_net_hooks(&init_net, miicmpfilter_hook, ARRAY_SIZE(miicmpfilter_hook));

	net_registered = false;
	printk(KERN_INFO "MIICMPFILTER: exit");
}

module_init(miicmpfilter_module_init);
module_exit(miicmpfilter_module_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Deshou Lin <lindeshou@xiaomi.com>");
MODULE_DESCRIPTION("icmp unreach module using netfilter hooks");
MODULE_ALIAS_RTNL_LINK(DRIVER_NAME);
MODULE_VERSION(DRIVER_VERSION);
