/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 XiaoMi, Inc.
 * All Rights Reserved.
 */

#ifndef __TUI_SHM_H__
#define __TUI_SHM_H__

#define TUI_SHM_MAPPED         BIT(0) /* Memory mapped by the kernel */
#define TUI_SHM_USER_MAPPED    BIT(1) /* Memory mapped in user space */
#define TUI_SHM_KERNEL_MAPPED  BIT(2) /* Memory mapped in kernel space */
#define TUI_SHM_NAME_LENTH     16

struct tui_ioctl_alloc_shm_data {
	uint8_t name[TUI_SHM_NAME_LENTH];
	uint64_t size;
	uint32_t flags;
	int32_t id;
	int32_t fd;
};

struct tui_shm {
	uint8_t name[TUI_SHM_NAME_LENTH];
	int fd;
	int id;
	void *vaddr;
	size_t size;
	uint32_t flags;
	refcount_t refcount;
};

extern int tui_shm_init(void);
extern int tui_shm_deinit(void);
extern struct tui_shm *tui_shm_alloc(size_t size, u32 flags);
extern void tui_shm_free(struct tui_shm *shm);
extern int tui_shm_create_fd(uint8_t *name, struct tui_shm *shm);
extern void tui_shm_delete_fd(uint8_t *name, int fd);
extern struct tui_shm *tui_shm_get_data_byid(int id);

#endif //  __TUI_SHM_H__
