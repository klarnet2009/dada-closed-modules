// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 XiaoMi, Inc.
 * All Rights Reserved.
 */

#include <linux/device.h>
#include <linux/anon_inodes.h>
#include <linux/idr.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include "mitee_tui.h"
#include "tui_shm.h"

static struct idr tuiIdr;
static struct mutex tuiMutex;

int tui_shm_init(void)
{
	idr_init(&tuiIdr);
	mutex_init(&tuiMutex);
	return 0;
}

int tui_shm_deinit(void)
{
	idr_destroy(&tuiIdr);
	mutex_destroy(&tuiMutex);
	return 0;
}

struct tui_shm *tui_shm_alloc(size_t size, u32 flags)
{
	int ret = 0;
	struct tui_shm *shm = NULL;
	uint8_t *page_buf = NULL;

	if (!(flags & TUI_SHM_MAPPED)) {
		tui_dev_err(-EINVAL, "shm flags is invalid");
		return ERR_PTR(-EINVAL);
	}

	shm = kzalloc(sizeof(*shm), GFP_KERNEL);
	if (!shm) {
		tui_dev_err(-ENOMEM, "fail to kzalloc shm");
		return ERR_PTR(-ENOMEM);
	}

	shm->flags = flags | TUI_SHM_USER_MAPPED;

	page_buf = (uint8_t *)vmalloc_user(PAGE_ALIGN(size));
	if (!page_buf) {
		tui_dev_err(-ENOMEM, "fail to vmalloc_user page, size:%zu", PAGE_ALIGN(size));
		ret = -ENOMEM;
		goto err_alloc_mem;
	}

	shm->vaddr = page_buf;
	shm->size = PAGE_ALIGN(size);

	mutex_lock(&tuiMutex);
	shm->id = idr_alloc(&tuiIdr, shm, 1, 0, GFP_KERNEL);
	mutex_unlock(&tuiMutex);
	if (shm->id < 0) {
		tui_dev_err(-EINVAL, "fail to idr_alloc shm id");
		ret = -EINVAL;
		goto err_alloc_id;
	}
	tui_dev_devel("shm alloc shm->id:%d, size:%lu\n", shm->id, shm->size);

	return shm;

err_alloc_id:
	vfree(page_buf);
err_alloc_mem:
	kfree(shm);

	return ERR_PTR(ret);
}


void tui_shm_free(struct tui_shm *shm)
{
	mutex_lock(&tuiMutex);

	if (shm != NULL) {
		if ((shm->flags & TUI_SHM_USER_MAPPED) && shm->id > 0) {
			tui_dev_info("free shm, name=%s\n", shm->name);
			tui_dev_devel("shm remove, shm->id:%d, fd=%d\n", shm->id, shm->fd);
			idr_remove(&tuiIdr, shm->id);
			if (shm->vaddr)
				vfree(shm->vaddr);
			kfree(shm);
		}
	}

	mutex_unlock(&tuiMutex);
}

static int tui_shm_fop_release(struct inode *inode, struct file *filp)
{
	struct tui_shm *shm = (struct tui_shm *)filp->private_data;

	tui_dev_info("release fd, and not free shm, name=%s\n",
			shm ? (char *)shm->name : "NULL");
	return 0;
}

static int tui_shm_fop_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int res = 0;
	struct tui_shm *shm = filp->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;

	tui_dev_devel("size:%zu, shm->size:%zu, shm->flags:%d, shm->id:%d\n",
				  size, shm->size, shm->flags, shm->id);

	if (!(shm->flags & TUI_SHM_USER_MAPPED)) {
		tui_dev_err(-EINVAL, "shm flags is invalid");
		return -EINVAL;
	}

	/* check for overflowing the buffer's size */
	if (vma->vm_pgoff + vma_pages(vma) > shm->size >> PAGE_SHIFT) {
		tui_dev_err(-EINVAL, "vma size is over shm_size, error");
		return -EINVAL;
	}

	res = remap_vmalloc_range(vma, (void *)shm->vaddr, vma->vm_pgoff);
	if (res < 0) {
		tui_dev_err(-EINVAL, "mmap: remap failed with error");
		return -ENOMEM;
	}

	return res;
}

static const struct file_operations tui_shm_fops = {
	.owner = THIS_MODULE,
	.release = tui_shm_fop_release,
	.mmap = tui_shm_fop_mmap,
};

int tui_shm_create_fd(uint8_t *name, struct tui_shm *shm)
{
	int fd = -1;

	mutex_lock(&tuiMutex);

	if (!(shm->flags & TUI_SHM_USER_MAPPED) || (name == NULL) || (strlen(name) == 0)) {
		tui_dev_err(-EINVAL, "shm flags is invalid");
		return -EINVAL;
	}
	fd = anon_inode_getfd(name, &tui_shm_fops, shm, O_RDWR);
	if (fd < 0) {
		tui_dev_err(-EINVAL, "failed to create anon fd, err=%d", fd);
		mutex_unlock(&tuiMutex);
		return -ENOMEM;
	}
#line 154 "../vendor/xiaomi/proprietary/kernelmodules/mitee_drv/src/tui/tui_shm.c"
	memcpy(shm->name, name, strlen(name));
	shm->fd = fd;

	mutex_unlock(&tuiMutex);
	return fd;
}

void tui_shm_delete_fd(uint8_t *name, int fd)
{
	mutex_lock(&tuiMutex);
	tui_dev_info("not release fd, and close fd in userspace, name=%s\n",
			name ? (char *)name : "NULL");
	mutex_unlock(&tuiMutex);
}

struct tui_shm *tui_shm_get_data_byid(int id)
{
	struct tui_shm *shm = NULL;

	mutex_lock(&tuiMutex);

	shm = idr_find(&tuiIdr, id);
	if (!shm) {
		tui_dev_err(-EINVAL, "failed to get data by anonid, err=%d", id);
		shm = ERR_PTR(-EINVAL);
	}

	mutex_unlock(&tuiMutex);
	return shm;
}
