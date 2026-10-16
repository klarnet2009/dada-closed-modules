/* SPDX-License-Identifier: GPL-2.0 */
/*
 * a17C_compat.h - two build-environment compatibility tweaks that make a module built against the 6.6.144 headers
 * byte-identical to the stock 6.6.57 build:
 *
 * 1. A17C_MMIO_6657: include this BEFORE <linux/io.h>.  6.6.144 gates log_read_mmio()/log_write_mmio() in the generic
 *    readX()/writeX() helpers with rwmmio_tracepoint_enabled() (a static key); 6.6.57 called them unconditionally.
 *    The helpers are renamed away and re-defined with the 6.6.57 shape (readl_relaxed/writel_relaxed only).
 * 2. A17C_LOCKKEYS: include this AFTER the lock/completion headers; define A17C_KEY_SECTION first.  The stock ThinLTO link
 *    folds the zero-size lockdep keys of mutex_init()/init_waitqueue_head()/init_completion() into one .bss address, a plain
 *    link keeps one section per key.  All keys of the module are put into one explicit section instead.
 */
#ifndef __A17C_COMPAT_H__
#define __A17C_COMPAT_H__

#ifdef A17C_MMIO_6657
#define readl_relaxed a17c_orig_readl_relaxed
#define writel_relaxed a17c_orig_writel_relaxed
#include <linux/io.h>
#undef readl_relaxed
#undef writel_relaxed
static noinline __maybe_unused u32 readl_relaxed(const volatile void __iomem *addr)
{
	u32 val;

	log_read_mmio(32, addr, _THIS_IP_, _RET_IP_);
	val = __le32_to_cpu((__le32 __force)__raw_readl(addr));
	log_post_read_mmio(val, 32, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#define readl_relaxed readl_relaxed
static noinline __maybe_unused void writel_relaxed(u32 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 32, addr, _THIS_IP_, _RET_IP_);
	__raw_writel((u32 __force)__cpu_to_le32(value), addr);
	log_post_write_mmio(value, 32, addr, _THIS_IP_, _RET_IP_);
}
#define writel_relaxed writel_relaxed
#endif /* A17C_MMIO_6657 */

#ifdef A17C_KEY_SECTION
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/swait.h>
#include <linux/completion.h>
#undef init_waitqueue_head
#undef mutex_init
#undef init_completion
#define init_waitqueue_head(wq_head)					\
do {									\
	static struct lock_class_key __key __attribute__((section(A17C_KEY_SECTION)));	\
	__init_waitqueue_head((wq_head), #wq_head, &__key);		\
} while (0)
#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key __attribute__((section(A17C_KEY_SECTION)));	\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)
#define init_completion(x)						\
do {									\
	static struct lock_class_key __key __attribute__((section(A17C_KEY_SECTION)));	\
	(x)->done = 0;							\
	__init_swait_queue_head(&(x)->wait, "&x->wait", &__key);	\
} while (0)
#endif /* A17C_KEY_SECTION */

#endif /* __A17C_COMPAT_H__ */
