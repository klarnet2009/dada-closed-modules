/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MISCORE_H
#define _MISCORE_H
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#define MISCORE_MAX_LINK	4
#define MISCORE_MAX_APP		4
#define MISCORE_NL_CMD		1
#define MISCORE_NL_ATTR_ENABLE	1
#define MISCORE_NL_ATTR_APP	2
#define MISCORE_NL_ATTR_LINK	3
#define MISCORE_NL_ATTR_PERIOD	4
#define MISCORE_NL_ATTR_LINKINFO	5
#define MISCORE_NL_MSG_LINKINFO	2

#define MISCORE_DBG(fmt, ...) \
	do { \
		if (READ_ONCE(miscore_debug)) \
			printk(KERN_INFO "miscore:" fmt, ##__VA_ARGS__); \
	} while (0)

/* counters of one link (96 bytes, sent to user space as is) */
struct network_para_st {
	u32 if_index;
	u32 rtt;
	u32 rtt_count;
	u32 uplink_tcp_normal_packets;
	u32 uplink_tcp_normal_bytes;
	u32 uplink_tcp_retrans_packets;
	u32 uplink_tcp_retrans_bytes;
	u32 uplink_udp_packets;
	u32 uplink_udp_bytes;
	u32 uplink_background_packets;
	u32 uplink_background_bytes;
	u32 downlink_tcp_normal_packets;
	u32 downlink_tcp_normal_bytes;
	u32 downlink_tcp_lost_packets;
	u32 downlink_tcp_lost_bytes;
	u32 downlink_tcp_retrans_packets;
	u32 downlink_tcp_retrans_bytes;
	u32 downlink_tcp_ooo_normal_packets;
	u32 downlink_tcp_ooo_bad_packets;
	u32 downlink_tcp_ooo_very_bad_packets;
	u32 downlink_udp_packets;
	u32 downlink_udp_bytes;
	u32 downlink_background_packets;
	u32 downlink_background_bytes;
};

struct miscore_link_info_st {
	struct network_para_st network_para;
	char if_name[IFNAMSIZ];
};

struct miscore_foreground_app_info_st {
	int app_num;
	int app_uids[MISCORE_MAX_APP];
};

extern int miscore_debug;

int miscore_nl_send_msg(void *data, int len, u8 cmd, u8 attr);
int miscore_nl_init(void);
void miscore_nl_exit(void);
void miscore_handle_score_enable(struct nlattr *nla);
void miscore_handle_foreground_app(struct nlattr *nla);
void miscore_handle_link_change(struct nlattr *nla);
void miscore_handle_period_change(struct nlattr *nla);

#endif
