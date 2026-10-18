// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 XiaoMi, Inc.
 * All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fb.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/gpio.h>
#include <linux/highmem.h>

#include "mitee_tui.h"
#include "tui_shm.h"

#ifdef XRING_MITEE_TUI
#include "display/display_tui.h"
#include "tui_hal_xring.h"
#define TUI_FRAME_BUFFER  "xring_tui_display"
#else
#include <mtk_heap.h>
#include <public/trusted_mem_api.h>
#include <linux/dma-heap.h>
#include <uapi/linux/dma-heap.h>
#define TUI_FRAME_BUFFER  "mtk_tui_region-aligned"
#endif

/* Static variables */
static DECLARE_COMPLETION(g_tuisvc_comp);
static DECLARE_COMPLETION(g_user_comp);
static struct mitee_tui_command_t g_user_cmd = {.id = MITEE_TUI_CMD_NONE};
struct mitee_tui_response_t g_user_rsp;
static struct font_info g_tui_font = { 0 };
static struct cdev g_tui_cdev;
static struct cdev g_tui_supp_cdev;
static unsigned int g_token;

struct device* (dev_tlc_tui) = NULL;
struct device* (dev_tui_supp) = NULL;
struct tui_alloc_buffer_t g_fb_buffer = { 0 };

#define DISP_ID_DET (243 + 95) //get panel vendor
#define MITEE_TUI_IO_BUFFER_MAX_SIZE 4096
#define MITEE_FB_BUF_SIZE   (20 * 1024 * 1024)
#define MITEE_FONT_BUF_SIZE (7 * 1024 * 1024)
#define MITEE_FONT_SIZE     (6499984)

atomic_t fileopened;

void hal_get_fb_buffer(struct tui_alloc_buffer_t **buffer)
{
	*buffer = &g_fb_buffer;
}

#ifndef XRING_MITEE_TUI
uint32_t hal_tui_alloc(const char *alloc_name,
					   struct tui_alloc_buffer_t *alloc_buffer,
					   size_t alloc_size)
{
	uint32_t ret = TUI_ERR_INTERNAL_ERROR;
	uint64_t pa = 0;
	uint64_t sec_handle = 0;
	struct dma_heap *dma_heap;

	if (!alloc_buffer) {
		ret = TUI_ERR_BAD_PARAMETERS;
		tui_dev_err(ret, "%s(%d): alloc_buffer is null\n", __func__, __LINE__);
		return ret;
	}

	if ((size_t)alloc_size == 0) {
		pr_notice("%s(%d): Nothing to allocate\n", __func__, __LINE__);
		return TUI_ERR_BAD_PARAMETERS;
	}

	dma_heap = dma_heap_find(alloc_name);
	if (!dma_heap) {
		ret = TUI_ERR_INTERNAL_ERROR;
		tui_dev_err(ret, "heap find failed!\n");
		return ret;
	}

	if (alloc_buffer->dma_buf == NULL)
		tui_dev_devel("tui_dma_buf is NULL, it's ok!\n");

	alloc_buffer->dma_buf = dma_heap_buffer_alloc(dma_heap, alloc_size,
		DMA_HEAP_VALID_FD_FLAGS, DMA_HEAP_VALID_HEAP_FLAGS);
	if (IS_ERR(alloc_buffer->dma_buf)) {
		ret = TUI_ERR_OUT_OF_MEMORY;
		tui_dev_err(ret, "%s, alloc buffer fail, heap:%s", __func__,
					dma_heap_get_name(dma_heap));
		goto error;
	}

	sec_handle = dmabuf_to_secure_handle(alloc_buffer->dma_buf);
	if (!sec_handle) {
		ret = TUI_ERR_INTERNAL_ERROR;
		tui_dev_err(ret, "%s, get tui frame buffer secure handle failed!\n", __func__);
		goto error;
	}

	ret = trusted_mem_api_query_pa(0, 0, 0, 0, &sec_handle, 0, 0, 0, &pa);
	if (ret == 0) {
		alloc_buffer->pa = (uint64_t)pa;
		alloc_buffer->ffa_handle = (uint64_t)sec_handle;
		tui_dev_devel("%s(%d):buf 0x%llx, handle 0x%llx\n",
			__func__, __LINE__, alloc_buffer->pa, alloc_buffer->ffa_handle);
	} else {
		ret = TUI_ERR_INTERNAL_ERROR;
		tui_dev_err(ret, "%s(%d): trusted_mem_api_query_pa failed!\n",
					__func__, __LINE__);
		goto error;
	}

	return TUI_OK;

error:
	if (alloc_buffer->dma_buf != NULL) {
		tui_dev_devel("dma_heap_buffer_free0!!\n");
		dma_heap_buffer_free(alloc_buffer->dma_buf);
		alloc_buffer->dma_buf = NULL;
	}

	return ret;
}

void hal_tui_free(void)
{
	tui_dev_devel("[TUI-HAL] %s\n", __func__);

	if (g_fb_buffer.dma_buf != NULL) {
		tui_dev_devel("dma_heap_buffer_free1!!\n");
		dma_heap_buffer_free(g_fb_buffer.dma_buf);
		g_fb_buffer.dma_buf = NULL;
	}
}
#endif // XRING_MITEE_TUI

void reset_global_command_id(void)
{
	g_user_cmd.id = MITEE_TUI_CMD_NONE;
}

/* send cancel notify */
void hal_display_callback(void)
{
	int ret;

	tui_dev_info("mitee_smc_notify_signal +++\n");

	ret = mitee_smc_notify_signal(TUI_SVC_ID, g_token, 0x0c);
	if (ret)
		tui_dev_err(ret, "notify signal failed!, ret = %d\n", ret);

}

uint32_t send_cmd_to_user(uint32_t command_id)
{
	uint32_t ret = TUI_OK;
	/* Init shared variables */
	g_user_cmd.id = command_id;

	/*
	 * Check that the client (TuiService) is still present before to return
	 * the command.
	 */
	if (atomic_read(&fileopened)) {
		/* Clean up previous response. */
		complete_all(&g_user_comp);
		reinit_completion(&g_user_comp);

		/*
		 * Unlock the ioctl thread (IOCTL_WAIT) in order to let the
		 * client know that there is a command to process.
		 */
		complete(&g_tuisvc_comp);
		tui_dev_devel("waiting for the userland response\n");
		/* Wait for the client acknowledge (IOCTL_ACK). */
		unsigned long completed = wait_for_completion_timeout(&g_user_comp,
				msecs_to_jiffies(5000));
		if (!completed) {
			tui_dev_info("No acknowledge from client, timeout!\n");
			ret = TUI_ERR_TIMEOUT;
		}
	} else {
		/*
		 * There is no client, do nothing except reporting an error to
		 * SWd.
		 */
		ret = TUI_ERR_INTERNAL_ERROR;
		tui_dev_err(ret, "waiting for userland answer for err!!\n");
		goto end;
	}

	tui_dev_devel("Got an answer from ioctl thread.\n");
	reinit_completion(&g_user_comp);

	/* Check id of the cmd processed by ioctl thread */
	if (g_user_rsp.id != command_id) {
		ret = TUI_ERR_INTERNAL_ERROR;
		tui_dev_err(ret, "Wrong response id %u %u\n", g_user_rsp.id, command_id);
	} else {
		/* retrieve return code */
		switch (g_user_rsp.return_code) {
		case MITEE_TUI_OK:
			ret = TUI_OK;
			break;
		case MITEE_TUI_ERROR:
			ret = TUI_ERR_INTERNAL_ERROR;
			break;
		case MITEE_TUI_ERR_UNKNOWN_CMD:
			ret = TUI_ERR_UNKNOWN_CMD;
			break;
		}
	}

end:
	reset_global_command_id();
	return ret;
}

/* ------------------------------------------------------------- */
static uint32_t tui_process_get_screen_info(void *buf, uint32_t *size_out)
{
	uint32_t ret = TUI_OK;
	unsigned long size = 0;
	struct screen_info_t *panel_info = NULL;

	panel_info = (struct screen_info_t *)buf;

	/* send cmd to user to get resolution */
	ret = send_cmd_to_user(MITEE_TUI_CMD_GET_RESOLUTION);
	if (ret != TUI_OK) {
		tui_dev_err(ret, "%d send_cmd_to_user() failed", __LINE__);
		return ret;
	}

	panel_info->width = g_user_rsp.data.resolution.width;
	panel_info->height = g_user_rsp.data.resolution.height;
	tui_dev_devel("tui value->a = %llu, value->b = %llu\n",
				  panel_info->width, panel_info->height);

	size = panel_info->width * panel_info->height * 4;//RGBA
#ifndef XRING_MITEE_TUI
	if (g_fb_buffer.pa == 0) {
		ret = hal_tui_alloc(TUI_FRAME_BUFFER, &g_fb_buffer, size);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "tui alloc failed. err = %d\n", ret);
			return ret;
		}
	}
#endif
	tui_dev_devel("fb phy addr or sfd = 0x%llx, size = %lu\n", g_fb_buffer.pa, size);

	panel_info->pa = g_fb_buffer.pa;
	panel_info->panel_vendor = tpd_tui_get_vendor_id();
	*size_out = sizeof(struct screen_info_t);

	tui_dev_devel("enter mitee tpd and display tui panel_vendor = %d\n",
				  panel_info->panel_vendor);
#ifdef XRING_MITEE_TUI
	ret = display_enter_tui();
	if (ret != TUI_OK) {
		// hal_tui_free();
		tui_dev_err(ret, "%d display entry fail", __LINE__);
		return ret;
	}
	display_callback_regist(hal_display_callback);
	ret = tpd_enter_tui();
	if (ret != TUI_OK) {
		// hal_tui_free();
		tui_dev_err(ret, "%d tp entry fail", __LINE__);
		return ret;
	}
#else
	display_enter_tui();
	tpd_enter_tui();
#endif
	return TUI_OK;
}

static uint32_t tui_process_get_font_info(struct optee_msg_param_value *value,
										  void *buf, uint32_t *size_out)
{
	uint32_t ret = TUI_OK;
	unsigned long size = 0;
	size_t offset = 0;

	offset = value->c;
	size = value->b;

	/* if offset is zero, send cmd to user to get font lib */
	if (offset == 0) {
		ret = send_cmd_to_user(MITEE_TUI_CMD_GET_FONT_BUFFER);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "%d send_cmd_to_user() failed", __LINE__);
			return ret;
		}

		tui_dev_devel("tui shm.id:%d, shm.size:%u\n", g_user_rsp.data.fontShm.shm_id,
					  g_user_rsp.data.fontShm.shm_size);

		g_tui_font.font_shm = tui_shm_get_data_byid(g_user_rsp.data.fontShm.shm_id);
		if (g_tui_font.font_shm == ERR_PTR(-EINVAL)) {
			tui_dev_err(-EINVAL, "tui_shm_get_data_byid() failed");
			return -EINVAL;
		}
		g_tui_font.font_size = g_user_rsp.data.fontShm.shm_size;
		g_tui_font.font_buffer = (uint8_t *)g_tui_font.font_shm->vaddr;
	}

	/* loop to transfer font buffer in a 128K buffer */
	if (g_tui_font.font_size <= offset + size)
		size = g_tui_font.font_size - offset;

	memcpy((uint8_t *)buf, g_tui_font.font_buffer + offset, size);
	if (offset == 0) {
		value->b = g_tui_font.font_size;
#ifdef XRING_MITEE_TUI
		struct tui_alloc_buffer_t *font_buffer = NULL;

		hal_get_font_buffer(&font_buffer);
		value->c = font_buffer->pa;
#endif
	} else {
		value->b = size;
	}
	*size_out = size;

	/* release font buffer after font copying is complete */
	if (g_tui_font.font_size <= offset + size) {
		g_tui_font.font_buffer = NULL;
		g_tui_font.font_size = 0;
		tui_shm_delete_fd(g_tui_font.font_shm->name, g_tui_font.font_shm->fd);
		tui_shm_free(g_tui_font.font_shm);
		g_tui_font.font_shm = NULL;
	}

	return ret;
}

uint32_t tui_process_tuisvc_cmd(struct optee_msg_param_value *value,
	void *buf, uint32_t size_in, uint32_t *size_out)
{
	uint32_t ret = TUI_OK;
	uint32_t command_id = (uint32_t)value->a;
	unsigned long size = 0;
	struct screen_info_t *panel_info = NULL;

	/* Warn if previous response was not acknowledged */
	if (command_id == CMD_TUI_SVC_NONE) {
		tui_dev_err(-1, "Notified without command");
		return -1;
	}

	tui_dev_devel("%s %u\n", __func__, command_id);
	/* Handle command */
	switch (command_id) {
	case CMD_TUI_SVC_GET_SCREEN_INFO:
		ret = tui_process_get_screen_info(buf, size_out);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "%d tui_process_get_screen_info failed", __LINE__);
			return ret;
		}
		break;
	case CMD_TUI_SVC_OPEN_SESSION:
		ret = send_cmd_to_user(MITEE_TUI_CMD_OPEN_SESSION);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "%d send_cmd_to_user() failed", __LINE__);
			return ret;
		}
		break;
	case CMD_TUI_SVC_CLOSE_SESSION:
		// hal_tui_free();
		tui_dev_devel("exit mitee tpd and display tui begin++++\n");
		tpd_exit_tui();
		display_exit_tui();
		tui_dev_devel("exit mitee tpd and display tui end ++++\n");
		/* send cmd to user to close session */
		ret = send_cmd_to_user(MITEE_TUI_CMD_CLOSE_SESSION);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "%d send_cmd_to_user() failed", __LINE__);
			return ret;
		}
		value->a = ret;
		break;
	case CMD_TUI_SVC_GET_FONT_BUFFER:
		ret = tui_process_get_font_info(value, buf, size_out);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "%d tui_process_get_screen_info failed", __LINE__);
			return ret;
		}
		break;
	case CMD_TUI_SVC_INIT_TOUCH:
		panel_info = (struct screen_info_t *)buf;
		panel_info->panel_vendor = tpd_tui_get_vendor_id();

		tui_dev_devel("panel_vendor = %d\n", panel_info->panel_vendor);

		*size_out = sizeof(struct screen_info_t);
		tpd_enter_tui();
		break;
	case CMD_TUI_SVC_INIT_DISPLAY:
		panel_info = (struct screen_info_t *)buf;

		/* send cmd to user to get resolution */
		ret = send_cmd_to_user(MITEE_TUI_CMD_GET_RESOLUTION);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "%d send_cmd_to_user() failed", __LINE__);
			return ret;
		}
		panel_info->width = g_user_rsp.data.resolution.width;
		panel_info->height = g_user_rsp.data.resolution.height;
		tui_dev_devel("tui value->a = %llu, value->b = %llu\n",
					  panel_info->width, panel_info->height);
		size = panel_info->width * panel_info->height * 4;//RGBA
		ret = hal_tui_alloc(TUI_FRAME_BUFFER, &g_fb_buffer, size);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "tui alloc failed. err = %d\n", ret);
			return ret;
		}
		tui_dev_devel("fb phy addr = %llu, size = %lu\n", g_fb_buffer.pa, size);
		panel_info->pa = g_fb_buffer.ffa_handle;

		*size_out = sizeof(struct screen_info_t);

		tui_dev_devel("begin display tui\n");
		display_enter_tui();
		break;
	case CMD_TUI_SVC_DEINIT_TOUCH:
		tui_dev_devel("exit mitee tpd\n");
		tpd_exit_tui();

		/* send cmd to user to close session */
		ret = send_cmd_to_user(MITEE_TUI_CMD_CLOSE_SESSION);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "%d send_cmd_to_user() failed", __LINE__);
			return ret;
		}
		value->a = ret;
		break;
	case CMD_TUI_SVC_DEINIT_DISPLAY:
		// hal_tui_free();

		tui_dev_devel("exit display tui\n");
		display_exit_tui();

		/* send cmd to user to close session */
		ret = send_cmd_to_user(MITEE_TUI_CMD_CLOSE_SESSION);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "%d send_cmd_to_user() failed", __LINE__);
			return ret;
		}
		value->a = ret;
		break;
	default:
		ret = TUI_ERR_UNKNOWN_CMD;
		tui_dev_err(ret, "%d Unknown command", __LINE__);
		break;
	}

	/* Fill in response to SWd, fill ID LAST */
	tui_dev_info("return 0x%08x to cmd 0x%08x", ret, command_id);
	return ret;
}

int tui_wait_cmd(struct mitee_tui_command_t *cmd_id)
{
	int ret = 0;

	if (wait_for_completion_interruptible(&g_tuisvc_comp)) {
		ret = -ERESTARTSYS;
		return ret;
	}
	reinit_completion(&g_tuisvc_comp);

	*cmd_id = g_user_cmd;
	return 0;
}

int tui_ack_cmd(struct tui_ioctl_buf_data *data)
{
	struct mitee_tui_response_t __user *uarg;

	uarg = u64_to_user_ptr(data->buf_ptr);
	if (copy_from_user(&g_user_rsp, uarg, data->buf_len)) {
		tui_dev_err(-EFAULT, "%s copy_from_user fail", __func__);
		return -EFAULT;
	}

	/* Send signal to TEE*/
	complete(&g_user_comp);

	return 0;
}

static int tui_ioctl_tui_shm_alloc(struct tui_ioctl_alloc_shm_data __user *udata)
{
	int ret = 0;
	struct tui_ioctl_alloc_shm_data data;
	struct tui_shm *shm = NULL;

	if (copy_from_user(&data, (struct tui_ioctl_alloc_shm_data __user *)udata,
		sizeof(struct tui_ioctl_alloc_shm_data))) {
		tui_dev_err(-EFAULT, "%d failed to copy_from_user", __LINE__);
		return -EFAULT;
	}

	if (data.size != MITEE_FONT_SIZE) {
		tui_dev_err(-EFAULT, "%d date size is invalid, %llu", __LINE__, data.size);
		return -EFAULT;
	}
	/* Currently no input flags are supported */
	if (data.flags) {
		tui_dev_err(-EINVAL, "%d shm flags is invalid", __LINE__);
		return -EINVAL;
	}

	data.flags |= TUI_SHM_MAPPED;
	shm = tui_shm_alloc(data.size, data.flags);
	if (IS_ERR(shm)) {
		tui_dev_err(-ENOMEM, "%d an error occured in tui_shm_alloc", __LINE__);
		return PTR_ERR(shm);
	}

	data.id = shm->id;
	/* add terminate string for safety */
	data.name[TUI_SHM_NAME_LENTH - 1] = '\0';
	data.fd = tui_shm_create_fd(data.name, shm);
	if (copy_to_user(udata, &data, sizeof(data))) {
		tui_dev_err(-EFAULT, "%d failed to copy_to_user", __LINE__);
		ret = -EFAULT;
	}

	return ret;
}

static int tui_ioctl_tui_shm_free(struct tui_ioctl_alloc_shm_data __user *udata)
{
	struct tui_ioctl_alloc_shm_data data;
	struct tui_shm *shm;

	if (copy_from_user(&data, (struct tui_ioctl_alloc_shm_data __user *)udata,
		sizeof(struct tui_ioctl_alloc_shm_data))) {
		tui_dev_err(-EFAULT, "%d failed to copy_from_user", __LINE__);
		return -EINVAL;
	}

	/* Currently no input flags are supported */
	if (data.flags) {
		tui_dev_err(-EINVAL, "%d shm flags is invalid", __LINE__);
		return -EINVAL;
	}

	tui_dev_devel("tui shm_id:%d, shm_size:%llu\n", data.id, data.size);
	shm = tui_shm_get_data_byid(data.id);
	if (shm == ERR_PTR(-EINVAL)) {
		tui_dev_err(-EINVAL, "%d an error occured in tui_shm_get_from_id", __LINE__);
		return -EINVAL;
	}

	tui_shm_delete_fd(shm->name, shm->fd);
	tui_shm_free(shm);

	return 0;
}

static long tui_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int ret = -ENOTTY;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != TUI_IO_MAGIC)
		return -EINVAL;

	tui_dev_info("mitee-tui module: ioctl 0x%x", cmd);

	switch (cmd) {
	case TUI_IO_NOTIFY: {
		ret = 0;
		break;
	}
	case TUI_IO_WAITCMD: {
		struct mitee_tui_command_t tui_cmd = {0};

		ret = tui_wait_cmd(&tui_cmd);
		if (ret)
			return ret;

		tui_dev_devel("tui send %d cmd to tui daemon", tui_cmd.id);
		if (copy_to_user(uarg, &tui_cmd, sizeof(struct mitee_tui_command_t))) {
			ret = -EFAULT;
			tui_dev_err(ret, "tui copy to user failed!");
		} else {
			ret = 0;
		}

		break;
	}
	case TUI_IO_ACK: {
		struct tui_ioctl_buf_data data;

		ret = 0;

		if (copy_from_user(&data, (struct tui_ioctl_buf_data __user *)uarg,
								sizeof(struct tui_ioctl_buf_data))) {
			tui_dev_devel("TUI_IO_ACK copy_from_user fail");
			return -EFAULT;
		}

		if (data.buf_len > MITEE_TUI_IO_BUFFER_MAX_SIZE) {
			tui_dev_devel("TUI_IO_ACK buffer len %llu over %d",
					data.buf_len, MITEE_TUI_IO_BUFFER_MAX_SIZE);
			return -EFAULT;
		}

		ret = tui_ack_cmd(&data);
		if (ret)
			return ret;

		break;
	}
	case TUI_IO_REGISTER_CALLBACK: {
		ret = 0;
		mitee_rpc_register_callback(1, 3, tui_process_tuisvc_cmd);
		break;
	}
	case TUI_IO_ALLOC_SHM: {
		ret = tui_ioctl_tui_shm_alloc(uarg);
		break;
	}
	case TUI_IO_FREE_SHM: {
		ret = tui_ioctl_tui_shm_free(uarg);
		break;
	}

	case TUI_IO_NOTIFY_CANCEL: {
		if (g_token == 0 || g_token == 0xffffffff)
			g_token = mitee_smc_notify_connect(TUI_SVC_ID, TUI_MAGIC_NUM);

		tui_dev_info(" mitee_tui check connect g_token: %x\n", g_token);
		ret = mitee_smc_notify_signal(TUI_SVC_ID, g_token, 0x0c);
		if (ret) {
			tui_dev_err(ret, "notify signal failed!, ret = %d\n", ret);
			ret = 0;
		}
		break;
	}
	case TUI_CLOSE_SESSION: {
		// hal_tui_free();
		tui_dev_info("exit mitee tpd and display tui\n");
		tpd_exit_tui();
		display_exit_tui();
		ret = 0;
		break;
	}
	default:
		ret = -ENOTTY;
		tui_dev_err(ret, "%d Unknown ioctl (%u)!", __LINE__, cmd);
		return ret;
	}

	return ret;
}

static int tui_open(struct inode *inode, struct file *file)
{
	tui_dev_info("TUI file opened");
	atomic_inc(&fileopened);
	return 0;
}

static int tui_release(struct inode *inode, struct file *file)
{
	if (atomic_dec_and_test(&fileopened))
		tui_dev_devel("TUI file closed");

	return 0;
}

static const struct file_operations tui_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tui_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tui_ioctl,
#endif
	.open = tui_open,
	.release = tui_release,
};

static struct device_driver tui_driver = {
	.name = "mitee_tui"
};

struct device tui_dev = {
	.driver = &tui_driver
};

static int __init mitee_tui_init(void)
{
	dev_t devno;
	int err;
	static struct class *tui_class;

	tui_dev_info("Loading mitee-tui module.");

	dev_set_name(&tui_dev, "tui");
	atomic_set(&fileopened, 0);

	/*------------------create tui cdev---------------------- */
	err = alloc_chrdev_region(&devno, 0, 1, TUI_DEV_NAME);
	if (err) {
		tui_dev_err(err, "Unable to allocate Trusted UI device number");
		return err;
	}

	cdev_init(&g_tui_cdev, &tui_fops);
	g_tui_cdev.owner = THIS_MODULE;

	err = cdev_add(&g_tui_cdev, devno, 1);
	if (err) {
		tui_dev_err(err, "Unable to add Trusted UI char device");
		unregister_chrdev_region(devno, 1);
		return err;
	}

	tui_class = class_create("tui_cls");
	dev_tlc_tui = device_create(tui_class, NULL, devno, NULL, TUI_DEV_NAME);
	if (IS_ERR(dev_tlc_tui))
		return PTR_ERR(dev_tlc_tui);

	/*------------------create tui supp cdev---------------------- */
	err = alloc_chrdev_region(&devno, 0, 1, TUI_SUPP_DEV_NAME);
	if (err) {
		tui_dev_err(err, "Unable to allocate Trusted UI supp device number");
		return err;
	}

	cdev_init(&g_tui_supp_cdev, &tui_fops);
	g_tui_supp_cdev.owner = THIS_MODULE;
	err = cdev_add(&g_tui_supp_cdev, devno, 1);
	if (err) {
		tui_dev_err(err, "Unable to add Trusted UI supp char device");
		unregister_chrdev_region(devno, 1);
		return err;
	}

	tui_class = class_create("tui_supp_cls");
	dev_tui_supp = device_create(tui_class, NULL, devno, NULL, TUI_SUPP_DEV_NAME);
	if (IS_ERR(dev_tui_supp))
		return PTR_ERR(dev_tui_supp);

	g_token = mitee_smc_notify_connect(TUI_SVC_ID, TUI_MAGIC_NUM);
	tui_dev_info("get mitee_tui connect g_token: %x\n", g_token);

	tui_shm_init();
#ifdef XRING_MITEE_TUI
	err = hal_tui_alloc(TUI_FRAME_BUFFER, &g_fb_buffer, MITEE_FB_BUF_SIZE);
	if (err != TUI_OK)
		tui_dev_err(err, "tui alloc failed. err = %d\n", err);

	err = hal_alloc_font_buffer(MITEE_FONT_BUF_SIZE);
	if (err != TUI_OK)
		tui_dev_err(err, "hal_alloc_font_buffer failed. err = %d\n", err);
#endif
	return 0;
}

static void __exit mitee_tui_exit(void)
{
	unregister_chrdev_region(g_tui_cdev.dev, 1);
	cdev_del(&g_tui_cdev);

	unregister_chrdev_region(g_tui_supp_cdev.dev, 1);
	cdev_del(&g_tui_supp_cdev);
	tui_shm_deinit();
	hal_tui_free();
}

module_init(mitee_tui_init);
module_exit(mitee_tui_exit);

MODULE_AUTHOR("XiaoMi Limited");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MITEE TUI");
