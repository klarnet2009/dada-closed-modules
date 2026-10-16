// SPDX-License-Identifier: GPL-2.0
/*
 * mca_adsp_glink.c
 *
 * mca glink driver: property read/write channel between the MCA charging stack and the
 * Qualcomm ADSP battery manager (pmic_glink)
 *
 * Reconstructed from the stock mca_adsp_glink.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
/*
 * The stock module was LTO-linked: every lock_class_key of mutex_init()/init_completion() ended up at the same
 * .bss address.  Put all keys into one explicit section so that our link behaves the same (same codegen and
 * relocation targets).  swait.h/mutex.h are included first so the redefinitions apply to completion.h inline code.
 */
#include <linux/swait.h>
#include <linux/mutex.h>
#define MCA_ADSP_KEY_SECTION	__attribute__((section(".bss.mca_adsp_lockkeys")))
#undef init_swait_queue_head
#define init_swait_queue_head(q)					\
do {									\
	static struct lock_class_key __key MCA_ADSP_KEY_SECTION;	\
	__init_swait_queue_head((q), #q, &__key);			\
} while (0)
#undef mutex_init
#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key MCA_ADSP_KEY_SECTION;	\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/soc/qcom/qti_pmic_glink.h>
#include <mca/common/mca_log.h>
#include <mca/qcom/mca_adsp_glink.h>
#include "mca_adsp_glink_msg.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_adsp_glink"
#endif


/* one registered user of the glink channel */
struct mca_adsp_ops_node {
	struct list_head list;
	void *priv;
	struct mca_adsp_call_back *ops;
};

struct mca_adsp_glink {
	struct device *dev;
	struct pmic_glink_client *client;
	struct pmic_glink_client *qbg_client;
	struct mutex rw_lock;
	struct completion ack;
	atomic_t state;
	struct work_struct sync_work;
	u8 unused[0x30];
	u32 prop_id;
	u32 ret_code;
	u32 seq_num;
	u8 read_buf[MCA_ADSP_GLINK_MAX_DATA];
};

static struct mca_adsp_glink *g_mca_adsp_glink;
static LIST_HEAD(g_mca_adsp_glink_ops_head);
static LIST_HEAD(g_mca_adsp_glink_qbg_ops_head);

/*
 * Protocol (observed in the stock binary): requests and responses travel over pmic_glink with the
 * battery manager owner id (BATTMGR 0x800a, the "qbg" flavour uses 0x8009 over the same client).
 *
 *   request  (276 bytes): hdr{owner, type=1 (req/resp), opcode 1=read 2=write}, prop_id, seq_num, data[256]
 *   response (280 bytes): hdr, prop_id, ret_code, seq_num, data[256]
 *   notify   (type 2)   : hdr, notify_id, data[256]; fanned out to the registered ops of that owner
 *
 * mca_adsp_glink_write() below is the common transfer path: it serialises the users with rw_lock,
 * stamps a sequence number, sends the request and waits (1 s) for the response, which
 * mca_adsp_glink_callback() delivers through the ack completion and the read_buf.
 * The four exported property accessors are thin wrappers (read/write x BATTMGR/QBG).
 *
 * Return value: 0 on success, -1 for bad arguments or when the driver has not probed yet,
 * -ENOTCONN while the glink channel is down (SSR/PDR), -ETIMEDOUT when the ADSP does not answer,
 * otherwise the pmic_glink_write() error or the non-zero ret_code reported by the ADSP.
 * A response with an unexpected sequence number is dropped and ends in the timeout.
 */
static __always_inline int mca_adsp_glink_write(u32 owner, u32 opcode, unsigned int prop_id, void *data, size_t len)
{
	struct mca_adsp_glink *mca_glink_data = g_mca_adsp_glink;
	struct mca_adsp_req_msg req_msg = { 0 };
	int rc = -1;

	if (!data || len >= MCA_ADSP_GLINK_MAX_DATA || !mca_glink_data)
		return rc;

	mutex_lock(&mca_glink_data->rw_lock);
	req_msg.hdr.owner = owner;
	req_msg.hdr.type = PMIC_GLINK_REQ_RESP;
	req_msg.prop_id = prop_id;
	req_msg.seq_num = ++mca_glink_data->seq_num;
	req_msg.hdr.opcode = opcode;
	if (opcode == MCA_ADSP_OPCODE_WRITE)
		memcpy(req_msg.data, data, len);

	if (atomic_read(&mca_glink_data->state) == PMIC_GLINK_STATE_DOWN) {
#line 106
		mca_log_err("glink state is down\n");
		rc = -ENOTCONN;
		goto out;
	}
	reinit_completion(&mca_glink_data->ack);
	mca_glink_data->prop_id = prop_id;
	rc = pmic_glink_write(mca_glink_data->client, &req_msg, sizeof(req_msg));
	if (rc)
		goto out;
	rc = wait_for_completion_timeout(&mca_glink_data->ack, msecs_to_jiffies(MCA_ADSP_GLINK_WAIT_MS));
	if (!rc) {
#line 117
		mca_log_err("timed out sending message, prop_id: %d\n", mca_glink_data->prop_id);
		mca_glink_data->prop_id = -1;
		mca_glink_data->ret_code = 0;
		rc = -ETIMEDOUT;
		goto out;
	}

	rc = mca_glink_data->ret_code;
	mca_glink_data->prop_id = -1;
	mca_glink_data->ret_code = 0;
	if (rc)
		goto out;

	if (opcode == MCA_ADSP_OPCODE_READ) {
		memcpy(data, mca_glink_data->read_buf, len);
		memset(mca_glink_data->read_buf, 0, sizeof(mca_glink_data->read_buf));
	}

out:
	mutex_unlock(&mca_glink_data->rw_lock);
	return rc;
}

/**
 * mca_adsp_glink_write_prop() - write a property through the BATTMGR owner
 * @prop_id: property id understood by the ADSP battery manager
 * @data: buffer with the value to write / receiving the value read
 * @len: size of @data in bytes, at most 255
 *
 * Context: process context, sleeps up to one second.
 * Return: 0 on success or a negative error code.
 */
int mca_adsp_glink_write_prop(unsigned int prop_id, void *data, int len)
{
	return mca_adsp_glink_write(MCA_ADSP_GLINK_OWNER, MCA_ADSP_OPCODE_WRITE, prop_id, data, len);
}
EXPORT_SYMBOL(mca_adsp_glink_write_prop);

/**
 * mca_adsp_glink_read_prop() - read a property through the BATTMGR owner
 * @prop_id: property id understood by the ADSP battery manager
 * @data: buffer with the value to write / receiving the value read
 * @len: size of @data in bytes, at most 255
 *
 * Context: process context, sleeps up to one second.
 * Return: 0 on success or a negative error code.
 */
int mca_adsp_glink_read_prop(unsigned int prop_id, void *data, int len)
{
	return mca_adsp_glink_write(MCA_ADSP_GLINK_OWNER, MCA_ADSP_OPCODE_READ, prop_id, data, len);
}
EXPORT_SYMBOL(mca_adsp_glink_read_prop);

/**
 * mca_adsp_glink_qbg_write_prop() - write a property through the QBG owner
 * @prop_id: property id understood by the ADSP battery manager
 * @data: buffer with the value to write / receiving the value read
 * @len: size of @data in bytes, at most 255
 *
 * Context: process context, sleeps up to one second.
 * Return: 0 on success or a negative error code.
 */
int mca_adsp_glink_qbg_write_prop(unsigned int prop_id, void *data, int len)
{
	return mca_adsp_glink_write(MCA_ADSP_GLINK_QBG_OWNER, MCA_ADSP_OPCODE_WRITE, prop_id, data, len);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_write_prop);

/**
 * mca_adsp_glink_qbg_read_prop() - read a property through the QBG owner
 * @prop_id: property id understood by the ADSP battery manager
 * @data: buffer with the value to write / receiving the value read
 * @len: size of @data in bytes, at most 255
 *
 * Context: process context, sleeps up to one second.
 * Return: 0 on success or a negative error code.
 */
int mca_adsp_glink_qbg_read_prop(unsigned int prop_id, void *data, int len)
{
	return mca_adsp_glink_write(MCA_ADSP_GLINK_QBG_OWNER, MCA_ADSP_OPCODE_READ, prop_id, data, len);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_read_prop);

static int mca_adsp_glink_add_ops(struct list_head *head, struct mca_adsp_call_back *ops, void *priv)
{
	struct mca_adsp_ops_node *node;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->ops = ops;
	node->priv = priv;
	list_add_tail(&node->list, head);
	return 0;
}

int mca_adsp_glink_resister_ops(struct mca_adsp_call_back *ops, void *priv)
{
	return mca_adsp_glink_add_ops(&g_mca_adsp_glink_ops_head, ops, priv);
}
EXPORT_SYMBOL(mca_adsp_glink_resister_ops);

int mca_adsp_glink_qbg_resister_ops(struct mca_adsp_call_back *ops, void *priv)
{
	return mca_adsp_glink_add_ops(&g_mca_adsp_glink_qbg_ops_head, ops, priv);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_resister_ops);

void mca_adsp_glink_state_cb(void *priv, enum pmic_glink_state state)
{
	struct mca_adsp_glink *mca_glink_data = priv;
	struct mca_adsp_ops_node *node;

	atomic_set(&mca_glink_data->state, state);
	if (state == PMIC_GLINK_STATE_UP) {
		schedule_work(&mca_glink_data->sync_work);
		return;
	}

	list_for_each_entry(node, &g_mca_adsp_glink_ops_head, list) {
		if (node->ops->glink_down_cb)
			node->ops->glink_down_cb(node->priv);
	}
	list_for_each_entry(node, &g_mca_adsp_glink_qbg_ops_head, list) {
		if (node->ops->glink_down_cb)
			node->ops->glink_down_cb(node->priv);
	}
}

static void mca_adsp_glink_sync_work(struct work_struct *work)
{
	struct mca_adsp_ops_node *node;

	list_for_each_entry(node, &g_mca_adsp_glink_ops_head, list) {
		if (node->ops->sync_data_cb)
			node->ops->sync_data_cb(node->priv);
	}
	list_for_each_entry(node, &g_mca_adsp_glink_qbg_ops_head, list) {
		if (node->ops->sync_data_cb)
			node->ops->sync_data_cb(node->priv);
	}
}

static void mca_adsp_glink_handle_notification(struct mca_adsp_notify_msg *msg)
{
	struct list_head *head = msg->hdr.owner == MCA_ADSP_GLINK_QBG_OWNER ?
			&g_mca_adsp_glink_qbg_ops_head : &g_mca_adsp_glink_ops_head;
	struct mca_adsp_ops_node *node;

#line 287
	mca_log_info("mca_adsp receive notification %d, owner %d\n", msg->notify_id, msg->hdr.owner);
	list_for_each_entry(node, head, list) {
		if (node->ops->notify_cb)
			node->ops->notify_cb(msg->notify_id, msg->data, sizeof(msg->data), node->priv);
	}
}

static void mca_adsp_glink_handle_message(struct mca_adsp_glink *mca_glink_data,
		struct mca_adsp_resp_msg *msg, size_t len)
{
	if (len != sizeof(*msg) || msg->ret_code) {
#line 302
		mca_log_err("glink retcode error, property_id: %d, retcode: %d, len: %lu %lu\n",
			msg->prop_id, msg->ret_code, len, sizeof(*msg));
		mca_glink_data->ret_code = msg->ret_code;
	} else if (msg->seq_num != mca_glink_data->seq_num) {
#line 310
		mca_log_err("invalid seq_num: %d != %d\n", mca_glink_data->seq_num, msg->seq_num);
		return;
	} else if (msg->hdr.opcode == MCA_ADSP_OPCODE_READ) {
		memcpy(mca_glink_data->read_buf, msg->data, sizeof(mca_glink_data->read_buf));
	}
	complete(&mca_glink_data->ack);
}

static int mca_adsp_glink_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;

	if (hdr->type == PMIC_GLINK_NOTIFY)
		mca_adsp_glink_handle_notification(data);
	else
		mca_adsp_glink_handle_message(priv, data, len);

	return 0;
}

static int mca_adsp_glink_probe(struct platform_device *pdev)
{
	struct mca_adsp_glink *mca_glink_data;
	struct pmic_glink_client_data client_data = { };
	struct pmic_glink_client_data qbg_client_data = { };
	int rc;

	mca_glink_data = devm_kzalloc(&pdev->dev, sizeof(*mca_glink_data), GFP_KERNEL);
	if (!mca_glink_data)
		return -ENOMEM;

	mca_glink_data->dev = &pdev->dev;
	mca_glink_data->prop_id = -1;
	mutex_init(&mca_glink_data->rw_lock);
	init_completion(&mca_glink_data->ack);
	atomic_set(&mca_glink_data->state, PMIC_GLINK_STATE_UP);

	client_data.id = MCA_ADSP_GLINK_OWNER;
	client_data.name = "mca_adap_glink";
	client_data.msg_cb = mca_adsp_glink_callback;
	client_data.priv = mca_glink_data;
	client_data.state_cb = mca_adsp_glink_state_cb;
	mca_glink_data->client = pmic_glink_register_client(mca_glink_data->dev, &client_data);
	if (IS_ERR(mca_glink_data->client)) {
		rc = PTR_ERR(mca_glink_data->client);
		if (rc != -EPROBE_DEFER)
#line 417
			mca_log_err("Error in registering with pmic_glink %d\n", rc);
		return rc;
	}

	qbg_client_data.id = MCA_ADSP_GLINK_QBG_OWNER;
	qbg_client_data.name = "mca_adap_qbg_glink";
	qbg_client_data.msg_cb = mca_adsp_glink_callback;
	qbg_client_data.priv = mca_glink_data;
	qbg_client_data.state_cb = mca_adsp_glink_state_cb;
	mca_glink_data->qbg_client = pmic_glink_register_client(mca_glink_data->dev, &qbg_client_data);
	if (IS_ERR(mca_glink_data->qbg_client)) {
		rc = PTR_ERR(mca_glink_data->qbg_client);
		if (rc != -EPROBE_DEFER)
#line 430
			mca_log_err("Error in registering with qbg_glink %d\n", rc);
		return rc;
	}

	INIT_WORK(&mca_glink_data->sync_work, mca_adsp_glink_sync_work);
	platform_set_drvdata(pdev, mca_glink_data);
	g_mca_adsp_glink = mca_glink_data;
#line 437
	mca_log_err("probe ok\n");
	return 0;
}

static int mca_adsp_glink_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id mca_adsp_match_table[] = {
	{ .compatible = "mca,adsp_glink" },
	{},
};

static struct platform_driver mca_adsp_driver = {
	.driver = {
		.name = "mca_adsp_glink",
		.of_match_table = mca_adsp_match_table,
	},
	.probe = mca_adsp_glink_probe,
	.remove = mca_adsp_glink_remove,
};
module_platform_driver(mca_adsp_driver);

MODULE_DESCRIPTION("mca glink driver");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
