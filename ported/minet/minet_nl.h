#ifndef _MINET_NL_H_
#define _MINET_NL_H_

/**********************************
 * NETLINK_GENERIC netlink family.
 **********************************/

/* netlink attributes */
enum {
	MINET_ATTR_UNSPEC,	
	MINET_ATTR_COMMON,
	__MINET_ATTR_MAX,
	MINET_ATTR_MAX = __MINET_ATTR_MAX - 1,
};

/* netlink command */
enum {
	MINET_CMD_UNSPEC,
	MINET_CMD_SET,
	MINET_CMD_NOTIFY,
	__MINET_CMD_MAX,
	MINET_CMD_MAX = __MINET_CMD_MAX - 1,
};

/*
 * NETLINK_GENERIC related info
 */
#define MINET_GENERIC_VERSION 0x01
#define MINET_GENERIC_NAME "minet_genl"
#define MINET_GENERIC_EVENT_MCGRP_NAME "minet_event"

extern int minet_nl_init(void);
extern void minet_nl_finit(void);
int minet_nl_notify(void *data, int len);

#endif