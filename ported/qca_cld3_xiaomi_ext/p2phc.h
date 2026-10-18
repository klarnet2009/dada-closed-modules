/* Xiaomi p2p header compression (p2phc) - shared declarations. Reconstructed from the OS4 stock qca_cld3_*.ko. */
#ifndef XIAOMI_P2PHC_H
#define XIAOMI_P2PHC_H

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/slhc_vj.h>

struct task_struct;
struct notifier_block;
struct sk_buff;

/*
 * Per connection state of the Xiaomi header compressor (stock layout, 0xc0 bytes): like the kernel's
 * struct cstate (Van Jacobson), but with an extra UDP header slot and without cs_hsize.
 */
struct dl_cstate {
	unsigned char cs_this;		/* 0x00 connection id (xmit) */
	bool initialized;		/* 0x01 */
	struct dl_cstate *next;		/* 0x08 next in ring (xmit) */
	struct iphdr cs_ip;		/* 0x10 */
	struct tcphdr cs_tcp;		/* 0x24 */
	struct udphdr cs_udp;		/* 0x38 */
	unsigned char cs_ipopt[64];	/* 0x40 */
	unsigned char cs_tcpopt[64];	/* 0x80 */
};

/* same as struct slcompress (0x48 bytes) with struct dl_cstate */
struct dl_slcompress {
	struct dl_cstate *tstate;
	struct dl_cstate *rstate;
	unsigned char tslot_limit;
	unsigned char rslot_limit;
	unsigned char xmit_oldest;
	unsigned char xmit_current;
	unsigned char recv_current;
	unsigned char flags;
	int sls_o_nontcp;
	int sls_o_tcp;
	int sls_o_uncompressed;
	int sls_o_compressed;
	int sls_o_searches;
	int sls_o_misses;
	int sls_i_uncompressed;
	int sls_i_compressed;
	int sls_i_error;
	int sls_i_tossed;
	int sls_i_runt;
	int sls_i_badcheck;
};

/* two VJ-style compressors: the first for TCP, the second for UDP */
struct dlhc {
	struct dl_slcompress tcp;
	struct dl_slcompress udp;
};

struct p2phc {
	spinlock_t rx_tcp_lock;
	spinlock_t tx_tcp_lock;
	spinlock_t rx_udp_lock;
	spinlock_t tx_udp_lock;
	struct dlhc *hc;
	bool hooked;
	struct task_struct *thread;
	unsigned long reserved;
};

extern struct p2phc g_p2phc;
extern struct notifier_block p2phc_nb;
extern int sysctl_p2phc_enable;
extern int sysctl_p2phc_debug;
extern int sysctl_p2phc_interval;

struct dlhc *dlhc_init(int rslots, int tslots);
void dlhc_free(struct dlhc *comp);
int dlhc_reset(struct dlhc *comp);
int dlhc_toss(struct dlhc *comp);
int dlhc_compress(struct dlhc *comp, unsigned char *icp, int isize, unsigned char *ocp,
		  unsigned char **cpp, int compress_cid);
int dlhc_uncompress(struct dlhc *comp, unsigned char *icp, int isize);
int dlhc_remember(struct dlhc *comp, unsigned char *icp, int isize);

int p2phc_init(void);
int p2phc_cleanup(void);
unsigned int p2phc_tx_netdev_hook(struct sk_buff *skb, struct net_device *unused);

#endif
