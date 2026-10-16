/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The stock modules are ThinLTO builds whose link merges every zero-size lockdep `static struct lock_class_key __key`
 * of mutex_init()/init_waitqueue_head() into .bss at one address.  Our link keeps every -fdata-sections .bss.<sym>
 * input section separate, so put all keys into one explicit section: same codegen, same reloc targets.
 * Include AFTER all <linux/...> headers.
 */
#ifndef A17D_LOCKKEYS_H
#define A17D_LOCKKEYS_H
#define A17D_KEY_SECTION	__attribute__((section(".bss.a17d_lockkeys")))
#undef mutex_init
#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key A17D_KEY_SECTION;		\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)
#undef init_waitqueue_head
#define init_waitqueue_head(wq_head)					\
do {									\
	static struct lock_class_key __key A17D_KEY_SECTION;		\
	__init_waitqueue_head((wq_head), #wq_head, &__key);		\
} while (0)
#endif
