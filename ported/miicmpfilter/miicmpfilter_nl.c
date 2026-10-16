#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/rtnetlink.h>

#include "miicmpfilter.h"
#include "miicmpfilter_nl.h"

static struct nla_policy icmp_unreach_genl_policy[ICMP_UNREACH_GENL_ATTR_MAX + 1] = {
	[ICMP_UNREACH_GENL_ATTR_PORT] = { .type = NLA_U16 },
	[ICMP_UNREACH_GENL_ATTR_IP] = { .len = IP_MAX_LEN },
	[ICMP_UNREACH_GENL_ATTR_INTERFACE] = { .len = INTERFACE_MAX_LEN },
};

static int icmp_unreach_genl_open(struct sk_buff *skb, struct genl_info *info)
{
	register_net_hook();
	return 0;
}

static int icmp_unreach_genl_close(struct sk_buff *skb, struct genl_info *info)
{
	unregister_net_hook();
	return 0;
}

static struct genl_ops icmp_unreach_genl_ops[] = {
	{
		.cmd = ICMP_UNREACH_GENL_EVENT_OPEN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = icmp_unreach_genl_open,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = ICMP_UNREACH_GENL_EVENT_CLOSE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = icmp_unreach_genl_close,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_multicast_group icmp_unreach_genl_mcgrps[] = {
	{
		.name = ICMP_UNREACH_MULTICAST_GROUP_EVENT,
	},
};

static struct genl_family imcp_unreach_genl_family __ro_after_init = {
	.name = ICMP_UNREACH_GENL_NAME,
	.version = ICMP_UNREACH_GENL_VERSION,
	.maxattr = ICMP_UNREACH_GENL_ATTR_MAX,
	.module = THIS_MODULE,
	.ops = icmp_unreach_genl_ops,
	.policy = icmp_unreach_genl_policy,
	.n_ops = ARRAY_SIZE(icmp_unreach_genl_ops),
	.mcgrps = icmp_unreach_genl_mcgrps,
	.n_mcgrps = ARRAY_SIZE(icmp_unreach_genl_mcgrps),
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0))
	.resv_start_op = ICMP_UNREACH_GENL_CMD_MAX + 1,
	#endif
};

bool send_nl_msg(u_int16_t port, const char* ip, const char* interface)
{
	int ret = 0;
	void *hdr;
	struct sk_buff *msg;
	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return false;

	hdr = genlmsg_put(
		msg, 0, 0, &imcp_unreach_genl_family, 0, ICMP_UNREACH_GENL_EVENT_ICMP_UNREACH);
	if (!hdr)
	{
		nlmsg_free(msg);
		return false;
	}

	ret = nla_put_u16(msg, ICMP_UNREACH_GENL_ATTR_PORT, port);
	if (ret < 0)
	{
		nlmsg_free(msg);
		return false;
	}

	ret = nla_put(msg, ICMP_UNREACH_GENL_ATTR_IP, strlen(ip), ip);
	if (ret < 0)
	{
		nlmsg_free(msg);
		return false;
	}

	ret = nla_put(msg, ICMP_UNREACH_GENL_ATTR_INTERFACE, strlen(interface), interface);
	if (ret < 0)
	{
		nlmsg_free(msg);
		return false;
	}

	genlmsg_end(msg, hdr);
	ret = genlmsg_multicast(&imcp_unreach_genl_family, msg, 0, 0, GFP_ATOMIC);
	if (ret < 0)
		printk(KERN_ERR "MIICMPFILTER: Failed to send genl msg: %d\n", ret);

	return true;
}

bool register_genl_family(void)
{
	int err;
	err = genl_register_family(&imcp_unreach_genl_family);
	if (err)
	{
		printk(KERN_ERR "MIICMPFILTER: failed register icmp filter family, err: %d", err);
		return false;
	}

	return true;
}

void unregister_genl_family(void)
{
	genl_unregister_family(&imcp_unreach_genl_family);
}
