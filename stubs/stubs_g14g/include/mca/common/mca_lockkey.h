/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_lockkey.h - include FIRST in a module .c that must be byte-identical with the stock (ThinLTO) build.
 *
 * In the stock modules every lock_class_key of mutex_init()/init_rwsem()/init_*waitqueue_head() ended up at the same .bss
 * address (zero-size objects merged by the LTO link), so all init calls reference one symbol.  Put all keys into one
 * explicit section so that our link behaves the same (same codegen and relocation targets).
 */
#ifndef __MCA_LOCKKEY_H__
#define __MCA_LOCKKEY_H__

#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/swait.h>

#define MCA_KEY_SECTION	__attribute__((section(".bss.mca_lockkeys")))

#undef mutex_init
#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key MCA_KEY_SECTION;		\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)

#undef init_rwsem
#define init_rwsem(sem)							\
do {									\
	static struct lock_class_key __key MCA_KEY_SECTION;		\
	__init_rwsem((sem), #sem, &__key);				\
} while (0)

#undef init_waitqueue_head
#define init_waitqueue_head(wq_head)					\
do {									\
	static struct lock_class_key __key MCA_KEY_SECTION;		\
	__init_waitqueue_head((wq_head), #wq_head, &__key);		\
} while (0)

#undef init_swait_queue_head
#define init_swait_queue_head(q)					\
do {									\
	static struct lock_class_key __key MCA_KEY_SECTION;		\
	__init_swait_queue_head((q), #q, &__key);			\
} while (0)

#endif /* __MCA_LOCKKEY_H__ */
