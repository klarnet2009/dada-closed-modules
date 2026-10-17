// SPDX-License-Identifier: GPL-2.0
/* miscore: generic netlink part (family nl_miscore) */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#include "miscore.h"

static int s_portid;

static struct genl_family miscore_nl_family;

int miscore_nl_send_msg(void *data, int len, u8 cmd, u8 attr)
{
	struct sk_buff *skb;
	void *hdr;
	int ret, portid;

	if (!READ_ONCE(s_portid)) {
		MISCORE_DBG("portid is 0\n");
		return -EINVAL;
	}
	skb = nlmsg_new(genlmsg_total_size(nla_total_size(len)), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	hdr = genlmsg_put(skb, READ_ONCE(s_portid), 0, &miscore_nl_family, 0, cmd);
	if (!hdr) {
		nlmsg_free(skb);
		return -EMSGSIZE;
	}
	if (nla_put(skb, attr, len, data)) {
		genlmsg_cancel(skb, hdr);
		nlmsg_free(skb);
		return -EMSGSIZE;
	}
	genlmsg_end(skb, hdr);
	portid = READ_ONCE(s_portid);
	ret = netlink_unicast(init_net.genl_sock, skb, portid, MSG_DONTWAIT);
	return ret > 0 ? 0 : ret;
}

static int miscore_nl_cmd_msg_handle(struct sk_buff *skb, struct genl_info *info)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;

	if (nlh->nlmsg_type != miscore_nl_family.id) {
		printk(KERN_INFO "miscore:nlmsg_type error\xef\xbc\x9auser_id=%d, kernel_id=%d\n",
		       nlh->nlmsg_type, miscore_nl_family.id);
		return -EINVAL;
	}
	if (READ_ONCE(s_portid) != nlh->nlmsg_pid) {
		printk(KERN_INFO "miscore:update portid:new=%d,old=%d\n", nlh->nlmsg_pid,
		       READ_ONCE(s_portid));
		s_portid = nlh->nlmsg_pid;
	}
	genlhdr = nlmsg_data(nlh);
	if (genlhdr->cmd != MISCORE_NL_CMD) {
		printk(KERN_INFO "miscore:unknown cmd:%d\n", genlhdr->cmd);
		return -EINVAL;
	}
	nla = (struct nlattr *)(genlhdr + 1);
	switch (nla->nla_type) {
	case MISCORE_NL_ATTR_ENABLE:
		miscore_handle_score_enable(nla);
		break;
	case MISCORE_NL_ATTR_APP:
		miscore_handle_foreground_app(nla);
		break;
	case MISCORE_NL_ATTR_LINK:
		miscore_handle_link_change(nla);
		break;
	case MISCORE_NL_ATTR_PERIOD:
		miscore_handle_period_change(nla);
		break;
	default:
		MISCORE_DBG("unknown attr:%d\n", nla->nla_type);
		break;
	}
	return 0;
}

static struct genl_ops miscore_nl_ops[] = {
	{
		.cmd = MISCORE_NL_CMD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = miscore_nl_cmd_msg_handle,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family miscore_nl_family __ro_after_init = {
	.name = "nl_miscore",
	.version = 1,
	.maxattr = 5,
	.module = THIS_MODULE,
	.ops = miscore_nl_ops,
	.n_ops = ARRAY_SIZE(miscore_nl_ops),
	.resv_start_op = 3,
};

int miscore_nl_init(void)
{
	int ret = genl_register_family(&miscore_nl_family);

	if (ret) {
		printk(KERN_INFO "miscore:register miscore_nl_family fail, ret=%d\n", ret);
		return ret;
	}
	printk(KERN_INFO "miscore:register miscore_nl_family success, familyid=%d\n",
	       miscore_nl_family.id);
	return 0;
}

void miscore_nl_exit(void)
{
	genl_unregister_family(&miscore_nl_family);
}

