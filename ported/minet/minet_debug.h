/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MINET_DEBUG_H_
#define __MINET_DEBUG_H_

// #define minet_dbg(fmt, ...)                                              	\
// 	printk(KERN_DEBUG "minet-d: " fmt, ##__VA_ARGS__)
#define minet_dbg(fmt, ...)

#define minet_info(fmt, ...)                                              	\
	printk(KERN_INFO "minet-i: " fmt, ##__VA_ARGS__)

#define minet_err(fmt, ...)                                               	\
	printk(KERN_ERR "minet-e: " fmt, ##__VA_ARGS__)

#define IPv4_FMT "%d.%d.%d.%d"
#define IPv4_ARG(x)                                                       	\
	((u8 *)(x))[0], ((u8 *)(x))[1], ((u8 *)(x))[2], ((u8 *)(x))[3]

#define IPv6_FMT "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x"
#define IPv6_ARG(x)                                                        	\
	ntohs((x.s6_addr16)[0]), ntohs((x.s6_addr16)[1]), 						\
	ntohs((x.s6_addr16)[2]), ntohs((x.s6_addr16)[3]),						\
	ntohs((x.s6_addr16)[4]), ntohs((x.s6_addr16)[5]), 						\
	ntohs((x.s6_addr16)[6]), ntohs((x.s6_addr16)[7])

#endif /* __MINET_DEBUG_H__*/
