// SPDX-License-Identifier: GPL-2.0
/*
 * SI-IN SIA91xx smart-amplifier tuning interface (misc devices sipa_cmd / sipa_tool).
 * Reconstructed from the stock sia91xx_tuning.ko (Xiaomi 15 "dada") by reverse engineering; derived from the
 * GPL SI-IN driver (sia91xx_v3.1.0/sipa_tuning_if.c + sipa_tuning_misc.c), differences fixed against the binary.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <asm/current.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#define DATA_MAX_LEN       1024

typedef struct dev_comm_data {
	uint32_t opt;
	uint32_t param_id;
	uint32_t payload_size;
	uint32_t reserve;
	uint8_t payload[];
} __packed dev_comm_data_t;

#define DEV_COMM_DATA_LEN(data) \
	((uint32_t)sizeof(struct dev_comm_data) + data->payload_size)

#define MAX_SIZE  (10)

typedef struct {
	uint8_t data[DATA_MAX_LEN];
	uint32_t len;
	bool flag;
} myqueue;

typedef struct {
	myqueue q[MAX_SIZE];
	uint8_t tail;
	uint8_t head;
	uint8_t k;
	wait_queue_head_t wq;
	struct mutex lock;
} sipa_sync_t;

typedef struct {
	sipa_sync_t toolup;
	sipa_sync_t tooldown;
	sipa_sync_t cmdup;
	sipa_sync_t cmddown;
} sipa_turning_t;

#define SIPA_TUNING_CTRL_WR_UP       _IOW(0x10, 0xE0, dev_comm_data_t)
#define SIPA_TUNING_CTRL_RD_UP       _IOR(0x10, 0xE1, dev_comm_data_t)
#define SIPA_TUNING_CTRL_WR_DOWN     _IOW(0x10, 0xE2, dev_comm_data_t)
#define SIPA_TUNING_CTRL_RD_DOWN     _IOR(0x10, 0xE3, dev_comm_data_t)

extern struct file_operations sipa_turning_cmd_fops;
extern struct file_operations sipa_turning_tool_fops;
sipa_turning_t *g_sipa_turning = NULL;

static void myCircularQueueCreate(sipa_sync_t *obj)
{
	int i;

	for (i = 0; i < MAX_SIZE; i++) {
		memset(obj->q[i].data, 0, DATA_MAX_LEN);
		obj->q[i].flag = false;
		obj->q[i].len = 0;
	}

	obj->head = 0;
	obj->tail = 0;
	obj->k = MAX_SIZE;
	mutex_unlock(&obj->lock);
}

static bool myCircularQueueEnQueue(sipa_sync_t *obj, void __user *arg, uint32_t len)
{
	if (obj->head == 0 && obj->tail + 1 == MAX_SIZE)
		return -1;
	else if ((obj->tail + 1) % MAX_SIZE == obj->head)
		return -1;

	if (copy_from_user(obj->q[obj->tail].data, arg, len))
		return -1;
	obj->q[obj->tail].len = len;
	obj->q[obj->tail].flag = true;
	obj->tail++;

	if (obj->tail == obj->k)
		obj->tail = 0;
	return 0;
}

static bool myCircularQueueDeQueue(void __user *arg, sipa_sync_t *obj)
{
	mutex_lock(&obj->lock);

	if (obj->head == obj->tail) {
		mutex_unlock(&obj->lock);
		return -1;
	}

	if (copy_to_user(arg, obj->q[obj->head].data, obj->q[obj->head].len)) {
		mutex_unlock(&obj->lock);
		return -1;
	}
	memset(obj->q[obj->head].data, 0, obj->q[obj->head].len);
	obj->q[obj->head].flag = false;
	obj->head++;

	if (obj->head == obj->k)
		obj->head = 0;
	mutex_unlock(&obj->lock);
	return 0;
}

static int sipa_tuning_write(void __user *arg, sipa_sync_t *pdata)
{
	uint8_t data[DATA_MAX_LEN] = { 0 };
	uint32_t len;
	struct dev_comm_data *msg = NULL;

	mutex_lock(&pdata->lock);
	if (copy_from_user(data, arg, sizeof(dev_comm_data_t))) {
		mutex_unlock(&pdata->lock);
		return -EFAULT;
	}

	msg = (struct dev_comm_data *)data;
	len = DEV_COMM_DATA_LEN(msg);
	if (myCircularQueueEnQueue(pdata, arg, len)) {
		pr_err("[ err] %s: enqueue fail\n", __func__);
		mutex_unlock(&pdata->lock);
		return -EFAULT;
	}
	wake_up_interruptible(&pdata->wq);
	mutex_unlock(&pdata->lock);
	pr_info("[ info] %s: datalen:%d payload len:%d\n", __func__, len, msg->payload_size);

	return 0;
}

static int sipa_tuning_read(void __user *arg, sipa_sync_t *pdata)
{
	int ret = 0;

	ret = wait_event_interruptible(pdata->wq, pdata->q[pdata->head].flag);
	if (ret) {
		pr_err("[ err] %s: wait_event return\n", __func__);
		return -ERESTART;
	}

	if (myCircularQueueDeQueue(arg, pdata)) {
		pr_err("[ err] %s: dequeue fail\n", __func__);
		return -EFAULT;
	}
	return 0;
}

static int sipa_tuning_read_unblock(void __user *arg, sipa_sync_t *pdata)
{
	int cnt = 0;

	while (pdata->q[pdata->head].flag == false) {
		cnt++;
		usleep_range(1000, 2000);
		if (cnt == 300) {
			pr_err("[  err] %s: wait reply event timeout !! \n", __func__);
			return -1;
		}
	}

	if (myCircularQueueDeQueue(arg, pdata))
		return -EFAULT;
	return 0;
}

static long sipa_tuning_comm_ioctl(unsigned int cmd, unsigned long arg, sipa_sync_t *up, sipa_sync_t *down)
{
	int ret = 0;

	switch (cmd) {
	case SIPA_TUNING_CTRL_WR_UP: {
		pr_info("[ info] %s: up write cmd\n", __func__);
		ret = sipa_tuning_write((void __user *)arg, up);
	}
		break;
	case SIPA_TUNING_CTRL_RD_UP: {
		ret = sipa_tuning_read((void __user *)arg, up);
		pr_info("[ info] %s: up read cmd, ret:%d \n", __func__, ret);
	}
		break;
	case SIPA_TUNING_CTRL_WR_DOWN: {
		pr_info("[ info] %s: down write cmd\n", __func__);
		ret = sipa_tuning_write((void __user *)arg, down);
	}
		break;
	case SIPA_TUNING_CTRL_RD_DOWN: {
		ret = sipa_tuning_read_unblock((void __user *)arg, down);
		pr_info("[ info] %s: down read cmd, ret:%d \n", __func__, ret);
	}
		break;
	default: {
		pr_info("[ info] %s: unsuport cmd:0x%x\n", __func__, cmd);
		ret = -EFAULT;
		break;
	}
	}

	return ret;
}

static long sipa_tuning_cmd_unlocked_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	sipa_turning_t *priv = g_sipa_turning;
	int ret = 0;

	pr_info("[ info] %s: enter cmd:0x%x\n", __func__, cmd);
	switch (cmd) {
	case SIPA_TUNING_CTRL_WR_UP:
	case SIPA_TUNING_CTRL_RD_UP:
	case SIPA_TUNING_CTRL_WR_DOWN:
	case SIPA_TUNING_CTRL_RD_DOWN: {
		ret = sipa_tuning_comm_ioctl(cmd, arg, &(priv->cmdup), &(priv->cmddown));
		break;
	}
	default: {
		pr_err("%s: not support ioctl:0x%x\n", __func__, cmd);
		break;
	}
	}
	return ret;
}

struct file_operations sipa_turning_cmd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = sipa_tuning_cmd_unlocked_ioctl,
	.compat_ioctl = sipa_tuning_cmd_unlocked_ioctl,
};

static int sipa_tuning_tool_open(struct inode *inode, struct file *fp)
{
	sipa_turning_t *priv = g_sipa_turning;

	pr_info("[ info] %s: run !! \n", __func__);

	myCircularQueueCreate(&priv->toolup);
	myCircularQueueCreate(&priv->tooldown);
	myCircularQueueCreate(&priv->cmdup);
	myCircularQueueCreate(&priv->cmddown);

	return 0;
}

static long sipa_tuning_tool_unlocked_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	sipa_turning_t *priv = g_sipa_turning;

	pr_info("[ info] %s: enter cmd:0x%x\n", __func__, cmd);
	return sipa_tuning_comm_ioctl(cmd, arg, &(priv->toolup), &(priv->tooldown));
}

struct file_operations sipa_turning_tool_fops = {
	.owner = THIS_MODULE,
	.open = sipa_tuning_tool_open,
	.unlocked_ioctl = sipa_tuning_tool_unlocked_ioctl,
	.compat_ioctl = sipa_tuning_tool_unlocked_ioctl,
};

#define DEVICE_NAME_CMD     "sipa_cmd"
#define DEVICE_NAME_TOOL    "sipa_tool"

struct miscdevice sipa_cmd_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME_CMD,
	.fops = &sipa_turning_cmd_fops,
};

struct miscdevice sipa_tool_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME_TOOL,
	.fops = &sipa_turning_tool_fops,
};

/*
 * The stock module was linked with the LTO module.lds, which merges the zero-sized per-use lock_class_key objects
 * of mutex_init()/init_waitqueue_head() into .bss at one address.  Our non-LTO link keeps every -fdata-sections
 * .bss.<sym> input section separate, so put all keys into one explicit section: same codegen, same reloc targets.
 */
#define SIA_KEY_SECTION	__attribute__((section(".bss.sipa_lockkeys")))
#undef init_waitqueue_head
#undef mutex_init
#define init_waitqueue_head(wq_head)					\
do {									\
	static struct lock_class_key __key SIA_KEY_SECTION;		\
	__init_waitqueue_head((wq_head), #wq_head, &__key);		\
} while (0)
#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key SIA_KEY_SECTION;		\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)

static int __init sipa_tuning_if_init(void)
{
	int ret = 0;
	sipa_turning_t *priv = NULL;

	pr_info("[ info] %s: run\n", __func__);
	priv = kzalloc(sizeof(sipa_turning_t), GFP_KERNEL);
	if (priv == NULL) {
		pr_err("[  err] %s: kmalloc failed \n", __func__);
		return -EFAULT;
	}

	init_waitqueue_head(&priv->toolup.wq);
	init_waitqueue_head(&priv->tooldown.wq);
	init_waitqueue_head(&priv->cmdup.wq);
	init_waitqueue_head(&priv->cmddown.wq);
	mutex_init(&priv->toolup.lock);
	mutex_init(&priv->tooldown.lock);
	mutex_init(&priv->cmdup.lock);
	mutex_init(&priv->cmddown.lock);

	ret = misc_register(&sipa_cmd_dev);
	if (ret) {
		pr_err("[  err] %s: err\n", __func__);
		goto err1;
	}

	ret = misc_register(&sipa_tool_dev);
	if (ret) {
		pr_err("[  err] %s: err\n", __func__);
		goto err2;
	}
	g_sipa_turning = priv;

	pr_info("[ info] %s: success\n", __func__);

	return 0;
err2:
	misc_deregister(&sipa_cmd_dev);
err1:
	if (priv) {
		kfree(priv);
		priv = NULL;
	}
	return ret;
}

static void __exit sipa_tuning_if_exit(void)
{
	pr_info("[ info] %s: run\n", __func__);

	if (g_sipa_turning) {
		mutex_destroy(&(g_sipa_turning->toolup.lock));
		mutex_destroy(&(g_sipa_turning->tooldown.lock));
		mutex_destroy(&(g_sipa_turning->cmdup.lock));
		mutex_destroy(&(g_sipa_turning->cmddown.lock));

		kfree(g_sipa_turning);
		g_sipa_turning = NULL;
	}
	misc_deregister(&sipa_cmd_dev);
	misc_deregister(&sipa_tool_dev);
}

module_init(sipa_tuning_if_init);
module_exit(sipa_tuning_if_exit);
MODULE_LICENSE("GPL");
