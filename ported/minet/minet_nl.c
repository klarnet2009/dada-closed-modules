#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/rtnetlink.h>
#include <net/rtnetlink.h>

#include "minet_nl.h"
#include "minet_debug.h"

static int minet_nl_cmd_set(struct sk_buff *skb, struct genl_info *info);

static struct nla_policy minet_genl_policy[MINET_ATTR_MAX + 1] = {
	[MINET_ATTR_UNSPEC]	     	= { .type = NLA_UNSPEC, },
	[MINET_ATTR_COMMON]    		= { .type = NLA_UNSPEC },
};

static struct genl_ops minet_nl_ops[] = {
	{
		.cmd = MINET_CMD_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = minet_nl_cmd_set,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_multicast_group minet_nl_mcgrps[] = {
		{ .name = MINET_GENERIC_EVENT_MCGRP_NAME, },
};

static struct genl_family minet_nl_family __ro_after_init = {
	.name       = MINET_GENERIC_NAME,
	.version    = MINET_GENERIC_VERSION,
	.maxattr    = MINET_ATTR_MAX,
	.policy     = minet_genl_policy,
	.module		= THIS_MODULE,
	.ops		= minet_nl_ops,
	.n_ops		= ARRAY_SIZE(minet_nl_ops),
	.mcgrps		= minet_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(minet_nl_mcgrps),
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0))
	.resv_start_op	= MINET_CMD_MAX + 1,
	#endif
};

int minet_nl_msg(void *data, int len, pid_t pid, u8 cmd, u8 attr)
{
	struct sk_buff *skb;
	void *hdr;

	skb = genlmsg_new(nla_total_size(len), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_put(skb, pid, 0, &minet_nl_family, 0, cmd);
	if (!hdr)
		goto out_free;

	if (nla_put(skb, attr, len, data))
		goto out_cancel;

	genlmsg_end(skb, hdr);

	if (pid) {
		return genlmsg_unicast(&init_net, skb, pid);
	} else {
		return genlmsg_multicast(&minet_nl_family, skb, 0, 0, GFP_ATOMIC);
	}

out_cancel:
	genlmsg_cancel(skb, hdr);
out_free:
	nlmsg_free(skb);
	return -EMSGSIZE;
}

static inline int __minet_nl_notify(void *data, int len, u8 attr)
{
	return minet_nl_msg(data, len, 0, MINET_CMD_NOTIFY, attr);
}

int minet_nl_notify(void *data, int len)
{
	minet_dbg("%s %d", __func__, len);
	return __minet_nl_notify(data, len, MINET_ATTR_COMMON);
}

extern int minet_set_app(void *arg);
static int minet_nl_cmd_set(struct sk_buff *skb, struct genl_info *info)
{
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *opt_attrs[MINET_ATTR_MAX + 1];
	int err = 0;

	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	err = nla_parse_deprecated(opt_attrs, MINET_ATTR_MAX, genlmsg_data(genlhdr),
								genlmsg_len(genlhdr), minet_genl_policy, info->extack);
	if (err)
		return err;

	if (opt_attrs[MINET_ATTR_COMMON]) {
		return minet_set_app(nla_data(opt_attrs[MINET_ATTR_COMMON]));
	}
	
	return 0;
}

int  __init minet_nl_init(void)
{
	int rc;
	if ((rc = genl_register_family(&minet_nl_family))) {
		minet_err("%s genl_register_family:%d", __func__, rc);
		return rc;
	}
	minet_dbg("%s OK", __func__);
	return 0;
}

void minet_nl_finit(void)
{
	minet_dbg("%s", __func__);
	genl_unregister_family(&minet_nl_family);
}