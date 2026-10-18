/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * xm_compat_6657.h - force-included first into every TU of the qca_cld3 modules.
 *
 * The OS4 stock modules were compiled against the headers of kernel 6.6.57-android15-8, our tree is
 * 6.6.144. Three inline helpers changed in between and show up in the stock object code:
 *  - netif_tx_stop_queue(): 6.6.144 stores jiffies into trans_start and adds a barrier first
 *    (netif_stop_queue, netif_stop_subqueue and netif_tx_disable inherit this); 6.6.57 only does set_bit().
 *  - readl()/writel(): 6.6.57 calls log_read_mmio()/log_post_read_mmio()/log_write_mmio()/
 *    log_post_write_mmio() unconditionally, 6.6.144 first tests the static key of the rwmmio tracepoints
 *    (extra __tracepoint_rwmmio_* imports and jump table entries).
 * The 6.6.57 bodies are restored here. Behaviour for the running 6.6.144 kernel stays correct
 * (a missing trans_start update only affects the tx watchdog of a stopped queue; the log_* helpers test the
 * tracepoints themselves).
 */
#ifndef XM_COMPAT_6657_H
#define XM_COMPAT_6657_H

#include <linux/types.h>
#include <linux/compiler.h>

#if defined(_LINUX_NETDEVICE_H) || defined(__ASM_GENERIC_IO_H) || defined(_ASM_GENERIC_IO_H)
#error "xm_compat_6657.h has to be included before linux/netdevice.h and asm/io.h"
#endif

/* ---- readl / writel ---- */
static inline u32 xm_readl_144(const volatile void __iomem *addr);
static inline void xm_writel_144(u32 value, volatile void __iomem *addr);
#define readl xm_readl_144
#define writel xm_writel_144

/* ---- netif_tx_stop_queue family ---- */
#define netif_tx_stop_queue xm_netif_tx_stop_queue_144
#define netif_stop_queue xm_netif_stop_queue_144
#define netif_stop_subqueue xm_netif_stop_subqueue_144
#define netif_tx_disable xm_netif_tx_disable_144

#include <asm/io.h>
#include <linux/netdevice.h>

#undef readl
#undef writel
#undef netif_tx_stop_queue
#undef netif_stop_queue
#undef netif_stop_subqueue
#undef netif_tx_disable

#define readl readl
#define writel writel

static inline u32 readl(const volatile void __iomem *addr)
{
	u32 val;

	log_read_mmio(32, addr, _THIS_IP_, _RET_IP_);
	__io_br();
	val = __le32_to_cpu((__le32 __force)__raw_readl(addr));
	__io_ar(val);
	log_post_read_mmio(val, 32, addr, _THIS_IP_, _RET_IP_);
	return val;
}

static inline void writel(u32 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 32, addr, _THIS_IP_, _RET_IP_);
	__io_bw();
	__raw_writel((u32 __force)__cpu_to_le32(value), addr);
	__io_aw();
	log_post_write_mmio(value, 32, addr, _THIS_IP_, _RET_IP_);
}

/* only referenced by the (unused here) ioread32/iowrite32 inlines of asm-generic/io.h */
static inline u32 xm_readl_144(const volatile void __iomem *addr)
{
	return readl(addr);
}

static inline void xm_writel_144(u32 value, volatile void __iomem *addr)
{
	writel(value, addr);
}

static __always_inline void netif_tx_stop_queue(struct netdev_queue *dev_queue)
{
	/* Must be an atomic op see netif_txq_try_stop() */
	set_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

static inline void netif_stop_queue(struct net_device *dev)
{
	netif_tx_stop_queue(netdev_get_tx_queue(dev, 0));
}

static inline void netif_stop_subqueue(struct net_device *dev, u16 queue_index)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, queue_index);

	netif_tx_stop_queue(txq);
}

static inline void netif_tx_disable(struct net_device *dev)
{
	unsigned int i;
	int cpu;

	local_bh_disable();
	cpu = smp_processor_id();
	spin_lock(&dev->tx_global_lock);
	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

		__netif_tx_lock(txq, cpu);
		netif_tx_stop_queue(txq);
		__netif_tx_unlock(txq);
	}
	spin_unlock(&dev->tx_global_lock);
	local_bh_enable();
}

/* the headers above pull in printk.h/device.h, so TUs that define their own pr_fmt/dev_fmt afterwards redefine them */
#pragma clang diagnostic ignored "-Wmacro-redefined"

#endif /* XM_COMPAT_6657_H */
