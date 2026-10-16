/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The stock ThinLTO link folds the zero-size lockdep keys of init_waitqueue_head()/mutex_init() of one function into
 * one .bss address; a plain per-section link keeps them apart.  Put the keys of one function into one explicit section
 * (define NXP_KEY_SECTION before the first use): same codegen, same relocation targets.
 */
#ifndef __NXP_LOCKKEYS_H__
#define __NXP_LOCKKEYS_H__
#include <linux/mutex.h>
#include <linux/wait.h>
#undef init_waitqueue_head
#undef mutex_init
#define init_waitqueue_head(wq_head)					\
do {									\
	static struct lock_class_key __key NXP_KEY_SECTION;		\
	__init_waitqueue_head((wq_head), #wq_head, &__key);		\
} while (0)
#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key NXP_KEY_SECTION;		\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)
#endif
