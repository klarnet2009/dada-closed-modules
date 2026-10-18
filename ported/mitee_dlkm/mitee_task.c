// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 XiaoMi, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <asm/io.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/time64.h>
#include <linux/panic.h>
#include <linux/gfp.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/crc32.h>
#include "optee_ffa.h"
#include "optee_private.h"
#include "optee_bench.h"
#include "optee_rpc_cmd.h"
#include "mitee_task.h"

int32_t mitee_msg_queue_init(struct mitee_msg_queue *queue)
{
	void *va = NULL;

	if (!queue) {
		pr_err("invalid param\n");
		return -EINVAL;
	}

	va = alloc_pages_exact(MITEE_MSG_QUEUE_SIZE, GFP_KERNEL | __GFP_ZERO);
	if (!va) {
		pr_err("message queue alloc fail\n");
		return -ENOMEM;
	}

	queue->pa = virt_to_phys(va);
	queue->queue_size = MITEE_MSG_QUEUE_SIZE;
	queue->ringbuf_size = MITEE_MSG_RINGBUF_SIZE;

	/* receive message to mitee */
	mutex_init(&queue->rx_buf.lock);
	queue->rx_buf.desc = (struct shm_mem_desc *)va;
	queue->rx_buf.buf =
		(struct shm_mem_msg *)(va + MITEE_MSG_QUEUE_DESC_SIZE);
	/* 16 slots */
	queue->rx_buf.desc->size =
		MITEE_MSG_RINGBUF_SIZE / sizeof(struct shm_mem_msg);

	/* send message to mitee */
	mutex_init(&queue->tx_buf.lock);
	queue->tx_buf.desc =
		(struct shm_mem_desc *)(va + MITEE_MSG_TX_DESC_OFFSET);
	queue->tx_buf.buf =
		(struct shm_mem_msg *)(va + MITEE_MSG_QUEUE_DESC_SIZE +
				       MITEE_MSG_RINGBUF_SIZE);
	/* 16 slots */
	queue->tx_buf.desc->size =
		MITEE_MSG_RINGBUF_SIZE / sizeof(struct shm_mem_msg);

	pr_info("mitee allocated task queue pa: %#llx va: %p\n", queue->pa, va);

	return 0;
}

int32_t mitee_msg_queue_deinit(struct mitee_msg_queue *queue)
{
	if (!queue) {
		pr_err("invalid param\n");
		return -EINVAL;
	}

	free_pages_exact(phys_to_virt(queue->pa), queue->queue_size);

	mutex_destroy(&queue->rx_buf.lock);
	mutex_destroy(&queue->tx_buf.lock);

	pr_info("mitee message queue destroyed\n");

	return 0;
}

union msgq_args {
	union common_args common;

	struct {
		u64 cmd;
		u64 paddr;
		u64 size;
	} in;

	struct {
		u64 resp;
	} out;
};

int32_t mitee_msg_queue_register(void)
{
	int32_t rc = 0;
	struct optee *optee = get_optee_drv_state();
	union msgq_args args;

	if (!optee) {
		pr_err("invalid param\n");
		return -EINVAL;
	}

	args.in.cmd = OPTEE_FFA_SET_MITEE_MSG_BUFFER;
	args.in.paddr = optee->msg_queue.pa;
	args.in.size = optee->msg_queue.queue_size;
	rc = optee->comm_ops->call(&args.common);
	if (rc) {
		pr_err("register msg buf failed:%d\n", rc);
		return rc;
	}

	return 0;
}

static int32_t mitee_msg_enqueue(struct tee_context *ctx, struct shm_mem_msg *msg)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct mitee_msg_ringbuf *tx_buf = NULL;
	struct shm_mem_msg *buf = NULL;

	if (!ctx || !msg) {
		pr_err("invalid param\n");
		return -EINVAL;
	}

	tx_buf = &optee->msg_queue.tx_buf;
	buf = tx_buf->buf;

	mutex_lock(&tx_buf->lock);

	if (((tx_buf->desc->head + 1) % tx_buf->desc->size) ==
	    tx_buf->desc->tail) {
		pr_err("tx buf full\n");
		mutex_unlock(&tx_buf->lock);
		return -EBUSY;
	}

	memcpy(&buf[tx_buf->desc->head], msg, sizeof(struct shm_mem_msg));

	/* update tx buf head */
	smp_store_release(&tx_buf->desc->head,
			  (tx_buf->desc->head + 1) % tx_buf->desc->size);

	mutex_unlock(&tx_buf->lock);

	return 0;
}

static int32_t mitee_msg_dequeue(struct tee_context *ctx, struct shm_mem_msg *msg)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct mitee_msg_ringbuf *rx_buf = NULL;
	struct shm_mem_msg *buf = NULL;

	if (!msg) {
		pr_err("invalid param\n");
		return -EINVAL;
	}

	rx_buf = &optee->msg_queue.rx_buf;
	buf = rx_buf->buf;

	mutex_lock(&rx_buf->lock);

	if (rx_buf->desc->tail == rx_buf->desc->head) {
		pr_err("rx buf empty\n");
		mutex_unlock(&rx_buf->lock);
		return -EBUSY;
	}

	memcpy(msg, &buf[rx_buf->desc->tail], sizeof(struct shm_mem_msg));
	memset(&buf[rx_buf->desc->tail], 0, sizeof(struct shm_mem_msg));

	/* update rx buf tail */
	smp_store_release(&rx_buf->desc->tail,
			  (rx_buf->desc->tail + 1) % rx_buf->desc->size);

	mutex_unlock(&rx_buf->lock);

	return 0;
}

static void mitee_msg_pack(uint32_t msg_id, uint32_t msg_type,
			   struct optee_msg_arg *arg, struct shm_mem_msg *msg)
{
	struct timespec64 t;
	uint32_t sz = 0;
	void *payload = &msg->cmd;

	msg->magic = MITEE_SHM_MSG_MAGIC;
	msg->version = MITEE_SHM_MSG_VER_1;
	msg->rsv0 = 0;
	msg->msg_type = msg_type;
	msg->msg_id = msg_id;

	if (arg != NULL) {
		sz = OPTEE_MSG_GET_ARG_SIZE(arg->num_params);
		memcpy(payload, arg, sz);
	} else {
		sz = OPTEE_MSG_GET_ARG_SIZE(MITEE_MSG_MAX_NUM_PARAMS);
		memset(payload, 0, sz);
	}

	ktime_get_real_ts64(&t);
	msg->timestamp = t.tv_sec;

#if MITEE_MSG_CRC
	/* crc32 with seed 0 */
	msg->crc = crc32(0, msg, offsetof(struct shm_mem_msg, crc));
#else
	msg->crc = 0;
#endif
}

static int32_t mitee_msg_unpack(struct shm_mem_msg *msg, uint32_t *msg_id,
				uint32_t *msg_type, struct optee_msg_arg **arg)
{
	if ((!msg) || (!msg_id) || (!msg_type) || (!arg)) {
		pr_err("invalid param\n");
		return -EINVAL;
	}

	if (msg->magic != MITEE_SHM_MSG_MAGIC) {
		pr_err("invalid magic\n");
		return -EINVAL;
	}

	if (msg->version != MITEE_SHM_MSG_VER_1) {
		pr_err("invalid version\n");
		return -EINVAL;
	}
#if MITEE_MSG_CRC
	if (crc32(0, msg, offsetof(struct shm_mem_msg, crc)) != msg->crc) {
		return -EINVAL;
	}
#endif
	*msg_id = msg->msg_id;
	*msg_type = msg->msg_type;
	/* parse out address of optee_msg_arg without memcpy */
	*arg = (struct optee_msg_arg *)&msg->cmd;

	return 0;
}

void mitee_task_list_init(struct mitee_task_list *task_list)
{
	mutex_init(&task_list->lock);
	idr_init(&task_list->idr);
	INIT_LIST_HEAD(&task_list->list);
}

void mitee_task_list_deinit(struct mitee_task_list *task_list)
{
	mutex_destroy(&task_list->lock);
	idr_destroy(&task_list->idr);
}

static inline void mitee_task_insert(struct mitee_task_list *task_list,
				     struct mitee_task *task)
{
	list_add_tail(&task->link, &task_list->list);
}

static inline void mitee_task_remove(struct mitee_task *task)
{
	list_del(&task->link);
}

struct mitee_task *mitee_task_alloc(struct tee_context *ctx, uint32_t type,
				    struct optee_msg_arg *msg_arg)
{
	struct mitee_task *task = NULL;
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct mitee_task_list *task_list = &optee->task_list;

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return ERR_PTR(-ENOMEM);

	/* task context */
	task->data = ctx;
	INIT_LIST_HEAD(&task->link);
	init_completion(&task->c);

	task->type = type;

	if (type == MITEE_REE_REQUEST) {
		mutex_lock(&task_list->lock);
		/* idr alloc should be exclusive*/
		task->id = idr_alloc(&task_list->idr, task, REE_REQUEST_MIN_ID,
				     REE_REQUEST_MAX_ID, GFP_KERNEL);
		if (task->id <= 0) {
			mutex_unlock(&task_list->lock);
			return ERR_PTR(-EINVAL);
		}

		task->legacy_msg = msg_arg;
		task->status = MITEE_TASK_INIT;

		mitee_task_insert(task_list, task);
		mutex_unlock(&task_list->lock);
	} else {
		pr_err("invalid param\n");
		return ERR_PTR(-EINVAL);
	}

	return task;
}

void mitee_task_free(struct tee_context *ctx, int32_t id)
{
	struct mitee_task *task = NULL;
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct mitee_task_list *task_list = &optee->task_list;

	mutex_lock(&task_list->lock);
	task = idr_find(&task_list->idr, id);
	if (!task) {
		pr_warn("task id invalid\n");
		BUG_ON(0);
	}

	idr_remove(&task_list->idr, id);
	mutex_unlock(&task_list->lock);

	kfree(task);
}

static inline struct mitee_task *
mitee_task_pop(struct mitee_task_list *task_list)
{
	struct mitee_task *task = NULL;

	mutex_lock(&task_list->lock);
	task = list_first_entry_or_null(&task_list->list, struct mitee_task,
					link);
	if (task)
		mitee_task_remove(task);
	mutex_unlock(&task_list->lock);

	return task;
}

/*
 * In the stock the shm alloc/free handlers are not separate functions: their __func__ strings do not exist,
 * the messages carry the enclosing function name. Emulated here for the two helpers.
 */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, "handle_rpc_cmd_callback"

static void handle_rpc_func_cmd_shm_alloc(struct tee_context *ctx,
					  struct optee_msg_arg *arg)
{
	struct tee_shm *shm;

	if (arg->num_params != 1 ||
	    arg->params[0].attr != OPTEE_MSG_ATTR_TYPE_VALUE_INPUT) {
		pr_err("invalid param\n");
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		return;
	}

	switch (arg->params[0].u.value.a) {
	case OPTEE_RPC_SHM_TYPE_APPL:
		shm = optee_rpc_cmd_alloc_suppl(ctx, arg->params[0].u.value.b);
		break;
	case OPTEE_RPC_SHM_TYPE_KERNEL:
		shm = tee_shm_alloc(ctx, arg->params[0].u.value.b,
				    TEE_SHM_MAPPED | TEE_SHM_PRIV);
		break;
	case OPTEE_RPC_SHM_TYPE_GLOBAL:
		shm = tee_shm_alloc(ctx, arg->params[0].u.value.b,
				    TEE_SHM_MAPPED | TEE_SHM_PRIV | TEE_SHM_DMA_BUF);
		break;
	default:
		pr_err("invalid param\n");
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		return;
	}

	if (IS_ERR(shm)) {
		pr_err("shm is error\n");
		arg->ret = TEEC_ERROR_OUT_OF_MEMORY;
		return;
	}

	arg->params[0] = (struct optee_msg_param){
		.attr = OPTEE_MSG_ATTR_TYPE_TMEM_OUTPUT,
		.u.tmem.buf_ptr = shm->paddr,
		.u.tmem.size = tee_shm_get_size(shm),
		.u.tmem.shm_ref = (unsigned long)shm,
	};

	arg->ret = TEEC_SUCCESS;
}

static void handle_rpc_func_cmd_shm_free(struct tee_context *ctx,
					 struct optee *optee,
					 struct optee_msg_arg *arg)
{
	struct tee_shm *shm = NULL;

	if (arg->num_params != 1 ||
		arg->params[0].attr != OPTEE_MSG_ATTR_TYPE_VALUE_INPUT) {
		pr_err("invalid param\n");
		goto err_bad_param;
	}

	shm = (struct tee_shm *)arg->params[0].u.value.b;
	if (!shm) {
		pr_err("shm is error\n");
		goto err_bad_param;
	}

	switch (arg->params[0].u.value.a) {
	case OPTEE_RPC_SHM_TYPE_APPL:
		optee_rpc_cmd_free_suppl(ctx, shm);
		break;
	case OPTEE_RPC_SHM_TYPE_KERNEL:
	case OPTEE_RPC_SHM_TYPE_GLOBAL:
		tee_shm_free(shm);
		break;
	default:
		pr_err("invalid param\n");
		goto err_bad_param;
	}

	arg->ret = TEEC_SUCCESS;
	return;

err_bad_param:
	arg->ret = TEEC_ERROR_BAD_PARAMETERS;
}

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

/*RPC cmd message only support transfer in reserved memory*/
static void handle_rpc_func_cmd(struct tee_context *ctx,
				struct optee_msg_arg *arg)
{
	struct optee *optee = NULL;

	if (arg == NULL) {
		return;
	}

	if (ctx == NULL) {
		arg->ret = TEEC_ERROR_ITEM_NOT_FOUND;
		return;
	}

	optee = tee_get_drvdata(ctx->teedev);

	if (DEBUG_RPC)
		pr_info("rpc inner session=%x cmd=%x func=%x\n",
			arg->session, arg->cmd, arg->func);

	arg->ret_origin = TEEC_ORIGIN_COMMS;
	switch (arg->cmd) {
	case OPTEE_RPC_CMD_SHM_ALLOC:
		handle_rpc_func_cmd_shm_alloc(ctx, arg);
		break;
	case OPTEE_RPC_CMD_SHM_FREE:
		handle_rpc_func_cmd_shm_free(ctx, optee, arg);
		break;
	default:
		optee_rpc_cmd(ctx, optee, arg);
	}
}

/*
 * One yielding call: pop the oldest REE request, push it to the secure side and
 * serve the RPCs of the secure side until the response of the request arrives.
 * Inlined into mitee_worker in the stock (single worker thread).
 */
static int32_t mitee_yielding_call(struct optee *optee)
{
	struct mitee_task_list *task_list = &optee->task_list;
	struct mitee_task *task = NULL;
	struct tee_context *ctx = NULL;
	/* mitee message buffer */
	struct shm_mem_msg msg;
	struct shm_mem_msg msg_recv;
	struct optee_msg_arg *arg_addr = NULL;
	int32_t rc = 0;
	int32_t msg_id = 0;
	uint32_t msg_type = 0;
	uint32_t sz = 0;
	uint32_t handle_irq = 0;
	union common_args args;

	args.in.cmd = OPTEE_FFA_YIELDING_CALL_WITH_ARG;
	args.in.param[0] = OPTEE_FFA_MBZ;
	args.in.param[1] = OPTEE_FFA_MBZ;
	args.in.param[2] = OPTEE_FFA_MBZ;
	args.in.param[3] = OPTEE_FFA_MBZ;

	/* Step 1: retrieve ree request task */
	task = mitee_task_pop(task_list);
	if (!task) {
		pr_info("task list empty\n");
		return 0;
	}
	ctx = task->data;

	mitee_msg_pack(task->id, task->type, task->legacy_msg, &msg);

	task->status = MITEE_TASK_SENT;

	while (true) {
		/* Step 2: enqueue task message, except irq handle */
		if (!handle_irq) {
			rc = mitee_msg_enqueue(ctx, &msg);
			if (rc) {
				pr_err("msg enqueue err: %d\n", rc);
				return rc;
			}
		}

		/* clear irq flag */
		handle_irq = 0;

		optee_bm_timestamp();
		rc = optee->comm_ops->call(&args);
		if (rc)
			return rc;

		atomic_notifier_call_chain(&optee->notifier,
					   MITEE_CALL_RETURNED, NULL);

		optee_bm_timestamp();

		if (args.out.resp == OPTEE_FFA_YIELDING_CALL_RETURN_INTERRUPT) {
			args.in.cmd = OPTEE_FFA_YIELDING_CALL_WITH_ARG |
				      OPTEE_FFA_YIELDING_CALL_RETURN_INTERRUPT;
			args.in.param[0] = OPTEE_FFA_MBZ;
			args.in.param[1] = OPTEE_FFA_MBZ;
			args.in.param[2] = OPTEE_FFA_MBZ;
			args.in.param[3] = OPTEE_FFA_MBZ;

			/* set irq flag */
			handle_irq = 1;
			continue;
		}

		/*
		 * Step 4: dequeue message, Scheduling may occur on the TEE side, so
		 * the data sent and received may not belong to the same task.
		 */
		rc = mitee_msg_dequeue(ctx, &msg_recv);
		if (rc) {
			pr_err("msg recv dequeue err: %d\n", rc);
			return rc;
		}

		/* Step 5: unpack received message */
		rc = mitee_msg_unpack(&msg_recv, &msg_id, &msg_type, &arg_addr);
		if (rc) {
			pr_err("msg unpack err: %d\n", rc);
			return rc;
		}

		/* Step 6: handle received message */
		if (msg_type == MITEE_RPC_REQUEST) {
			handle_rpc_func_cmd(ctx, arg_addr);

			/* message id of RPC_REQUEST and RPC_RESUME should be match */
			mitee_msg_pack(msg_id, MITEE_RPC_RESUME, arg_addr,
				       &msg);

			args.in.cmd = OPTEE_FFA_YIELDING_CALL_WITH_ARG;
			args.in.param[0] = OPTEE_FFA_MBZ;
			args.in.param[1] = OPTEE_FFA_MBZ;
			args.in.param[2] = OPTEE_FFA_MBZ;
			args.in.param[3] = OPTEE_FFA_MBZ;
			continue;
		} else if (msg_type == MITEE_REE_RESPONSE) {
			mutex_lock(&task_list->lock);
			task = idr_find(&task_list->idr, msg_id);
			if (!task) {
				mutex_unlock(&task_list->lock);
				pr_warn("task id invalid, maybe task aborted\n");
#line 626 "../vendor/xiaomi/proprietary/kernelmodules/mitee_drv/src/mitee_task.c"
				BUG();
#line 593 "../vendor/xiaomi/proprietary/kernelmodules/mitee_drv/src/mitee_task.c"
			}

			sz = OPTEE_MSG_GET_ARG_SIZE(arg_addr->num_params);
			memcpy(task->legacy_msg, arg_addr, sz);
			task->status = MITEE_TASK_DONE;

			/* wakeup CA thread */
			complete(&task->c);

			mutex_unlock(&task_list->lock);
			return 0;
		} else {
			pr_err("invalid msg type: %#x\n", msg_type);
			return -EINVAL;
		}
	}
}

/*
 * The single mitee worker thread (stock: one kthread, started by init_module)
 */
int32_t mitee_worker(void *arg)
{
	struct optee *optee = (struct optee *)arg;
	int32_t rc = 0;
	int cnt = atomic_inc_return(&optee->workers_run);

	while (true) {
		rc = wait_event_interruptible_exclusive(optee->workers_wq,
							!list_empty(&optee->task_list.list));
		if (rc == -EINTR) {
			pr_info("[%d] task abort\n", cnt);
			continue;
		}

		rc = mitee_yielding_call(optee);
#line 626 "../vendor/xiaomi/proprietary/kernelmodules/mitee_drv/src/mitee_task.c"
		BUG_ON(rc);
#line 632 "../vendor/xiaomi/proprietary/kernelmodules/mitee_drv/src/mitee_task.c"
	}

	return rc;
}
