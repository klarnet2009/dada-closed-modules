/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The stock modules are ThinLTO builds whose link merges every zero-size lockdep `static struct lock_class_key __key`
 * of mutex_init()/init_waitqueue_head()/spin_lock_init() into .bss at one address.  Our link keeps every -fdata-sections
 * .bss.<sym> input section separate, so put all keys into one explicit section: same codegen, same reloc targets.
 * Include AFTER all <linux/...> headers.
 */
#ifndef PORTA_LOCKKEYS_H
#define PORTA_LOCKKEYS_H
#define PA_KEY_SECTION	__attribute__((section(".bss.pa_lockkeys")))
#undef mutex_init
#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key PA_KEY_SECTION;		\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)
#undef init_waitqueue_head
#define init_waitqueue_head(wq_head)					\
do {									\
	static struct lock_class_key __key PA_KEY_SECTION;		\
	__init_waitqueue_head((wq_head), #wq_head, &__key);		\
} while (0)
/* explicit section variant: PA_MUTEX_INIT(&lock, ".bss.somename") */
#define PA_MUTEX_INIT(mutex, sec)					\
do {									\
	static struct lock_class_key __key __attribute__((section(sec)));	\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)
#endif
