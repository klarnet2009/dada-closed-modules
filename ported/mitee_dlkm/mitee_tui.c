// SPDX-License-Identifier: GPL-2.0-only
/*
 * MITEE TUI (trusted user interface) of mitee_dlkm: two char devices (mitee-tui, mitee-tui-supp)
 * between the TUI service in user space and the secure side.
 *
 * Recovered from the disassembly of the stock mitee_dlkm.ko (HyperOS 4.0). The vocabulary (names, messages,
 * ioctl numbers, structures) follows the newer Xiaomi XRING revision (drivers/tee/mitee/tui, GPL-2.0,
 * see attic/mitee_tui_xring_template.c); the stock is the OLDER revision: no display/touch hooks, no
 * frame buffer, the user space is woken up with poll() (tuisvc_event_queue) instead of a completion.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/string.h>
#include "mitee_tui.h"
#include "tui_shm.h"

#define MITEE_TUI_PANEL_GPIO 338

static struct tui_font_t {
	uint64_t font_size;
	uint8_t *font_buffer;
	struct tui_shm *font_shm;
} tui_font;

static struct mitee_tui_command_t g_user_cmd;
struct mitee_tui_response_t g_user_rsp;
static int token;
atomic_t fileopened;
int tuisvc_event_active;
DECLARE_WAIT_QUEUE_HEAD(tuisvc_event_queue);
static DECLARE_COMPLETION(user_comp);

struct device *dev_tlc_tui;
struct device *dev_tui_supp;
static struct cdev tui_cdev;
static struct cdev tui_supp_cdev;

static struct device_driver tui_driver = {
	.name = "MITEE_TUI"
};

struct device tui_dev = {
	.driver = &tui_driver
};

void reset_global_command_id(void)
{
	g_user_cmd.id = 0;
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
		complete_all(&user_comp);
		reinit_completion(&user_comp);

		/* Let the client know (poll) that there is a command to process. */
		tuisvc_event_active = 1;
		wake_up_interruptible(&tuisvc_event_queue);

		/* Wait for the client acknowledge (IOCTL_ACK). */
		if (!wait_for_completion_timeout(&user_comp, msecs_to_jiffies(5000))) {
			tui_dev_info("No acknowledge from client, timeout!\n");
			ret = TUI_ERR_TIMEOUT;
		}
	} else {
		/* There is no client, do nothing except reporting an error to SWd. */
		ret = TUI_ERR_INTERNAL_ERROR;
		tui_dev_err(ret, "waiting for userland answer for err!!\n");
		goto end;
	}

	reinit_completion(&user_comp);

	/* Check id of the cmd processed by ioctl thread */
	if (g_user_rsp.id != command_id) {
		ret = TUI_ERR_INTERNAL_ERROR;
		tui_dev_err(ret, "Wrong response id(%d,%d)\n", g_user_rsp.id, command_id);
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

int tui_get_panel_vendor(void)
{
	gpio_direction_input(MITEE_TUI_PANEL_GPIO);
	return gpio_get_value(MITEE_TUI_PANEL_GPIO);
}

/*
 * RPC callback (module 1, command 3) of the secure side: screen info, session and font buffer.
 */
uint32_t tui_process_tuisvc_callback(struct optee_msg_param_value *value,
				     void *buf, uint32_t size_in, uint32_t *size_out)
{
	uint32_t ret = TUI_OK;
	uint32_t command_id = (uint32_t)value->a;
	struct screen_info_t *panel_info = (struct screen_info_t *)buf;

	/* Warn if previous response was not acknowledged */
	if (command_id == CMD_TUI_SVC_NONE) {
		tui_dev_err(-1, "Notified without command");
		return -1;
	}

	switch (command_id) {
	case CMD_TUI_SVC_GET_SCREEN_INFO:
		ret = send_cmd_to_user(MITEE_TUI_CMD_GET_RESOLUTION);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "send_cmd_to_user() failed");
			return ret;
		}
		panel_info->width = g_user_rsp.data.resolution.width;
		panel_info->height = g_user_rsp.data.resolution.height;
		*size_out = sizeof(struct screen_info_t);
		break;
	case CMD_TUI_SVC_OPEN_SESSION:
		ret = send_cmd_to_user(MITEE_TUI_CMD_OPEN_SESSION);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "send_cmd_to_user() failed");
			return ret;
		}
		break;
	case CMD_TUI_SVC_CLOSE_SESSION:
		ret = send_cmd_to_user(MITEE_TUI_CMD_CLOSE_SESSION);
		if (ret != TUI_OK) {
			tui_dev_err(ret, "send_cmd_to_user() failed");
			return ret;
		}
		value->a = 0;
		break;
	case CMD_TUI_SVC_GET_FONT_BUFFER: {
		uint64_t len = value->b;
		uint64_t offset = value->c;
		uint64_t size;

		if (offset == 0) {
			/* first chunk: ask the TUI service for the shared font buffer */
			ret = send_cmd_to_user(MITEE_TUI_CMD_GET_FONT_BUFFER);
			if (ret != TUI_OK) {
				tui_dev_err(ret, "send_cmd_to_user() failed");
				return ret;
			}
			tui_font.font_shm = tui_shm_get_data_byid(g_user_rsp.data.fontShm.shm_id);
			if (tui_font.font_shm == ERR_PTR(-EINVAL)) {
				tui_dev_err(-EINVAL, "failed to get shm byid, name=%s",
					    g_user_rsp.data.fontShm.shm_name);
				return -EINVAL;
			}
			tui_font.font_buffer = tui_font.font_shm->vaddr;
			tui_font.font_size = g_user_rsp.data.fontShm.shm_size;
		}

		if (tui_font.font_size > offset + len)
			size = len;
		else
			size = tui_font.font_size - offset;
		memcpy(buf, tui_font.font_buffer + offset, size);

		if (offset == 0)
			value->b = tui_font.font_size;
		else
			value->b = size;
		*size_out = size;

		/* release font buffer after font copying is complete */
		if (tui_font.font_size <= offset + size) {
			tui_font.font_buffer = NULL;
			tui_font.font_size = 0;
			tui_shm_delete_fd(tui_font.font_shm->name, tui_font.font_shm->fd);
			tui_shm_free(tui_font.font_shm);
			tui_font.font_shm = NULL;
		}
		break;
	}
	default:
		ret = TUI_ERR_UNKNOWN_CMD;
		tui_dev_err(ret, "Unknown command");
		break;
	}

	tui_dev_info("return 0x%08x to cmd 0x%08x", ret, command_id);
	return ret;
}

int tui_receive_request(struct mitee_tui_command_t *cmd_id)
{
	*cmd_id = g_user_cmd;
	return 0;
}

int tui_send_response(struct tui_ioctl_buf_data *data)
{
	if (copy_from_user(&g_user_rsp, u64_to_user_ptr(data->buf_ptr), data->buf_len)) {
		tui_dev_err(-EFAULT, "tui_send_response copy_from_user fail");
		return -EFAULT;
	}

	/* Send signal to TEE */
	complete(&user_comp);

	return 0;
}

static const struct file_operations tui_fops;

int mitee_tui_init(void)
{
	dev_t devno;
	int err;
	static struct class *tui_class;

	dev_set_name(&tui_dev, "TUI");
	tui_dev_info("Loading mitee-tui module.");
	tui_dev_info("=============== Running MITEE TUI Kernel ===============");

	atomic_set(&fileopened, 0);

	/*------------------create tui cdev---------------------- */
	err = alloc_chrdev_region(&devno, 0, 1, TUI_DEV_NAME);
	if (err) {
		tui_dev_err(err, "Unable to allocate Trusted UI device number");
		return err;
	}

	cdev_init(&tui_cdev, &tui_fops);
	tui_cdev.owner = THIS_MODULE;

	err = cdev_add(&tui_cdev, devno, 1);
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

	cdev_init(&tui_supp_cdev, &tui_fops);
	tui_supp_cdev.owner = THIS_MODULE;
	err = cdev_add(&tui_supp_cdev, devno, 1);
	if (err) {
		tui_dev_err(err, "Unable to add Trusted UI supp char device");
		unregister_chrdev_region(devno, 1);
		return err;
	}

	tui_class = class_create("tui_supp_cls");
	dev_tui_supp = device_create(tui_class, NULL, devno, NULL, TUI_SUPP_DEV_NAME);
	if (IS_ERR(dev_tui_supp))
		return PTR_ERR(dev_tui_supp);

	token = mitee_smc_notify_connect(TUI_SVC_ID, TUI_MAGIC_NUM);
	tui_dev_info("get mitee_tui connect token: %x\n", token);

	tui_shm_init();
	tuisvc_event_active = 0;

	return 0;
}

int mitee_tui_exit(void)
{
	unregister_chrdev_region(tui_cdev.dev, 1);
	cdev_del(&tui_cdev);

	unregister_chrdev_region(tui_supp_cdev.dev, 1);
	cdev_del(&tui_supp_cdev);
	tui_shm_deinit();

	return 0;
}

static __poll_t tui_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &tuisvc_event_queue, wait);
	if (READ_ONCE(tuisvc_event_active) == 1) {
		tuisvc_event_active = 0;
		mask = EPOLLIN | EPOLLRDNORM;
	}

	return mask;
}

static int tui_ioctl_tui_shm_alloc(struct tui_ioctl_alloc_shm_data __user *udata);
static int tui_ioctl_tui_shm_free(struct tui_ioctl_alloc_shm_data __user *udata);

static long tui_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != TUI_IO_MAGIC)
		return -EINVAL;

	tui_dev_info("mitee-tui module: ioctl 0x%x +++", cmd);

	if (cmd == TUI_IO_ACK) {
		struct tui_ioctl_buf_data data = {0};

		if (copy_from_user(&data, (struct tui_ioctl_buf_data __user *)uarg,
				   sizeof(struct tui_ioctl_buf_data)))
			return -EFAULT;

		ret = tui_send_response(&data);
		if (ret)
			return ret;
		goto out;
	}

	switch (cmd) {
	case TUI_IO_NOTIFY:
		break;
	case TUI_IO_WAITCMD: {
		struct mitee_tui_command_t tui_cmd;

		tui_receive_request(&tui_cmd);
		if (copy_to_user(uarg, &tui_cmd, sizeof(struct mitee_tui_command_t))) {
			ret = -EFAULT;
			tui_dev_err(ret, "tui copy to user failed!");
		}
		break;
	}
	case TUI_CLOSE_SESSION:
		tui_dev_info("tui-free tui buffer");
		break;
	case TUI_IO_NOTIFY_CANCEL:
		if (token == 0 || token == -1)
			token = mitee_smc_notify_connect(TUI_SVC_ID, TUI_MAGIC_NUM);

		tui_dev_info(" mitee_tui check connect token: %x\n", token);
		ret = mitee_smc_notify_signal(TUI_SVC_ID, token, 0x0c);
		if (ret) {
			tui_dev_err(ret, "notify signal failed!, ret = %d\n", ret);
			ret = 0;
		}
		break;
	case TUI_IO_REGISTER_CALLBACK:
		mitee_rpc_register_callback(1, 3, tui_process_tuisvc_callback);
		break;
	case TUI_IO_ALLOC_SHM:
		ret = tui_ioctl_tui_shm_alloc(uarg);
		break;
	case TUI_IO_FREE_SHM:
		ret = tui_ioctl_tui_shm_free(uarg);
		break;
	default:
		ret = -ENOTTY;
		tui_dev_err(ret, "Unknown ioctl (%u)!", cmd);
		return ret;
	}

out:
	tui_dev_info("mitee-tui module: ioctl 0x%x ret=%d +++", cmd, ret);
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
	atomic_dec_return(&fileopened);
	return 0;
}

static int tui_ioctl_tui_shm_alloc(struct tui_ioctl_alloc_shm_data __user *udata)
{
	struct tui_ioctl_alloc_shm_data data = {0};
	struct tui_shm *shm;

	if (copy_from_user(&data, udata, sizeof(struct tui_ioctl_alloc_shm_data))) {
		tui_dev_err(-EFAULT, "failed to copy_from_user");
		return -EFAULT;
	}

	if (data.flags) {
		tui_dev_err(-EINVAL, "shm flags is invalid");
		return -EINVAL;
	}

	data.flags = TUI_SHM_MAPPED;
	shm = tui_shm_alloc(data.size, data.flags);
	if (IS_ERR(shm)) {
		tui_dev_err(-ENOMEM, "an error occured in tui_shm_alloc");
		return PTR_ERR(shm);
	}

	data.id = shm->id;
	data.fd = tui_shm_create_fd(data.name, shm);

	if (copy_to_user(udata, &data, sizeof(struct tui_ioctl_alloc_shm_data))) {
		tui_dev_err(-EFAULT, "failed to copy_to_user");
		return -EFAULT;
	}

	return 0;
}

static int tui_ioctl_tui_shm_free(struct tui_ioctl_alloc_shm_data __user *udata)
{
	struct tui_ioctl_alloc_shm_data data = {0};
	struct tui_shm *shm;

	if (copy_from_user(&data, udata, sizeof(struct tui_ioctl_alloc_shm_data))) {
		tui_dev_err(-EFAULT, "failed to copy_from_user");
		return -EINVAL;
	}

	if (data.flags) {
		tui_dev_err(-EINVAL, "shm flags is invalid");
		return -EINVAL;
	}

	shm = tui_shm_get_data_byid(data.id);
	if (shm == ERR_PTR(-EINVAL)) {
		tui_dev_err(-EINVAL, "an error occured in tui_shm_get_from_id");
		return -EINVAL;
	}

	tui_shm_delete_fd(shm->name, shm->fd);
	tui_shm_free(shm);

	return 0;
}

static const struct file_operations tui_fops = {
	.owner = THIS_MODULE,
	.poll = tui_poll,
	.unlocked_ioctl = tui_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tui_ioctl,
#endif
	.open = tui_open,
	.release = tui_release,
};

MODULE_AUTHOR("XiaoMi Limited");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MITEE TUI");
